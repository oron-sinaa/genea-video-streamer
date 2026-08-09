#include "streamer/HttpServer.h"

#include "streamer/Logger.h"
#include "streamer/StreamManager.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace streamer {

namespace {

const int BACKLOG = 5;
const int RECV_BUFFER_SIZE = 4096;
const int FILE_CHUNK_SIZE = 4096;

// Generate standard HTTP response header
std::string generateHttpHeader(int status_code, const std::string& content_type, size_t content_length, bool enable_cors) {
    std::string status_text;
    switch (status_code) {
        case 200:
            status_text = "OK";
            break;
        case 206:
            status_text = "Partial Content";
            break;
        case 304:
            status_text = "Not Modified";
            break;
        case 400:
            status_text = "Bad Request";
            break;
        case 404:
            status_text = "Not Found";
            break;
        case 500:
            status_text = "Internal Server Error";
            break;
        default:
            status_text = "Unknown";
    }

    std::ostringstream header;
    header << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    header << "Content-Type: " << content_type << "\r\n";
    header << "Content-Length: " << content_length << "\r\n";
    header << "Connection: close\r\n";
    
    if (enable_cors) {
        header << "Access-Control-Allow-Origin: *\r\n";
        header << "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n";
    }
    
    header << "\r\n";
    return header.str();
}

}  // namespace

HttpServer::HttpServer(StreamManager* manager, const ServerConfig& config)
    : manager_(manager), config_(config) {
    if (!manager_) {
        LOG_ERROR("HttpServer: StreamManager pointer is null");
    }
}

HttpServer::HttpServer(StreamManager* manager)
    : manager_(manager) {
    if (!manager_) {
        LOG_ERROR("HttpServer: StreamManager pointer is null");
    }
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_.load()) {
        LOG_WARN("HttpServer: Already running");
        return true;
    }

    // Create listening socket
    listening_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket_ < 0) {
        lastError_ = std::string("Failed to create socket: ") + strerror(errno);
        LOG_ERROR("HttpServer: %s", lastError_.c_str());
        return false;
    }

    // Allow port reuse
    int reuse = 1;
    if (setsockopt(listening_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        lastError_ = std::string("Failed to set SO_REUSEADDR: ") + strerror(errno);
        LOG_ERROR("HttpServer: %s", lastError_.c_str());
        close(listening_socket_);
        listening_socket_ = -1;
        return false;
    }

    // Bind to address and port
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.listen_port);
    
    if (inet_pton(AF_INET, config_.listen_address.c_str(), &server_addr.sin_addr) <= 0) {
        lastError_ = std::string("Invalid listen address: ") + config_.listen_address;
        LOG_ERROR("HttpServer: %s", lastError_.c_str());
        close(listening_socket_);
        listening_socket_ = -1;
        return false;
    }

    if (bind(listening_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        lastError_ = std::string("Failed to bind socket: ") + strerror(errno);
        LOG_ERROR("HttpServer: %s", lastError_.c_str());
        close(listening_socket_);
        listening_socket_ = -1;
        return false;
    }

    // Listen for incoming connections
    if (listen(listening_socket_, BACKLOG) < 0) {
        lastError_ = std::string("Failed to listen: ") + strerror(errno);
        LOG_ERROR("HttpServer: %s", lastError_.c_str());
        close(listening_socket_);
        listening_socket_ = -1;
        return false;
    }

    // Give accept() a short timeout so that stop() can reliably interrupt the
    // listener thread by setting running_=false and waiting.  On Linux,
    // close()ing the fd from another thread does NOT reliably wake up a
    // blocked accept(), so we poll every 200 ms instead.
    struct timeval accept_tv{0, 200000};  // 200 ms
    setsockopt(listening_socket_, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv));

    // Start listener thread
    running_.store(true);
    listener_thread_ = std::thread(&HttpServer::listenerLoop, this);
    LOG_INFO("HttpServer: Started on %s:%u", config_.listen_address.c_str(), config_.listen_port);
    return true;
}

void HttpServer::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Wait for listener thread to exit.  The listener polls accept() with a
    // 200 ms SO_RCVTIMEO, so it will notice running_=false within 200 ms.
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }

    // Free the port only after the thread has exited
    if (listening_socket_ >= 0) {
        close(listening_socket_);
        listening_socket_ = -1;
    }

    LOG_INFO("HttpServer: Stopped");
}

void HttpServer::listenerLoop() {
    LOG_INFO("HttpServer: Listener loop started");

    while (running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_socket = accept(listening_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // accept() timed out (200 ms SO_RCVTIMEO); loop back and re-check running_
                continue;
            }
            if (running_.load()) {
                LOG_INFO("HttpServer: accept() failed: %s", strerror(errno));
            }
            continue;
        }

        // Set a receive timeout on the accepted socket so that a slow or
        // closed client cannot stall the server indefinitely.
        struct timeval client_tv{0, 500000};  // 500ms read timeout per request
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO,
                   &client_tv, sizeof(client_tv));

        // Handle connection (synchronous, one at a time)
        handleConnection(client_socket);
        close(client_socket);
    }

    LOG_INFO("HttpServer: Listener loop ended");
}

void HttpServer::handleConnection(int client_socket) {
    std::string request_line = readHttpRequest(client_socket);
    if (request_line.empty()) {
        return;
    }

    HttpRequest request = parseRequestLine(request_line);
    if (request.method.empty() || request.path.empty()) {
        LOG_INFO("HttpServer: Failed to parse request line");
        return;
    }

    LOG_INFO("HttpServer: %s %s", request.method.c_str(), request.path.c_str());

    std::string response = routeRequest(request.method, request.path);
    sendResponse(client_socket, response);
}

std::string HttpServer::readHttpRequest(int socket) {
    char buffer[RECV_BUFFER_SIZE];
    std::memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return "";
    }

    std::string raw_request(buffer);
    size_t line_end = raw_request.find("\r\n");
    if (line_end == std::string::npos) {
        line_end = raw_request.find("\n");
    }

    if (line_end == std::string::npos) {
        return "";
    }

    return raw_request.substr(0, line_end);
}

HttpServer::HttpRequest HttpServer::parseRequestLine(const std::string& line) {
    HttpRequest request;
    std::istringstream iss(line);

    iss >> request.method >> request.path >> request.version;

    // Convert method to uppercase
    std::transform(request.method.begin(), request.method.end(), request.method.begin(), ::toupper);

    return request;
}

bool HttpServer::sendResponse(int socket, const std::string& response) {
    size_t total_sent = 0;
    size_t response_size = response.size();

    while (total_sent < response_size) {
        ssize_t sent = send(socket, response.c_str() + total_sent, response_size - total_sent, 0);
        if (sent < 0) {
            LOG_INFO("HttpServer: Failed to send response: %s", strerror(errno));
            return false;
        }
        total_sent += sent;
    }

    return true;
}

std::string HttpServer::routeRequest(const std::string& method, const std::string& path) {
    if (method != "GET" && method != "HEAD") {
        // Return 405 Method Not Allowed
        std::string body = "Method not allowed";
        std::string header = generateHttpHeader(405, "text/plain", body.size(), config_.enable_cors);
        return header + body;
    }

    // Route /api/health
    if (path == "/api/health") {
        return handleGetAggregateHealth();
    }

    // Route /api/streams
    if (path == "/api/streams") {
        return handleGetStreamsList();
    }

    // Route /api/streams/<stream-name>
    if (path.find("/api/streams/") == 0) {
        std::string stream_name = path.substr(13);  // Skip "/api/streams/"
        if (stream_name.empty()) {
            return handleNotFound();
        }
        return handleGetStreamStatus(stream_name);
    }

    // Route /hls/<stream-name>/live.m3u8
    if (path.find("/hls/") == 0) {
        size_t second_slash = path.find('/', 5);  // Find slash after /hls/
        if (second_slash == std::string::npos) {
            return handleNotFound();
        }

        std::string stream_name = path.substr(5, second_slash - 5);  // Extract stream name
        std::string resource = path.substr(second_slash + 1);        // Extract resource

        if (resource.find("live.m3u8") != std::string::npos) {
            return handleGetPlaylist(stream_name);
        } else if (resource.find(".ts") != std::string::npos) {
            return handleGetSegment(stream_name, resource);
        }
    }

    // Route / or /index.html
    if (path == "/" || path == "/index.html") {
        return handleGetIndex();
    }

    return handleNotFound();
}

std::string HttpServer::handleGetPlaylist(const std::string& stream_name) {
    if (!manager_) {
        return handleNotFound();
    }

    StreamWorker* worker = manager_->getStream(stream_name);
    if (!worker) {
        LOG_INFO("HttpServer: Stream not found: %s", stream_name.c_str());
        return handleNotFound();
    }

    // Construct path to playlist file
    // Assuming HLS output dir is stored in worker or accessible via config
    // For now, use pattern: segments/<stream-name>/live.m3u8
    std::string playlist_path = "segments/" + stream_name + "/live.m3u8";

    std::string content = readFileContent(playlist_path);
    if (content.empty() && !fileExists(playlist_path)) {
        LOG_INFO("HttpServer: Playlist not found: %s", playlist_path.c_str());
        return handleNotFound();
    }

    std::string header = generateHttpHeader(200, "application/vnd.apple.mpegurl", content.size(), config_.enable_cors);
    return header + content;
}

std::string HttpServer::handleGetSegment(const std::string& stream_name, const std::string& segment_name) {
    if (!manager_) {
        return handleNotFound();
    }

    StreamWorker* worker = manager_->getStream(stream_name);
    if (!worker) {
        return handleNotFound();
    }

    // Construct path to segment file
    std::string segment_path = "segments/" + stream_name + "/" + segment_name;

    std::string content = readFileContent(segment_path);
    if (content.empty() && !fileExists(segment_path)) {
        LOG_INFO("HttpServer: Segment not found: %s", segment_path.c_str());
        return handleNotFound();
    }

    std::string header = generateHttpHeader(200, "video/mp2t", content.size(), config_.enable_cors);
    return header + content;
}

std::string HttpServer::handleGetAggregateHealth() {
    if (!manager_) {
        return handleNotFound();
    }

    auto health = manager_->getAggregateHealth();

    std::ostringstream json;
    json << "{\n";
    json << "  \"total_packets_read\": " << health.total_packets_read << ",\n";
    json << "  \"total_packets_written\": " << health.total_packets_written << ",\n";
    json << "  \"total_packets_dropped\": " << health.total_packets_dropped << ",\n";
    json << "  \"total_reconnects\": " << health.total_reconnects << ",\n";
    json << "  \"total_throughput_mbps\": " << health.total_throughput_mbps << ",\n";
    json << "  \"active_streams\": " << health.active_streams << ",\n";
    json << "  \"error_streams\": " << health.error_streams << ",\n";
    json << "  \"stream_stats\": [\n";

    for (size_t i = 0; i < health.stream_stats.size(); ++i) {
        const auto& stat = health.stream_stats[i];
        json << "    {\n";
        json << "      \"name\": \"" << jsonEscape(stat.name) << "\",\n";
        json << "      \"status\": \"" << static_cast<int>(stat.status) << "\",\n";
        json << "      \"packets_written\": " << stat.packets_written << ",\n";
        json << "      \"throughput_mbps\": " << stat.throughput_mbps << ",\n";
        json << "      \"reconnects\": " << stat.reconnects << "\n";
        json << "    }";
        if (i < health.stream_stats.size() - 1) {
            json << ",";
        }
        json << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    std::string body = json.str();
    std::string header = generateHttpHeader(200, "application/json", body.size(), config_.enable_cors);
    return header + body;
}

std::string HttpServer::handleGetStreamsList() {
    if (!manager_) {
        return handleNotFound();
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"total_streams\": " << manager_->getStreamCount() << ",\n";
    json << "  \"streams\": [\n";

    size_t count = manager_->getStreamCount();
    auto health = manager_->getAggregateHealth();

    for (size_t i = 0; i < health.stream_stats.size(); ++i) {
        const auto& stat = health.stream_stats[i];
        json << "    {\n";
        json << "      \"name\": \"" << jsonEscape(stat.name) << "\",\n";
        json << "      \"status\": \"" << static_cast<int>(stat.status) << "\",\n";
        json << "      \"packets_written\": " << stat.packets_written << ",\n";
        json << "      \"reconnects\": " << stat.reconnects << "\n";
        json << "    }";
        if (i < health.stream_stats.size() - 1) {
            json << ",";
        }
        json << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    std::string body = json.str();
    std::string header = generateHttpHeader(200, "application/json", body.size(), config_.enable_cors);
    return header + body;
}

std::string HttpServer::handleGetStreamStatus(const std::string& stream_name) {
    if (!manager_) {
        return handleNotFound();
    }

    StreamWorker* worker = manager_->getStream(stream_name);
    if (!worker) {
        return handleNotFound();
    }

    auto health = manager_->getAggregateHealth();
    
    // Find this stream's stats
    for (const auto& stat : health.stream_stats) {
        if (stat.name == stream_name) {
            std::ostringstream json;
            json << "{\n";
            json << "  \"name\": \"" << jsonEscape(stat.name) << "\",\n";
            json << "  \"status\": \"" << static_cast<int>(stat.status) << "\",\n";
            json << "  \"packets_written\": " << stat.packets_written << ",\n";
            json << "  \"throughput_mbps\": " << stat.throughput_mbps << ",\n";
            json << "  \"reconnects\": " << stat.reconnects << "\n";
            json << "}\n";

            std::string body = json.str();
            std::string header = generateHttpHeader(200, "application/json", body.size(), config_.enable_cors);
            return header + body;
        }
    }

    return handleNotFound();
}

std::string HttpServer::handleGetIndex() {
    std::string content = readFileContent("web/player.html");
    if (content.empty()) {
        // Return a simple HTML page if player.html not found
        content = "<html><body><h1>Genea Video Streamer</h1><p>Player not available</p></body></html>";
    }

    std::string header = generateHttpHeader(200, "text/html", content.size(), config_.enable_cors);
    return header + content;
}

std::string HttpServer::handleNotFound() {
    std::string body = "404 Not Found";
    std::string header = generateHttpHeader(404, "text/plain", body.size(), config_.enable_cors);
    return header + body;
}

std::string HttpServer::readFileContent(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream content;
    char buffer[FILE_CHUNK_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        content.write(buffer, file.gcount());
    }

    return content.str();
}

bool HttpServer::fileExists(const std::string& file_path) {
    std::ifstream file(file_path);
    return file.good();
}

std::string HttpServer::jsonEscape(const std::string& input) {
    std::string output;
    for (char c : input) {
        switch (c) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += c;
        }
    }
    return output;
}

}  // namespace streamer
