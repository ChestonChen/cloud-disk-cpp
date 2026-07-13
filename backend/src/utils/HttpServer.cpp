#include "utils/HttpServer.h"

#include "utils/Json.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace cloud_disk {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            int hi = hexValue(value[i + 1]);
            int lo = hexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string& queryText) {
    std::map<std::string, std::string> result;
    std::istringstream in(queryText);
    std::string item;
    while (std::getline(in, item, '&')) {
        auto eq = item.find('=');
        if (eq == std::string::npos) {
            result[urlDecode(item)] = "";
        } else {
            result[urlDecode(item.substr(0, eq))] = urlDecode(item.substr(eq + 1));
        }
    }
    return result;
}

HttpRequest parseRequest(const std::string& raw) {
    HttpRequest request;
    auto headerEnd = raw.find("\r\n\r\n");
    std::string headerText = headerEnd == std::string::npos ? raw : raw.substr(0, headerEnd);
    request.body = headerEnd == std::string::npos ? "" : raw.substr(headerEnd + 4);

    std::istringstream headers(headerText);
    std::string line;
    if (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream start(line);
        std::string target;
        start >> request.method >> target;
        auto q = target.find('?');
        request.path = q == std::string::npos ? target : target.substr(0, q);
        if (q != std::string::npos) {
            request.query = parseQuery(target.substr(q + 1));
        }
    }
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            request.headers[lower(line.substr(0, colon))] = trim(line.substr(colon + 1));
        }
    }
    return request;
}

std::string buildResponse(const HttpResponse& response) {
    std::ostringstream out;
    std::string reason = response.status == 200 ? "OK"
                         : response.status == 201 ? "Created"
                         : response.status == 400 ? "Bad Request"
                         : response.status == 401 ? "Unauthorized"
                         : response.status == 404 ? "Not Found"
                                                  : "Internal Server Error";
    out << "HTTP/1.1 " << response.status << " " << reason << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Content-Type: " << response.contentType << "\r\n";
    out << "Connection: close\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
    out << "Access-Control-Expose-Headers: Content-Disposition\r\n";
    for (const auto& [key, value] : response.headers) {
        out << key << ": " << value << "\r\n";
    }
    out << "\r\n" << response.body;
    return out.str();
}

} // namespace

HttpServer::HttpServer(std::string host, int port) : host_(std::move(host)), port_(port) {}

void HttpServer::route(const std::string& method, const std::string& path, Handler handler) {
    handlers_[method + " " + path] = std::move(handler);
}

void HttpServer::run() {
    int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        throw std::runtime_error("socket failed");
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) <= 0) {
        ::close(serverFd);
        throw std::runtime_error("invalid listen host");
    }
    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(serverFd);
        throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
    }
    if (::listen(serverFd, 64) < 0) {
        ::close(serverFd);
        throw std::runtime_error("listen failed");
    }

    std::cout << "Cloud Disk backend listening on " << host_ << ":" << port_ << std::endl;
    while (true) {
        int client = ::accept(serverFd, nullptr, nullptr);
        if (client < 0) {
            continue;
        }

        std::string raw;
        char buffer[8192];
        ssize_t n = 0;
        while ((n = ::recv(client, buffer, sizeof(buffer), 0)) > 0) {
            raw.append(buffer, static_cast<std::size_t>(n));
            auto headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                auto req = parseRequest(raw);
                std::size_t contentLength = 0;
                auto it = req.headers.find("content-length");
                if (it != req.headers.end()) {
                    contentLength = static_cast<std::size_t>(std::stoull(it->second));
                }
                if (req.body.size() >= contentLength) {
                    break;
                }
            }
        }

        HttpResponse response;
        try {
            response = handle(parseRequest(raw));
        } catch (const std::exception& ex) {
            response.status = 500;
            response.body = errorJson(500, ex.what());
        }
        auto text = buildResponse(response);
        ::send(client, text.data(), text.size(), 0);
        ::close(client);
    }
}

HttpResponse HttpServer::handle(const HttpRequest& request) const {
    if (request.method == "OPTIONS") {
        return HttpResponse {200, "application/json", {}, okJson(jsonObject({{"preflight", "ok"}}))};
    }
    auto it = handlers_.find(request.method + " " + request.path);
    if (it == handlers_.end()) {
        return HttpResponse {404, "application/json", {}, errorJson(404, "route not found")};
    }
    return it->second(request);
}

std::string getHeader(const HttpRequest& request, const std::string& name) {
    auto it = request.headers.find(lower(name));
    return it == request.headers.end() ? "" : it->second;
}

std::string getQuery(const HttpRequest& request, const std::string& name, const std::string& fallback) {
    auto it = request.query.find(name);
    return it == request.query.end() ? fallback : it->second;
}

} // namespace cloud_disk

