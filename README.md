# Cloud Disk C++

一个使用 C++17 实现的轻量级个人网盘项目。

这个项目从零开始实现，目标是用于后端开发练习和面试展示。它借用了网盘产品的常见形态，但代码结构尽量保持清晰，方便说明认证、元数据持久化、文件存储、断点续传、分享、回收站和后台清理等后端工程能力。

当前版本是一个轻依赖本地 MVP：项目内置了一个最小 HTTP 服务，并使用本地文件保存元数据，因此可以在一台干净机器上直接编译和运行。后续面向生产化的演进方向，是把 HTTP 层和持久化层替换为 Drogon、MySQL、Redis，以及基于 OpenSSL 的哈希和 JWT 实现。

## MVP 功能

- 用户注册和登录。
- 进程内 Bearer Token 鉴权。
- 文件夹和文件元数据管理。
- 小文件直接上传和下载。
- 内容哈希、对象去重和秒传。
- 大文件分片上传、进度查询和合并。
- 文件列表查询和软删除。
- 回收站列表、恢复和永久删除。
- 公开分享链接、访问码和公开下载。
- 查询用户已用空间。

## 技术栈

### 当前 MVP

- C++17
- CMake
- POSIX Socket HTTP 服务
- 本地文件系统元数据和对象存储
- Python 标准库测试与压测脚本

### 后续升级方向

- Drogon
- MySQL
- Redis
- OpenSSL

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

该脚本会编译后端，在临时存储目录中启动服务，并验证健康检查、注册、登录、用户信息、创建文件夹、直接上传、秒传、分片上传、列表、下载、分享、回收站、删除，以及递归删除文件夹等行为。

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

