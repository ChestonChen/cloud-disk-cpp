#pragma once

#include "prod/MySqlDatabase.h"
#include "prod/RedisSessionStore.h"

namespace cloud_disk {

// 注册分享相关接口：创建分享链接、查看公开分享、公开下载文件。
void registerShareRoutes(const MySqlDatabase& db, const RedisSessionStore& sessions);

} // namespace cloud_disk
