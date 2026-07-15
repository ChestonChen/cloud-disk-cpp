#pragma once

#include <filesystem>
#include <string>

namespace cloud_disk {

// 校验文件名或文件夹名，避免空名、路径穿越和非法分隔符。
bool isValidName(const std::string& name);

// 确保目录存在；不存在时递归创建，并返回该路径。
std::filesystem::path ensureDirectory(const std::filesystem::path& path);

// 把 token 转成适合放进路径的安全片段。
std::string sanitizeTokenPathPart(const std::string& value);

} // namespace cloud_disk

