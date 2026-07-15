#pragma once

#include <map>
#include <string>
#include <vector>

namespace cloud_disk {

using JsonObject = std::map<std::string, std::string>;

// 解析简单的一层 JSON 对象，适合当前接口的注册、登录等请求体。
JsonObject parseFlatJsonObject(const std::string& body);

// 转义 JSON 字符串中的引号、反斜杠和控制字符。
std::string jsonEscape(const std::string& value);

// 把键值对组装成 JSON 对象字符串。
std::string jsonObject(const JsonObject& fields);

// 把多个 JSON 字符串组装成 JSON 数组字符串。
std::string jsonArray(const std::vector<std::string>& values);

// 生成统一成功响应：{"code":0,"data":...}。
std::string okJson(const std::string& dataJson);

// 生成统一错误响应：{"code":错误码,"message":"错误信息"}。
std::string errorJson(int code, const std::string& message);

} // namespace cloud_disk

