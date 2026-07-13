#pragma once

#include <filesystem>
#include <string>

namespace cloud_disk {

bool isValidName(const std::string& name);
std::filesystem::path ensureDirectory(const std::filesystem::path& path);
std::string sanitizeTokenPathPart(const std::string& value);

} // namespace cloud_disk

