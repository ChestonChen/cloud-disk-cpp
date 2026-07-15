#include "utils/Path.h"

#include <cctype>
#include <stdexcept>

namespace cloud_disk {

// 校验用户传入的文件名或文件夹名，避免非法字符和路径穿越。
bool isValidName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 255) {
        return false;
    }
    for (char ch : name) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::iscntrl(uch) || ch == '/' || ch == '\\' || ch == ':' || ch == '*'
            || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            return false;
        }
    }
    return true;
}

// 创建目录并确认创建结果，失败时抛出异常。
std::filesystem::path ensureDirectory(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
    if (!std::filesystem::is_directory(path)) {
        throw std::runtime_error("failed to create directory: " + path.string());
    }
    return path;
}

// 清理 token 中不适合放进路径的字符。
std::string sanitizeTokenPathPart(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace cloud_disk

