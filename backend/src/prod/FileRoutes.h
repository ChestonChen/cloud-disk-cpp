#pragma once

#include "prod/MySqlDatabase.h"
#include "prod/RedisSessionStore.h"

#include <filesystem>

namespace cloud_disk {

// 注册文件相关接口：创建文件夹、列目录、上传文件、下载文件。
void registerFileRoutes(const MySqlDatabase& db,
                        const RedisSessionStore& sessions,
                        const std::filesystem::path& storageRoot);

} // namespace cloud_disk
