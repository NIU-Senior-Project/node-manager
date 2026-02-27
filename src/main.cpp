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
        } else {
            response = makeHttpResponse(404, "Not Found", "Route not found\n");
        }

        send(clientFd, response.data(), response.size(), 0);
        close(clientFd);
    }
    
    return 0;
}
