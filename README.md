# Cloud Disk C++

一个 C++17 网盘后端项目，使用 Drogon 提供 HTTP 服务，MySQL 保存文件元数据，Redis 保存登录会话，前端通过浏览器访问。

## Features

- 用户注册、登录
- 文件夹创建和文件列表
- 文件上传、下载
- 内容哈希去重
- 公开分享页、访问码、公开下载
- MySQL 元数据持久化
- Redis token 会话

## Layout

```text
backend/
  src/
    prod/ProdMain.cpp     # HTTP routes, MySQL access, Redis sessions
    utils/                # json, hash, path helpers
  sql/                    # MySQL schema
scripts/                  # smoke test
web/                      # browser UI
```

## Dependencies

macOS:

```bash
brew install drogon mysql redis
brew services start mysql
brew services start redis
```

## Database

```bash
mysql -u root -e "
CREATE DATABASE IF NOT EXISTS cloud_disk DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'user1'@'localhost' IDENTIFIED BY '123456';
GRANT ALL PRIVILEGES ON cloud_disk.* TO 'user1'@'localhost';
FLUSH PRIVILEGES;
"

mysql -u user1 -p123456 cloud_disk < backend/sql/001_initial_schema.sql
```

## Build

```bash
cmake -S backend -B build
cmake --build build
```

## Run

```bash
CLOUD_DISK_STORAGE=./storage \
CLOUD_DISK_WEB_ROOT=./web \
CLOUD_DISK_PORT=8080 \
./build/cloud-disk
```

Open:

```text
http://127.0.0.1:8080
```

Optional environment variables:

```bash
export CLOUD_DISK_MYSQL_HOST=127.0.0.1
export CLOUD_DISK_MYSQL_PORT=3306
export CLOUD_DISK_MYSQL_DATABASE=cloud_disk
export CLOUD_DISK_MYSQL_USER=user1
export CLOUD_DISK_MYSQL_PASSWORD=123456
export CLOUD_DISK_REDIS_HOST=127.0.0.1
export CLOUD_DISK_REDIS_PORT=6379
export CLOUD_DISK_SESSION_TTL=86400
```

## Test

```bash
scripts/prod_smoke_test.sh
```
