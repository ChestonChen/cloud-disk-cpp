#include "utils/Path.h"

#include <stdexcept>

namespace cloud_disk {
namespace {

// ASCII 控制字符：0~31 以及 DEL(127)，例如 \n \r \t。
bool isControlChar(unsigned char ch) {
    return ch < 32 || ch == 127;
}

// 字母或数字。
bool isLetterOrDigit(unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

}

// 校验用户传入的文件名或文件夹名，避免非法字符和路径穿越。
bool isValidName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 255) {
        return false;
    }
    for (char ch : name) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (isControlChar(uch) || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?'
            || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
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

// 清理 token 中不适合放进路径的字符，只保留字母数字和 - _。
std::string sanitizeTokenPathPart(const std::string& value) {
    std::string out;
    for (char ch : value) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (isLetterOrDigit(uch) || ch == '-' || ch == '_') {
            out.push_back(ch);
        }
    }
    return out;
}

}
