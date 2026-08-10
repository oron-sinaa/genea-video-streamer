// Phase 6.6: Comprehensive HTTP Server Unit & Integration Tests
//
// Design rationale:
//   StreamManager creates workers in its constructor (createWorkers()), not in start().
//   getStream() searches workers_ by name regardless of worker runtime state.
//   HttpServer only checks (worker != nullptr), not worker status.
//   Therefore: constructing a StreamManager with a fake-named stream is sufficient
//   to make getStream() return non-null, enabling file-serving path testing —
//   without ever spawning RTSP threads. Zero timing sensitivity.
//
// Test groups:
//   A. Server Lifecycle      (3 tests)
//   B. HTTP Routing          (7 tests)
//   C. HTTP Method Handling  (2 tests)
//   D. Response Headers      (3 tests)
//   E. JSON Response Format  (3 tests)
//   F. File Serving          (3 tests)
//   G. Multi-Request         (1 test)
//   Total: 22 tests

#include "streamer/Config.h"
#include "streamer/HttpServer.h"
#include "streamer/StreamManager.h"

extern "C" {
#include <libavutil/log.h>
}

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

// ============================================================
// SECTION 1: Parsed HTTP Response
// ============================================================

struct HttpResponse {
    int status_code = 0;
    std::map<std::string, std::string> headers;  // all keys lowercased
    std::string body;
    bool success = false;
};

// ============================================================
// SECTION 2: Port Utilities
// ============================================================

// Binds to port 0, lets the OS assign, reads back the port, then closes.
// A tiny TOCTOU race exists between close and re-bind; acceptable in tests.
uint16_t findFreePort() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 18900;

    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(s);
        return 18900;
    }

    socklen_t len = sizeof(addr);
    getsockname(s, reinterpret_cast<struct sockaddr*>(&addr), &len);
    uint16_t port = ntohs(addr.sin_port);
    close(s);
    return port;
}

// Polls the port with a real minimal HTTP GET until a valid response is
// received or timeout elapses.  Using an actual request (not bare connect-close)
// ensures the server has finished the accept()+recv() cycle before we return,
// preventing test races where the next connection queues behind an unfinished one.
bool waitForServerReady(uint16_t port, int timeout_ms = 2000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Aggressive timeouts so each poll attempt is fast
        struct timeval tv{0, 100000};  // 100ms
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            // Probe /api/health — pure JSON, no file I/O, guaranteed fast response
            const char probe[] = "GET /api/health HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
            send(s, probe, sizeof(probe) - 1, 0);
            // Read (and discard) the response to let the server finish the recv() cycle
            char buf[256];
            ssize_t n;
            while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {}
            close(s);
            return true;
        }
        close(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// ============================================================
// SECTION 3: Minimal HTTP Client (raw POSIX sockets)
// ============================================================

HttpResponse sendRequest(uint16_t port,
                         const std::string& method,
                         const std::string& path) {
    HttpResponse resp;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return resp;

    // 3-second read/write timeout
    struct timeval tv{3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(s);
        return resp;
    }

    // HTTP/1.0 so server closes connection after response — no chunked encoding
    std::string req = method + " " + path + " HTTP/1.0\r\n"
                    + "Host: 127.0.0.1\r\n"
                    + "Connection: close\r\n"
                    + "\r\n";
    send(s, req.c_str(), req.size(), 0);

    // Read until connection closes
    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    close(s);

    if (raw.empty()) return resp;

    // Parse status line: "HTTP/1.x NNN ..."
    size_t first_crlf = raw.find("\r\n");
    if (first_crlf == std::string::npos) return resp;

    std::string status_line = raw.substr(0, first_crlf);
    size_t sp1 = status_line.find(' ');
    if (sp1 == std::string::npos || sp1 + 3 >= status_line.size()) return resp;
    try {
        resp.status_code = std::stoi(status_line.substr(sp1 + 1, 3));
    } catch (...) {
        return resp;
    }

    // Parse headers until blank line
    size_t header_block_end = raw.find("\r\n\r\n");
    if (header_block_end == std::string::npos) return resp;

    size_t hdr_pos = first_crlf + 2;
    while (hdr_pos < header_block_end) {
        size_t eol = raw.find("\r\n", hdr_pos);
        if (eol == std::string::npos || eol > header_block_end) break;
        std::string line = raw.substr(hdr_pos, eol - hdr_pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos && colon + 2 <= line.size()) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 2);  // skip ': '
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            resp.headers[key] = val;
        }
        hdr_pos = eol + 2;
    }

    resp.body = raw.substr(header_block_end + 4);
    resp.success = true;
    return resp;
}

// ============================================================
// SECTION 4: Filesystem Helpers
// ============================================================

bool mkdirp(const std::string& path) {
    // Walk path segments and mkdir each
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string sub = path.substr(0, i);
            if (mkdir(sub.c_str(), 0755) < 0 && errno != EEXIST) return false;
        }
    }
    if (mkdir(path.c_str(), 0755) < 0 && errno != EEXIST) return false;
    return true;
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f << content;
    return f.good();
}

// Returns path to a newly created temp directory, or "" on failure.
std::string createTempDir() {
    // Respect $TMPDIR so tests work even when /tmp is read-only (e.g. sandbox)
    const char* tmpdir_env = std::getenv("TMPDIR");
    std::string base = (tmpdir_env && tmpdir_env[0]) ? tmpdir_env : "/tmp";
    std::string tmpl = base + "/test_http_XXXXXX";
    // mkdtemp modifies the string in place; we need a mutable buffer
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* result = mkdtemp(buf.data());
    return result ? std::string(result) : "";
}

void cleanupDir(const std::string& path) {
    // Safety check: must be an absolute path with at least 5 characters
    if (path.size() < 5 || path[0] != '/') return;
    std::string cmd = "rm -rf " + path;
    int ret = std::system(cmd.c_str());
    (void)ret;  // best-effort cleanup; test continues regardless
}

// RAII chdir guard — restores CWD on destruction.
struct ChdirGuard {
    char saved_[4096];
    bool valid_ = false;

    explicit ChdirGuard(const std::string& dir) {
        if (getcwd(saved_, sizeof(saved_)) != nullptr) {
            valid_ = (chdir(dir.c_str()) == 0);
        }
    }
    ~ChdirGuard() {
        if (valid_) {
            int ret = chdir(saved_);
            (void)ret;  // best-effort restore; destructor cannot throw
        }
    }
};

// ============================================================
// SECTION 5: Config Factories
// ============================================================

// Builds a minimal AppConfig with no streams (for routing tests that don't
// need file-serving or stream lookups to succeed).
streamer::AppConfig emptyConfig(uint16_t port = 0) {
    streamer::AppConfig cfg;
    cfg.http.listen_port = port;
    return cfg;
}

// Builds an AppConfig with one named stream (no RTSP attempted — worker
// is created in StreamManager constructor but never started).
streamer::AppConfig configWithStream(const std::string& stream_name, uint16_t port = 0) {
    streamer::AppConfig cfg;
    cfg.http.listen_port = port;
    streamer::StreamConfig stream;
    stream.name = stream_name;
    stream.rtsp_url = "rtsp://127.0.0.1:19999/" + stream_name;  // unreachable — never used
    stream.hls_output = "segments/" + stream_name + "/";        // relative, CWD-based, with trailing slash
    cfg.streams.push_back(stream);
    return cfg;
}

// ============================================================
// SECTION 6: Test Helpers
// ============================================================

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "  FAIL: " << (msg) << "\n";                        \
            return false;                                                      \
        }                                                                      \
    } while (0)

// ============================================================
// GROUP A: Server Lifecycle (3 tests)
// ============================================================

bool test_server_starts_and_stops() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    CHECK(server.start(), "start() should return true");
    CHECK(server.isRunning(), "isRunning() should be true after start");
    CHECK(waitForServerReady(port, 1000), "server should accept connections");

    server.stop();
    CHECK(!server.isRunning(), "isRunning() should be false after stop");
    return true;
}

bool test_server_double_start_is_idempotent() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    CHECK(server.start(), "first start should succeed");
    CHECK(waitForServerReady(port, 1000), "server should accept connections");
    // Second start on already-running server should return true (idempotent)
    CHECK(server.start(), "second start should also return true");
    CHECK(server.isRunning(), "server should still be running");
    server.stop();
    return true;
}

bool test_server_bind_failure_on_taken_port() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    // First server binds the port
    streamer::HttpServer::ServerConfig sc1;
    sc1.listen_port = port;
    streamer::HttpServer server1(&manager, sc1);
    CHECK(server1.start(), "first server should start");
    CHECK(waitForServerReady(port, 1000), "first server must be ready");

    // Second server on same port should fail
    streamer::HttpServer::ServerConfig sc2;
    sc2.listen_port = port;
    streamer::HttpServer server2(&manager, sc2);
    bool started2 = server2.start();
    CHECK(!started2, "second server on same port should fail");
    CHECK(!server2.getLastError().empty(), "lastError should be set on failure");

    server1.stop();
    return true;
}

// ============================================================
// GROUP B: HTTP Routing — Status Codes (7 tests)
// ============================================================

bool test_health_endpoint_returns_200() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/health");
    CHECK(resp.success, "/api/health request failed");
    CHECK(resp.status_code == 200, "/api/health should return 200");

    server.stop();
    return true;
}

bool test_streams_list_endpoint_returns_200() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/streams");
    CHECK(resp.success, "/api/streams request failed");
    CHECK(resp.status_code == 200, "/api/streams should return 200");

    server.stop();
    return true;
}

bool test_stream_by_name_not_found_returns_404() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/streams/nonexistent-cam");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 404, "/api/streams/<unknown> should return 404");

    server.stop();
    return true;
}

bool test_hls_playlist_unknown_stream_returns_404() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/hls/ghost-cam/live.m3u8");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 404, "/hls/<unknown>/live.m3u8 should return 404");

    server.stop();
    return true;
}

bool test_hls_segment_unknown_stream_returns_404() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/hls/ghost-cam/segment-000.ts");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 404, "/hls/<unknown>/*.ts should return 404");

    server.stop();
    return true;
}

bool test_index_route_returns_200() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/");
    CHECK(resp.success, "/ request failed");
    CHECK(resp.status_code == 200, "/ should return 200");

    server.stop();
    return true;
}

bool test_unknown_route_returns_404() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/completely/unknown/path");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 404, "unknown path should return 404");

    server.stop();
    return true;
}

// ============================================================
// GROUP C: HTTP Method Handling (2 tests)
// ============================================================

bool test_post_method_returns_405() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "POST", "/api/health");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 405, "POST should return 405 Method Not Allowed");

    server.stop();
    return true;
}

bool test_head_method_returns_200() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    // HEAD is explicitly allowed in the server's method check
    auto resp = sendRequest(port, "HEAD", "/api/health");
    CHECK(resp.success, "request succeeded at TCP level");
    CHECK(resp.status_code == 200, "HEAD /api/health should return 200");

    server.stop();
    return true;
}

// ============================================================
// GROUP D: Response Headers (3 tests)
// ============================================================

bool test_cors_headers_present_when_enabled() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    sc.enable_cors = true;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/health");
    CHECK(resp.success, "request succeeded");
    CHECK(resp.headers.count("access-control-allow-origin") > 0,
          "CORS header access-control-allow-origin should be present");
    CHECK(resp.headers.at("access-control-allow-origin") == "*",
          "CORS origin should be *");

    server.stop();
    return true;
}

bool test_cors_headers_absent_when_disabled() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    sc.enable_cors = false;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/health");
    CHECK(resp.success, "request succeeded");
    CHECK(resp.headers.count("access-control-allow-origin") == 0,
          "CORS header should NOT be present when CORS is disabled");

    server.stop();
    return true;
}

bool test_content_type_json_for_api_routes() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    // Both /api/health and /api/streams should return JSON
    for (const auto& path : {"/api/health", "/api/streams"}) {
        auto resp = sendRequest(port, "GET", path);
        CHECK(resp.success, std::string("request to ") + path + " succeeded");
        CHECK(resp.headers.count("content-type") > 0,
              std::string("content-type header missing for ") + path);
        const std::string& ct = resp.headers.at("content-type");
        CHECK(ct.find("application/json") != std::string::npos,
              std::string("content-type should be JSON for ") + path);
    }

    server.stop();
    return true;
}

// ============================================================
// GROUP E: JSON Response Validation (3 tests)
// ============================================================

bool test_health_json_has_required_fields() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/health");
    CHECK(resp.success && resp.status_code == 200, "request should succeed");

    // Verify required JSON keys are present in body
    const std::string& body = resp.body;
    CHECK(body.find("total_packets_read") != std::string::npos,
          "JSON should contain total_packets_read");
    CHECK(body.find("total_packets_written") != std::string::npos,
          "JSON should contain total_packets_written");
    CHECK(body.find("total_reconnects") != std::string::npos,
          "JSON should contain total_reconnects");
    CHECK(body.find("active_streams") != std::string::npos,
          "JSON should contain active_streams");
    CHECK(body.find("error_streams") != std::string::npos,
          "JSON should contain error_streams");
    CHECK(body.find("stream_stats") != std::string::npos,
          "JSON should contain stream_stats array");

    server.stop();
    return true;
}

bool test_streams_list_json_has_required_fields() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/streams");
    CHECK(resp.success && resp.status_code == 200, "request should succeed");

    const std::string& body = resp.body;
    CHECK(body.find("total_streams") != std::string::npos,
          "JSON should contain total_streams");
    CHECK(body.find("streams") != std::string::npos,
          "JSON should contain streams array");

    server.stop();
    return true;
}

bool test_health_json_empty_manager_has_zero_counts() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    auto resp = sendRequest(port, "GET", "/api/health");
    CHECK(resp.success && resp.status_code == 200, "request should succeed");

    // With empty manager (no streams), active_streams and error_streams should be 0
    CHECK(resp.body.find("\"active_streams\": 0") != std::string::npos,
          "active_streams should be 0 with no streams");
    CHECK(resp.body.find("\"error_streams\": 0") != std::string::npos,
          "error_streams should be 0 with no streams");

    server.stop();
    return true;
}

// ============================================================
// GROUP F: File Serving (3 tests)
//
// These tests use a StreamManager constructed with a named stream ("test-cam").
// createWorkers() is called in StreamManager's constructor, so getStream("test-cam")
// returns non-null immediately — without ever calling start() or touching RTSP.
// Files are created in a temp dir; ChdirGuard ensures CWD is restored.
// ============================================================

bool test_hls_playlist_served_when_file_exists() {
    std::string tmp_dir = createTempDir();
    if (tmp_dir.empty()) {
        std::cerr << "  FAIL: could not create temp dir\n";
        return false;
    }

    const std::string stream_name = "test-cam-playlist";

    // Create the playlist file at: <tmpdir>/segments/<stream>/live.m3u8
    std::string seg_dir = tmp_dir + "/segments/" + stream_name;
    if (!mkdirp(seg_dir)) {
        cleanupDir(tmp_dir);
        std::cerr << "  FAIL: could not create segment dir\n";
        return false;
    }

    const std::string playlist_content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:4\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXTINF:4.0,\n"
        "segment-000.ts\n";
    CHECK(writeFile(seg_dir + "/live.m3u8", playlist_content),
          "should create test playlist file");

    uint16_t port = findFreePort();
    // StreamManager with "test-cam-playlist" stream — workers created in constructor
    auto cfg = configWithStream(stream_name, port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    bool started;
    {
        // Chdir to tmp_dir so server finds files at relative path segments/...
        ChdirGuard g(tmp_dir);
        started = server.start() && waitForServerReady(port, 1000);
        if (!started) {
            server.stop();
            cleanupDir(tmp_dir);
            std::cerr << "  FAIL: server did not start\n";
            return false;
        }

        auto resp = sendRequest(port, "GET",
                                "/hls/" + stream_name + "/live.m3u8");
        server.stop();

        CHECK(resp.success, "request succeeded at TCP level");
        CHECK(resp.status_code == 200, "existing playlist should return 200");

        const std::string& ct = resp.headers.count("content-type") > 0
                                    ? resp.headers.at("content-type")
                                    : "";
        CHECK(ct.find("mpegurl") != std::string::npos ||
              ct.find("application/vnd") != std::string::npos,
              "playlist content-type should be HLS MIME type");

        CHECK(resp.body.find("#EXTM3U") != std::string::npos,
              "playlist body should contain #EXTM3U");
        CHECK(resp.body.find("segment-000.ts") != std::string::npos,
              "playlist body should contain segment reference");
    }  // ChdirGuard restores CWD here

    cleanupDir(tmp_dir);
    return true;
}

bool test_hls_archive_playlist_served_when_file_exists() {
    std::string tmp_dir = createTempDir();
    if (tmp_dir.empty()) {
        std::cerr << "  FAIL: could not create temp dir\n";
        return false;
    }

    const std::string stream_name = "test-cam-archive";

    // Create the archive playlist file at: <tmpdir>/segments/<stream>/archive.m3u8
    std::string seg_dir = tmp_dir + "/segments/" + stream_name;
    if (!mkdirp(seg_dir)) {
        cleanupDir(tmp_dir);
        std::cerr << "  FAIL: could not create segment dir\n";
        return false;
    }

    const std::string archive_content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:4\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXTINF:4.0,\n"
        "segment-000.ts\n"
        "#EXTINF:4.0,\n"
        "segment-001.ts\n";
    CHECK(writeFile(seg_dir + "/archive.m3u8", archive_content),
          "should create test archive playlist file");

    uint16_t port = findFreePort();
    auto cfg = configWithStream(stream_name, port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    bool started;
    {
        // Chdir to tmp_dir so server finds files at relative path segments/...
        ChdirGuard g(tmp_dir);
        started = server.start() && waitForServerReady(port, 1000);
        if (!started) {
            server.stop();
            cleanupDir(tmp_dir);
            std::cerr << "  FAIL: server did not start\n";
            return false;
        }

        auto resp = sendRequest(port, "GET",
                                "/hls/" + stream_name + "/archive.m3u8");
        server.stop();

        CHECK(resp.success, "request succeeded at TCP level");
        CHECK(resp.status_code == 200, "existing archive playlist should return 200");

        const std::string& ct = resp.headers.count("content-type") > 0
                                    ? resp.headers.at("content-type")
                                    : "";
        CHECK(ct.find("mpegurl") != std::string::npos ||
              ct.find("application/vnd") != std::string::npos,
              "archive playlist content-type should be HLS MIME type");

        CHECK(resp.body.find("#EXTM3U") != std::string::npos,
              "archive playlist body should contain #EXTM3U");
        CHECK(resp.body.find("segment-000.ts") != std::string::npos,
              "archive playlist body should contain segment references");
    }  // ChdirGuard restores CWD here

    cleanupDir(tmp_dir);
    return true;
}

bool test_hls_segment_served_when_file_exists() {
    std::string tmp_dir = createTempDir();
    if (tmp_dir.empty()) {
        std::cerr << "  FAIL: could not create temp dir\n";
        return false;
    }

    const std::string stream_name = "test-cam-segment";

    std::string seg_dir = tmp_dir + "/segments/" + stream_name;
    if (!mkdirp(seg_dir)) {
        cleanupDir(tmp_dir);
        std::cerr << "  FAIL: could not create segment dir\n";
        return false;
    }

    // Write fake binary TS content (just a recognizable byte pattern)
    const std::string ts_content = "\x47\x40\x00\x10some fake ts data";
    CHECK(writeFile(seg_dir + "/segment-000.ts", ts_content),
          "should create test segment file");

    uint16_t port = findFreePort();
    auto cfg = configWithStream(stream_name, port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    {
        ChdirGuard g(tmp_dir);
        CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

        auto resp = sendRequest(port, "GET",
                                "/hls/" + stream_name + "/segment-000.ts");
        server.stop();

        CHECK(resp.success, "request succeeded at TCP level");
        CHECK(resp.status_code == 200, "existing segment should return 200");

        const std::string& ct = resp.headers.count("content-type") > 0
                                    ? resp.headers.at("content-type")
                                    : "";
        CHECK(ct.find("video/mp2t") != std::string::npos ||
              ct.find("video") != std::string::npos,
              "segment content-type should indicate video/mp2t");

        CHECK(resp.body.size() > 0, "segment body should not be empty");
    }

    cleanupDir(tmp_dir);
    return true;
}

bool test_hls_playlist_file_missing_returns_404() {
    uint16_t port = findFreePort();
    // Stream is in config (worker created) but no file on disk
    const std::string stream_name = "no-file-cam";
    auto cfg = configWithStream(stream_name, port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);

    std::string tmp_dir = createTempDir();  // empty dir — no playlist file
    if (tmp_dir.empty()) return false;

    {
        ChdirGuard g(tmp_dir);
        CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

        auto resp = sendRequest(port, "GET",
                                "/hls/" + stream_name + "/live.m3u8");
        server.stop();

        CHECK(resp.success, "request succeeded at TCP level");
        CHECK(resp.status_code == 404,
              "stream exists but missing file should return 404");
    }

    cleanupDir(tmp_dir);
    return true;
}

// ============================================================
// GROUP G: Multi-Request Robustness (1 test)
// ============================================================

bool test_multiple_sequential_requests_all_succeed() {
    uint16_t port = findFreePort();
    auto cfg = emptyConfig(port);
    streamer::StreamManager manager(cfg);

    streamer::HttpServer::ServerConfig sc;
    sc.listen_port = port;
    streamer::HttpServer server(&manager, sc);
    CHECK(server.start() && waitForServerReady(port, 1000), "server must start");

    struct TestCase {
        std::string method;
        std::string path;
        int expected_status;
    };

    const TestCase cases[] = {
        {"GET",  "/api/health",           200},
        {"GET",  "/api/streams",          200},
        {"GET",  "/api/streams/cam-xyz",  404},
        {"GET",  "/hls/cam-xyz/live.m3u8", 404},
        {"POST", "/api/health",           405},
        {"GET",  "/unknown/path",         404},
        {"GET",  "/",                     200},
        {"GET",  "/api/health",           200},  // repeat to confirm no degradation
    };

    for (const auto& tc : cases) {
        auto resp = sendRequest(port, tc.method, tc.path);
        CHECK(resp.success,
              "request to " + tc.path + " should succeed at TCP level");
        CHECK(resp.status_code == tc.expected_status,
              tc.method + " " + tc.path + " expected " +
              std::to_string(tc.expected_status) + " got " +
              std::to_string(resp.status_code));
    }

    server.stop();
    return true;
}

// ============================================================
// SECTION 7: Test Runner
// ============================================================

struct TestCase {
    const char* name;
    bool (*fn)();
};

static void runTest(const char* name, bool (*fn)()) {
    bool result = fn();
    if (result) {
        std::cout << "✓ " << name << " PASSED\n";
        ++g_pass;
    } else {
        std::cerr << "✗ " << name << " FAILED\n";
        ++g_fail;
    }
}

int main() {
    // Suppress FFmpeg verbose output so test results are readable
    av_log_set_level(AV_LOG_QUIET);

    // Disable all stdout/stderr buffering so each line is flushed immediately.
    // This ensures progress is visible even if the process is killed.
    std::cout.setf(std::ios::unitbuf);
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffer C stdout (for LOG_INFO fprintf)
    setvbuf(stderr, nullptr, _IONBF, 0);  // unbuffer C stderr (for LOG_ERROR fprintf)

    std::cout << "\n=== HTTP Server Unit & Integration Tests ===\n\n";
    std::cout << "--- Group A: Server Lifecycle ---\n";
    runTest("test_server_starts_and_stops",            test_server_starts_and_stops);
    runTest("test_server_double_start_is_idempotent",  test_server_double_start_is_idempotent);
    runTest("test_server_bind_failure_on_taken_port",  test_server_bind_failure_on_taken_port);

    std::cout << "\n--- Group B: HTTP Routing (Status Codes) ---\n";
    runTest("test_health_endpoint_returns_200",             test_health_endpoint_returns_200);
    runTest("test_streams_list_endpoint_returns_200",       test_streams_list_endpoint_returns_200);
    runTest("test_stream_by_name_not_found_returns_404",    test_stream_by_name_not_found_returns_404);
    runTest("test_hls_playlist_unknown_stream_returns_404", test_hls_playlist_unknown_stream_returns_404);
    runTest("test_hls_segment_unknown_stream_returns_404",  test_hls_segment_unknown_stream_returns_404);
    runTest("test_index_route_returns_200",                 test_index_route_returns_200);
    runTest("test_unknown_route_returns_404",               test_unknown_route_returns_404);

    std::cout << "\n--- Group C: HTTP Method Handling ---\n";
    runTest("test_post_method_returns_405", test_post_method_returns_405);
    runTest("test_head_method_returns_200", test_head_method_returns_200);

    std::cout << "\n--- Group D: Response Headers ---\n";
    runTest("test_cors_headers_present_when_enabled", test_cors_headers_present_when_enabled);
    runTest("test_cors_headers_absent_when_disabled", test_cors_headers_absent_when_disabled);
    runTest("test_content_type_json_for_api_routes",  test_content_type_json_for_api_routes);

    std::cout << "\n--- Group E: JSON Response Validation ---\n";
    runTest("test_health_json_has_required_fields",         test_health_json_has_required_fields);
    runTest("test_streams_list_json_has_required_fields",   test_streams_list_json_has_required_fields);
    runTest("test_health_json_empty_manager_has_zero_counts", test_health_json_empty_manager_has_zero_counts);

    std::cout << "\n--- Group F: File Serving ---\n";
    runTest("test_hls_playlist_served_when_file_exists",    test_hls_playlist_served_when_file_exists);
    runTest("test_hls_archive_playlist_served_when_file_exists", test_hls_archive_playlist_served_when_file_exists);
    runTest("test_hls_segment_served_when_file_exists",     test_hls_segment_served_when_file_exists);
    runTest("test_hls_playlist_file_missing_returns_404",   test_hls_playlist_file_missing_returns_404);

    std::cout << "\n--- Group G: Multi-Request Robustness ---\n";
    runTest("test_multiple_sequential_requests_all_succeed", test_multiple_sequential_requests_all_succeed);

    std::cout << "\n";
    std::cout << "=== Results: " << g_pass << " passed, " << g_fail << " failed ===\n";

    if (g_fail == 0) {
        std::cout << "\n✓ All HTTP Server Tests PASSED\n";
        return 0;
    } else {
        std::cerr << "\n✗ " << g_fail << " HTTP Server Test(s) FAILED\n";
        return 1;
    }
}
