#pragma once

#include <string>

namespace cloud_disk {

// 根据文件内容生成内容哈希，用于文件对象去重和存储路径命名。
std::string contentHash(const std::string& content);

} // namespace cloud_disk

