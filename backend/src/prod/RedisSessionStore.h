#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cloud_disk {

// 使用 Redis 保存登录 token 和用户 id 的对应关系。
class RedisSessionStore {
public:
    // 保存 Redis 地址、端口和会话有效期配置。
    RedisSessionStore(std::string host, int port, int ttlSeconds);

    // 写入登录 token，过期时间由 ttlSeconds 控制。
    void save(const std::string& token, std::int64_t userId) const;

    // 根据 token 查询对应用户 id；命中时会刷新过期时间（滑动过期）。
    // 不存在或已过期时返回空。
    std::optional<std::int64_t> findUserId(const std::string& token) const;

private:
    // 把已有会话的 TTL 重新设为 ttlSeconds_。
    void touch(const std::string& token) const;
    // 生成项目专用的 Redis key，避免和其他业务数据冲突。
    static std::string key(const std::string& token);

    // 发送一条 Redis RESP 命令，并解析简单字符串或批量字符串响应。
    std::optional<std::string> command(const std::vector<std::string>& parts) const;

    // 从 socket 读取一行 RESP 文本，直到 CRLF。
    static std::string readLine(int fd);

    // 从 socket 读取固定长度的数据。
    static std::string readBytes(int fd, std::size_t size);

    std::string host_;
    int port_ = 6379;
    int ttlSeconds_ = 86400;
};

}
