#include "prod/RedisSessionStore.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace cloud_disk {

// 保存 Redis 连接配置和会话过期时间。
RedisSessionStore::RedisSessionStore(std::string host, int port, int ttlSeconds)
    : host_(std::move(host)), port_(port), ttlSeconds_(ttlSeconds) {}

// 把登录 token 写入 Redis，并设置自动过期时间。
void RedisSessionStore::save(const std::string& token, std::int64_t userId) const {
    command({"SETEX", key(token), std::to_string(ttlSeconds_), std::to_string(userId)});
}

// 根据 token 查询用户 id，用于接口鉴权。
std::optional<std::int64_t> RedisSessionStore::findUserId(const std::string& token) const {
    auto value = command({"GET", key(token)});
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return std::stoll(*value);
}

// 生成统一前缀的 Redis key。
std::string RedisSessionStore::key(const std::string& token) {
    return "cloud_disk:session:" + token;
}

// 建立短连接发送 Redis 命令，并解析当前项目需要的 RESP 响应类型。
std::optional<std::string> RedisSessionStore::command(const std::vector<std::string>& parts) const {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("redis socket failed");
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) <= 0
        || ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd);
        throw std::runtime_error("redis connect failed");
    }

    std::ostringstream request;
    request << "*" << parts.size() << "\r\n";
    for (const auto& part : parts) {
        request << "$" << part.size() << "\r\n" << part << "\r\n";
    }
    std::string wire = request.str();
    ::send(fd, wire.data(), wire.size(), 0);

    auto line = readLine(fd);
    if (line.empty()) {
        ::close(fd);
        throw std::runtime_error("redis empty response");
    }

    char type = line[0];
    std::optional<std::string> result;
    if (type == '+') {
        result = line.substr(1);
    } else if (type == '$') {
        int size = std::stoi(line.substr(1));
        if (size >= 0) {
            result = readBytes(fd, static_cast<std::size_t>(size));
            readBytes(fd, 2);
        }
    } else if (type == '-') {
        ::close(fd);
        throw std::runtime_error("redis error: " + line.substr(1));
    }
    ::close(fd);
    return result;
}

// 从 Redis socket 中读取一行，主要用于读取 RESP 首行。
std::string RedisSessionStore::readLine(int fd) {
    std::string out;
    char ch = 0;
    while (::recv(fd, &ch, 1, 0) == 1) {
        if (ch == '\r') {
            ::recv(fd, &ch, 1, 0);
            break;
        }
        out.push_back(ch);
    }
    return out;
}

// 从 Redis socket 中读取指定长度的内容。
std::string RedisSessionStore::readBytes(int fd, std::size_t size) {
    std::string out(size, '\0');
    std::size_t offset = 0;
    while (offset < size) {
        ssize_t n = ::recv(fd, &out[offset], size - offset, 0);
        if (n <= 0) {
            throw std::runtime_error("redis read failed");
        }
        offset += static_cast<std::size_t>(n);
    }
    return out;
}

} // namespace cloud_disk
