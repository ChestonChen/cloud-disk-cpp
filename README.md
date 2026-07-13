# Cloud Disk C++

一个使用 C++17 实现的轻量级个人网盘项目。

这个项目从零开始实现，目标是用于后端开发练习和面试展示。它借用了网盘产品的常见形态，但代码结构尽量保持清晰，方便说明认证、元数据持久化、文件存储、断点续传、分享、回收站和后台清理等后端工程能力。

项目提供两个运行档位：

- `cloud-disk`：轻依赖版本，内置最小 HTTP 服务，使用本地文件保存元数据，适合快速演示和桌面 App。
- `cloud-disk-prod`：生产后端版本，使用 Drogon 提供 HTTP 服务，MySQL 保存元数据，Redis 保存登录 token 会话，适合部署到服务器作为简历项目展示。

## MVP 功能

- 用户注册和登录。
- 进程内 Bearer Token 鉴权。
- 文件夹和文件元数据管理。
- 小文件直接上传和下载。
- 内容哈希、对象去重和秒传。
- 大文件分片上传、进度查询和合并。
- 文件列表查询和软删除。
- 回收站列表、恢复和永久删除。
- 公开分享页面、访问码和公开下载。
- 多账号数据隔离：普通文件接口只能访问自己的文件，公开分享接口不需要登录。
- 查询用户已用空间。

## 技术栈

### 当前 MVP

- C++17
- CMake
- POSIX Socket HTTP 服务
- 本地文件系统元数据和对象存储
- Python 标准库测试与压测脚本

### 生产后端版本

- Drogon
- MySQL
- Redis
- 本地对象存储目录

## 项目结构

```text
backend/
  src/
    services/
    models/
    utils/
    config/
  sql/
docs/
scripts/
web/
```

## 构建和运行

### 桌面 App

普通用户优先使用桌面 App。Mac 构建产物：

```text
src-tauri/target/release/bundle/dmg/Cloud Disk_0.1.0_aarch64.dmg
```

双击 DMG，把 `Cloud Disk.app` 拖到 `Applications`，然后打开 App 即可。App 会自动启动本地 C++ 后端，用户不需要手动启动服务。

开发者重新构建：

```bash
npm install
npm run desktop:build
```

详细说明见 `docs/DESKTOP_APP.md`。

### 本机浏览器模式

```bash
cmake -S backend -B build
cmake --build build
CLOUD_DISK_STORAGE=./storage CLOUD_DISK_PORT=8080 ./build/cloud-disk
```

启动后打开浏览器访问：

```text
http://127.0.0.1:8080
```

你可以在页面里完成注册、登录、创建文件夹、上传、下载、秒传、分片上传、分享和回收站操作。

### 云端部署模式

如果希望别人也能访问，不要让他们打开你的 `127.0.0.1`，而是把后端运行在一台公网服务器上。轻量版可以这样启动：

```bash
CLOUD_DISK_STORAGE=/data/cloud-disk CLOUD_DISK_PORT=8080 ./build/cloud-disk
```

服务器安全组或防火墙放行 `8080` 后，用户访问：

```text
http://<服务器公网 IP>:8080
```

这时每个账号只能看到自己的文件。创建分享后，服务会返回类似 `http://<服务器公网 IP>:8080/share?token=...` 的链接，其他人可以在浏览器打开分享页，并在访问码正确时下载文件。

### Drogon/MySQL/Redis 生产后端

构建生产后端：

```bash
cmake -S backend -B build-prod -DCLOUD_DISK_WITH_DROGON=ON
cmake --build build-prod
```

运行前需要准备 MySQL 表结构和 Redis 服务，详细步骤见 `docs/PRODUCTION_BACKEND.md`。

生产版 smoke 测试：

```bash
scripts/prod_smoke_test.sh
```

## GitHub Pages 展示链接

仓库配置了 GitHub Pages 自动发布，`web/` 目录会发布成免费展示页面。

注意：GitHub Pages 只能托管静态网页，不能运行 C++ 后端。因此 `github.io` 链接提供的是前端演示模式：可以注册/登录进入工作台，并用浏览器本地状态体验上传、下载、分享、回收站等流程。完整的真实后端注册、上传、下载和持久化功能，需要在本机启动 C++ 后端服务后访问 `http://127.0.0.1:8080`。

健康检查接口：

```bash
curl http://127.0.0.1:8080/health
```

## Harness 验证

运行端到端验证脚本：

```bash
scripts/harness.sh
```

该脚本会编译后端，在临时存储目录中启动服务，并验证健康检查、注册、登录、用户信息、创建文件夹、直接上传、秒传、分片上传、列表、下载、分享页、公开下载、账号隔离、回收站、删除，以及递归永久删除文件夹等行为。

跨平台普通功能测试：

```bash
python3 scripts/functional_test.py
```

本地压测：

```bash
python3 scripts/load_test.py --requests 100 --concurrency 10 --payload-size 1024
```

压测脚本会输出请求总数、成功率、RPS、平均延迟、P50、P95 和 P99，方便面试时说明测试方法和结果。

详细实现计划见 `docs/PLAN.md`。

