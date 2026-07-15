#pragma once

#include "prod/MySqlDatabase.h"
#include "prod/RedisSessionStore.h"
#include "prod/Types.h"

#include <drogon/drogon.h>

#include <cstdint>
#include <optional>
#include <string>

namespace cloud_disk {

// 把有符号整数转换成 SQL 可拼接的数字字符串。
std::string sqlNumber(std::int64_t value);

// 把无符号整数转换成 SQL 可拼接的数字字符串。
std::string sqlNumber(std::uint64_t value);

// 从数据库行中读取 int64 字段。
std::int64_t asInt64(const DbRow& row, const std::string& name);

// 从数据库行中读取 uint64 字段。
std::uint64_t asUInt64(const DbRow& row, const std::string& name);

// 从数据库行中读取布尔字段，兼容 MySQL 的 1/0 和文本 true。
bool asBool(const DbRow& row, const std::string& name);

// 生成密码摘要；当前项目用于教学演示，生产环境应替换为更强的密码哈希。
std::string hashPassword(const std::string& username, const std::string& password);

// 生成登录 token。
std::string newToken();

// 生成公开分享 token。
std::string newShareToken();

// 校验用户名格式，只允许字母、数字、下划线和短横线。
bool validUsername(const std::string& username);

// 转义 HTML 特殊字符，避免分享页直接拼接文件名时破坏页面结构。
std::string htmlEscape(const std::string& value);

// 根据请求头拼出当前服务的基础 URL，用于返回分享链接。
std::string baseUrl(const drogon::HttpRequestPtr& req);

// 构造普通文本响应，并统一添加 CORS 头。
drogon::HttpResponsePtr textResponse(drogon::HttpStatusCode status,
                                     const std::string& body,
                                     const std::string& contentType);

// 构造 JSON 响应。
drogon::HttpResponsePtr jsonResponse(drogon::HttpStatusCode status, const std::string& body);

// 构造统一格式的错误 JSON 响应。
drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status, int code, const std::string& message);

// 根据用户 id 查询用户基础信息。
std::optional<UserContext> findUserById(const MySqlDatabase& db, std::int64_t userId);

// 从 Authorization 请求头读取 token，并通过 Redis/MySQL 解析当前用户。
std::optional<UserContext> requireUser(const drogon::HttpRequestPtr& req,
                                       const MySqlDatabase& db,
                                       const RedisSessionStore& sessions);

// 判断目标父目录是否存在，根目录 parent_id=0 永远有效。
bool parentExists(const MySqlDatabase& db, std::int64_t userId, std::int64_t parentId);

// 查询用户自己的未删除文件或文件夹。
std::optional<FileRow> findActiveFile(const MySqlDatabase& db,
                                      std::int64_t userId,
                                      std::int64_t fileId);

// 把文件记录转换成前端需要的 JSON 字符串。
std::string fileJson(const FileRow& file, bool isDeleted = false);

// 根据文件对象 id 查询磁盘存储路径。
std::optional<std::string> objectPath(const MySqlDatabase& db, std::int64_t objectId);

} // namespace cloud_disk
