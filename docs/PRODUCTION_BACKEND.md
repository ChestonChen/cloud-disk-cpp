# 生产版后端说明

这一版在原有轻量 MVP 外，新增了一个生产后端构建目标：`cloud-disk-prod`。

它的目标不是把项目做成复杂的企业级系统，而是让简历里写到的技术栈真实落地：

- Drogon：负责 HTTP 服务、路由注册和静态文件响应。
- MySQL：通过官方 MySQL C client 保存用户、文件元数据、对象索引和分享记录。
- Redis：保存登录 token 到用户 id 的会话映射，并设置过期时间。
- 本地文件系统：保存真实文件内容，MySQL 只保存文件路径和内容哈希。

## 构建

默认构建仍然是零依赖的轻量版本：

```bash
cmake -S backend -B build
cmake --build build
```

生产版需要先安装 Drogon、MySQL 和 Redis：

```bash
brew install drogon mysql redis
```

然后启用 CMake 开关：

```bash
cmake -S backend -B build-prod -DCLOUD_DISK_WITH_DROGON=ON
cmake --build build-prod
```

构建成功后会生成：

```text
build-prod/cloud-disk-prod
```

## 初始化 MySQL

创建数据库和用户：

```sql
CREATE DATABASE cloud_disk DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'cloud_disk'@'localhost' IDENTIFIED BY 'cloud_disk_password';
GRANT ALL PRIVILEGES ON cloud_disk.* TO 'cloud_disk'@'localhost';
FLUSH PRIVILEGES;
```

导入表结构：

```bash
mysql -u cloud_disk -pcloud_disk_password cloud_disk < backend/sql/001_initial_schema.sql
```

## 启动 Redis

本机开发可以直接启动 Redis：

```bash
brew services start redis
```

生产版后端默认连接：

```text
127.0.0.1:6379
```

如果 Redis 不在本机，可以通过环境变量调整：

```bash
export CLOUD_DISK_REDIS_HOST=127.0.0.1
export CLOUD_DISK_REDIS_PORT=6379
export CLOUD_DISK_SESSION_TTL=86400
```

## 配置 Drogon 和 MySQL

复制配置模板：

```bash
cp backend/config/drogon.example.json backend/config/drogon.json
```

Drogon 配置只负责 HTTP 线程等运行参数。MySQL 连接通过环境变量配置，更方便本机和服务器部署：

```bash
export CLOUD_DISK_MYSQL_HOST=127.0.0.1
export CLOUD_DISK_MYSQL_PORT=3306
export CLOUD_DISK_MYSQL_DATABASE=cloud_disk
export CLOUD_DISK_MYSQL_USER=cloud_disk
export CLOUD_DISK_MYSQL_PASSWORD=cloud_disk_password
```

## 运行

```bash
CLOUD_DISK_DROGON_CONFIG=backend/config/drogon.json \
CLOUD_DISK_STORAGE=./storage-prod \
CLOUD_DISK_WEB_ROOT=./web \
CLOUD_DISK_PORT=8080 \
./build-prod/cloud-disk-prod
```

打开：

```text
http://127.0.0.1:8080
```

## 面试讲法

可以这样解释这一版：

- 我保留了一个零依赖 MVP，方便展示和本机运行。
- 我另外做了一个 Drogon 生产版入口，体现真实 C++ Web 后端能力。
- 用户、文件、对象、分享这些长期数据落在 MySQL。
- 登录后的 token 不再只放进程内存，而是写入 Redis，并设置 TTL。
- 文件内容没有直接塞进数据库，而是放在对象存储目录，MySQL 只保存哈希、大小、路径和引用计数。
- 分享下载走公开接口，私有下载接口仍然校验当前登录账号，实现账号隔离。

这个规模足够体现技术栈，但没有引入过多中间层、消息队列或微服务，讲起来比较直接。

## 生产版 Smoke 测试

在 MySQL 表结构已导入、Redis 已启动后运行：

```bash
scripts/prod_smoke_test.sh
```

覆盖范围：

- Drogon 健康检查。
- MySQL 用户注册和文件元数据写入。
- Redis token 登录态。
- 私有文件上传和下载。
- 公开分享链接下载。

成功输出：

```text
PROD SMOKE OK
```
