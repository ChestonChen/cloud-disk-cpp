#include "prod/AppConfig.h"

#include <cstdlib>

namespace cloud_disk {

// 读取字符串环境变量，常用于数据库地址、Web 根目录等配置。
std::string envString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

// 读取整数环境变量，常用于端口号和 token 过期时间等配置。
int envInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::atoi(value) : fallback;
}

} // namespace cloud_disk
