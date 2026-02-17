#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

/**
 * @brief A simple WebSocket server to bridge C++ events to the Web Dashboard.
 * 
 * Note: In a real implementation, this would use a library like uWebSockets, 
 * Beast, or CivetWeb. For this example, we provide the stub structure.
 */
class WebsocketServer {
public:
    WebsocketServer(int port = 9000);
    ~WebsocketServer();

    bool start();
    void stop();

    // Broadcast system status to all connected web clients
    void broadcastStatus(const std::string& status, float fps);

    // Broadcast defect detection events
    void broadcastDefect(int cameraId, const std::string& timestamp);

private:
    void run();

    int port_;
    std::atomic<bool> running_;
    std::thread serverThread_;
    std::mutex clientsMutex_;
    // std::vector<ClientHandle> clients_;
};
