#include "prod/AppConfig.h"
#include "prod/AuthRoutes.h"
#include "prod/FileRoutes.h"
#include "prod/MySqlDatabase.h"
#include "prod/RedisSessionStore.h"
#include "prod/ShareRoutes.h"
#include "prod/StaticRoutes.h"

#include <drogon/drogon.h>

#include <filesystem>

using namespace cloud_disk;

// 程序入口：读取配置、初始化 MySQL/Redis、注册路由并启动 Drogon 服务。
int main() {
    auto config = envString("CLOUD_DISK_DROGON_CONFIG", "./backend/config/drogon.json");
    if (std::filesystem::exists(config)) {
        drogon::app().loadConfigFile(config);
    }

    auto storageRoot = std::filesystem::path(envString("CLOUD_DISK_STORAGE", "./storage"));
    auto webRoot = std::filesystem::path(envString("CLOUD_DISK_WEB_ROOT", "./web"));

    RedisSessionStore sessions(envString("CLOUD_DISK_REDIS_HOST", "127.0.0.1"),
                               envInt("CLOUD_DISK_REDIS_PORT", 6379),
                               envInt("CLOUD_DISK_SESSION_TTL", 86400));

    MySqlDatabase db(envString("CLOUD_DISK_MYSQL_HOST", "127.0.0.1"),
                     envInt("CLOUD_DISK_MYSQL_PORT", 3306),
                     envString("CLOUD_DISK_MYSQL_DATABASE", "cloud_disk"),
                     envString("CLOUD_DISK_MYSQL_USER", "cloud_disk"),
                     envString("CLOUD_DISK_MYSQL_PASSWORD", "cloud_disk_password"));

    registerHealthRoute();
    registerStaticRoutes(webRoot);
    registerAuthRoutes(db, sessions);
    registerFileRoutes(db, sessions, storageRoot);
    registerShareRoutes(db, sessions);

    drogon::app().addListener("0.0.0.0", envInt("CLOUD_DISK_PORT", 8080));
    drogon::app().run();
}
