#include "WebsocketServer.h"
#include <iostream>

WebsocketServer::WebsocketServer(int port) : port_(port), running_(false) {}

WebsocketServer::~WebsocketServer() {
    stop();
}

bool WebsocketServer::start() {
    if (running_) return true;
    running_ = true;
    serverThread_ = std::thread(&WebsocketServer::run, this);
    std::cout << "[WS] Server started on port " << port_ << std::endl;
    return true;
}

void WebsocketServer::stop() {
    running_ = false;
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void WebsocketServer::broadcastStatus(const std::string& status, float fps) {
    if (!running_) return;
    // Format JSON and send to all clients
    // std::string json = "{\"type\": \"status\", \"value\": \"" + status + "\", \"fps\": " + std::to_string(fps) + "}";
    // std::cout << "[WS] Broadcasting: " << json << std::endl;
}

void WebsocketServer::broadcastDefect(int cameraId, const std::string& timestamp) {
    if (!running_) return;
    // Send defect alert to web dashboard
    std::cout << "[WS] ALERT: Defect on Camera " << cameraId << " at " << timestamp << std::endl;
}

void WebsocketServer::run() {
    while (running_) {
        // Accept and handle websocket connections
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
