#pragma once

#include <map>
#include <string>
#include <vector>

namespace cloud_disk {

using JsonObject = std::map<std::string, std::string>;

JsonObject parseFlatJsonObject(const std::string& body);
std::string jsonEscape(const std::string& value);
std::string jsonObject(const JsonObject& fields);
std::string jsonArray(const std::vector<std::string>& values);
std::string okJson(const std::string& dataJson);
std::string errorJson(int code, const std::string& message);

} // namespace cloud_disk

