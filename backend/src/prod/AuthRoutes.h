#pragma once

#include "prod/MySqlDatabase.h"
#include "prod/RedisSessionStore.h"

namespace cloud_disk {

// 注册用户相关接口：注册、登录、查询当前用户信息。
void registerAuthRoutes(const MySqlDatabase& db, const RedisSessionStore& sessions);

}
