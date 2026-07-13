#pragma once

#include <functional>
#include <map>
#include <string>

namespace cloud_disk {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(std::string host, int port);
    void route(const std::string& method, const std::string& path, Handler handler);
    void run();

private:
    HttpResponse handle(const HttpRequest& request) const;

    std::string host_;
    int port_ = 8080;
    std::map<std::string, Handler> handlers_;
};

std::string getHeader(const HttpRequest& request, const std::string& name);
std::string getQuery(const HttpRequest& request, const std::string& name, const std::string& fallback = "");

} // namespace cloud_disk

