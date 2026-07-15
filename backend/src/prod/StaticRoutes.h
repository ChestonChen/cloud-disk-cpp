#pragma once

#include <filesystem>

namespace cloud_disk {

// 注册前端静态资源路由，返回 index.html、styles.css 和 app.js。
void registerStaticRoutes(const std::filesystem::path& webRoot);

// 注册健康检查接口，用来确认服务和技术栈状态。
void registerHealthRoute();

}
