#pragma once

#include "streamer/Logger.h"
#include "streamer/StreamManager.h"

#include <atomic>
#include <string>
#include <thread>

namespace streamer {

// HTTP Server for serving HLS playlists/segments and REST API endpoints
// Provides per-stream HLS routing and aggregate health metrics
class HttpServer {
   public:
    struct ServerConfig {
        uint16_t listen_port = 8000;
        std::string listen_address = "0.0.0.0";
        int max_concurrent_connections = 100;
        bool enable_cors = true;
        std::string database_path = "/app/detections.db";  // SQLite DB for AI detections
    };

    // Constructor: Inject StreamManager reference
    // manager lifetime must exceed server lifetime
    explicit HttpServer(StreamManager* manager);

    // Constructor with custom config
    explicit HttpServer(StreamManager* manager, const ServerConfig& config);

    // Destructor: Gracefully stops server if running
    ~HttpServer();

    // Start listening on configured port and address
    // Spawns background thread for request handling
    // Returns true if bind/listen succeeds
    bool start();

    // Stop server gracefully
    // Closes listening socket, waits for connections to drain
    void stop();

    // Check if server is currently running
    bool isRunning() const { return running_.load(); }

    // Get last error message (if start() failed)
    std::string getLastError() const { return lastError_; }

   private:
    // Main listener loop (runs in background thread)
    void listenerLoop();

    // Handle single HTTP connection
    void handleConnection(int client_socket);

    // Route HTTP request to appropriate handler
    // Returns HTTP response as complete string (status + headers + body)
    std::string routeRequest(const std::string& method, const std::string& path);

    // Handler methods for different endpoints
    std::string handleGetPlaylist(const std::string& stream_name, const std::string& playlist_name);
    std::string handleGetSegment(const std::string& stream_name, const std::string& segment_name);
    std::string handleGetStreamStatus(const std::string& stream_name);
    std::string handleGetStreamsList();
    std::string handleGetAggregateHealth();
    std::string handleGetPlaybackConfig();
    std::string handleGetIndex();
    std::string handleNotFound();
    
    // Detection endpoints (integrate with Python AI inference module)
    std::string handleDetectionStats(const std::string& path);
    std::string handleDetectionRecent(const std::string& path);
    std::string handleDetectionFrame(const std::string& det_id_str);

    // Utility: Parse HTTP request line "GET /path HTTP/1.1"
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string version;
    };
    HttpRequest parseRequestLine(const std::string& line);

    // Utility: Read HTTP request until blank line, return first line
    std::string readHttpRequest(int socket);

    // Utility: Send HTTP response via socket
    bool sendResponse(int socket, const std::string& response);

    // Utility: Read entire file from disk
    std::string readFileContent(const std::string& file_path);

    // Utility: Check if file exists
    bool fileExists(const std::string& file_path);

    // Utility: Escape special characters in JSON strings
    std::string jsonEscape(const std::string& input);

    // Database query helpers for detection endpoints
    std::string queryDetectionStats(const std::string& stream_id = "", const std::string& object_type = "");
    std::string queryRecentDetections(int limit, const std::string& stream_id = "");
    std::string queryDetectionFrame(int det_id);

    // Members
    StreamManager* manager_;
    ServerConfig config_;
    std::atomic<bool> running_{false};
    std::thread listener_thread_;
    int listening_socket_ = -1;
    mutable std::string lastError_;
    // SQLite database connection (lazy-initialized)
    void* db_connection_ = nullptr;  // sqlite3* (void* to avoid sqlite3.h in header)
};

}  // namespace streamer
