#pragma once

#include <string>

namespace cloud_disk {

// 读取字符串类型环境变量；没有配置时返回默认值。
std::string envString(const char* name, const std::string& fallback);

// 读取整数类型环境变量；没有配置时返回默认值。
int envInt(const char* name, int fallback);

} // namespace cloud_disk
