#include <iostream>
#include <sstream>
#include <set>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "node.hpp"

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string extractIpFromBody(const std::string& body) {
    const auto keyPos = body.find("ip=");
    if (keyPos != std::string::npos) {
        const auto start = keyPos + 3;
        const auto end = body.find('&', start);
        return trim(body.substr(start, end == std::string::npos ? std::string::npos : end - start));
    }

    const auto jsonKeyPos = body.find("\"ip\"");
    if (jsonKeyPos == std::string::npos) {
        return "";
    }

    const auto colonPos = body.find(':', jsonKeyPos);
    if (colonPos == std::string::npos) {
        return "";
    }

    const auto firstQuote = body.find('"', colonPos + 1);
    if (firstQuote == std::string::npos) {
        return "";
    }

    const auto secondQuote = body.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) {
        return "";
    }

    return trim(body.substr(firstQuote + 1, secondQuote - firstQuote - 1));
}

std::string makeHttpResponse(int code, const std::string& status, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << code << ' ' << status << "\r\n";
    response << "Content-Type: text/plain; charset=utf-8\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    return response.str();
}

// 提取 JSON 字串欄位（簡易版，支援跳脫字元檢查）
std::string extractJsonStringField(const std::string& body, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    auto keyPos = body.find(searchKey);
    if (keyPos == std::string::npos) return "";

    auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    auto firstQuote = body.find('"', colonPos + 1);
    if (firstQuote == std::string::npos) return "";

    auto secondQuote = body.find('"', firstQuote + 1);
    // 處理跳脫的雙引號 (例如 script 裡面有 \")
    while (secondQuote != std::string::npos && body[secondQuote - 1] == '\\') {
        secondQuote = body.find('"', secondQuote + 1);
    }

    if (secondQuote == std::string::npos) return "";
    return body.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

// Manager 轉發請求給目標 Node (假設 Node 端監聽 8081 Port 接收任務)
std::string forwardJobToNode(const std::string& targetIp, const std::string& payload) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    sockaddr_in nodeAddr{};
    nodeAddr.sin_family = AF_INET;
    // 這裡我們假設 GPU Node 接收任務的 Server 開在 8081 port
    nodeAddr.sin_port = htons(8081); 

    if (inet_pton(AF_INET, targetIp.c_str(), &nodeAddr.sin_addr) <= 0) {
        close(sock);
        return "";
    }

    // 設定 Timeout 避免目標 Node 死機導致 Manager 卡住 (設定 5 秒)
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    if (connect(sock, reinterpret_cast<sockaddr*>(&nodeAddr), sizeof(nodeAddr)) < 0) {
        close(sock);
        return "";
    }

    // 將收到的 JSON 直接打包成新的 HTTP 請求發給 Node
    std::ostringstream request;
    request << "POST /execute HTTP/1.1\r\n"
            << "Host: " << targetIp << ":8081\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << payload.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << payload;

    std::string reqStr = request.str();
    if (send(sock, reqStr.data(), reqStr.size(), 0) != static_cast<ssize_t>(reqStr.size())) {
        close(sock);
        return "";
    }

    // 讀取 Node 執行的回覆
    std::string respStr;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        respStr.append(buffer);
    }
    close(sock);

    // 回傳 Node Response 的 Body
    auto headerEnd = respStr.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        return respStr.substr(headerEnd + 4);
    }
    return respStr;
}

} // namespace

int main(int argc, char* argv[]) {
    std::set<GPUNode> gpuNodes;

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Failed to bind on port 8080\n";
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 16) < 0) {
        std::cerr << "Failed to listen\n";
        close(serverFd);
        return 1;
    }

    std::cout << "Node manager listening on port 8080\n";

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLen = sizeof(clientAddress);
        int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLen);
        if (clientFd < 0) {
            continue;
        }

        std::string request;
        char buffer[4096];
        ssize_t bytesRead = 0;

        while ((bytesRead = recv(clientFd, buffer, sizeof(buffer), 0)) > 0) {
            request.append(buffer, bytesRead);
            if (request.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }

        std::string response;
        const auto headerEnd = request.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            response = makeHttpResponse(400, "Bad Request", "Invalid HTTP request\n");
            send(clientFd, response.data(), response.size(), 0);
            close(clientFd);
            continue;
        }

        std::string headerPart = request.substr(0, headerEnd);
        std::string body = request.substr(headerEnd + 4);

        std::istringstream stream(headerPart);
        std::string method;
        std::string path;
        std::string version;
        stream >> method >> path >> version;

        std::size_t contentLength = 0;
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            constexpr const char* contentLengthKey = "Content-Length:";
            if (line.rfind(contentLengthKey, 0) == 0) {
                contentLength = static_cast<std::size_t>(std::stoul(trim(line.substr(15))));
            }
        }

        while (body.size() < contentLength && (bytesRead = recv(clientFd, buffer, sizeof(buffer), 0)) > 0) {
            body.append(buffer, bytesRead);
        }

        if (method == "POST" && path == "/register") {
            const std::string ip = extractIpFromBody(body);
            if (ip.empty()) {
                response = makeHttpResponse(400, "Bad Request", "Missing ip\n");
                std::cout << "[WARNING] Failed registration attempt: Missing IP\n";
            } else {
                gpuNodes.insert(GPUNode(ip));
                response = makeHttpResponse(200, "OK", "Registered: " + ip + "\n");
                std::cout << "[INFO] New Node registered successfully. IP: " << ip << "\n";
            }
        } else if (method == "GET" && path == "/nodes") {
            std::ostringstream jsonArray;
            jsonArray << "[\n";
            bool first = true;
            for (const auto& node : gpuNodes) {
                if (!first) {
                    jsonArray << ",\n";
                }
                jsonArray << "  \"" << node.getIpAddress() << "\"";
                first = false;
            }
            jsonArray << "\n]";

            response = makeHttpResponse(200, "OK", jsonArray.str());
            std::cout << "[INFO] Node list requested. Total online nodes: " << gpuNodes.size() << "\n";
        } else if (method == "POST" && path == "/submit_job") {
            std::string targetNode = extractJsonStringField(body, "target_node");
            std::string script = extractJsonStringField(body, "script");

            if (targetNode.empty() || script.empty()) {
                response = makeHttpResponse(400, "Bad Request", "Missing target_node or script\n");
                std::cout << "[ERROR] Job submission failed: Missing parameters\n";
            } else {
                std::cout << "[INFO] Forwarding job to Node IP: " << targetNode << "\n";

                // 執行轉發
                std::string nodeResponse = forwardJobToNode(targetNode, body);

                if (nodeResponse.empty()) {
                    response = makeHttpResponse(502, "Bad Gateway", "Failed to reach target node " + targetNode + "\n");
                    std::cout << "[ERROR] Failed to forward job. Node " << targetNode << " is unreachable.\n";
                } else {
                    // 成功轉發，把 Node 的回覆包裝後傳回給送出請求的 Client
                    response = makeHttpResponse(200, "OK", "Job Forwarded. Node replied:\n" + nodeResponse);
                    std::cout << "[INFO] Job successfully forwarded and acknowledged by Node " << targetNode << "\n";
                }
            }
        }
        else {
            response = makeHttpResponse(404, "Not Found", "Route not found\n");
        }

        send(clientFd, response.data(), response.size(), 0);
        close(clientFd);
    }

    return 0;
}
