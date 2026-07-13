# 技术栈说明

## 两个运行档位

项目现在保留两个运行档位，分别服务于“快速展示”和“简历级后端能力”。

### 轻量 MVP：`cloud-disk`

- C++17：实现后端业务逻辑。
- CMake：跨平台构建。
- POSIX Socket：实现最小 HTTP 服务。
- `std::filesystem`：处理本地目录和文件路径。
- 本地文件系统：保存真实文件内容。
- `metadata.tsv`：保存用户、文件、对象、上传会话、分享和回收站元数据。
- Python 标准库：实现普通测试和压测脚本。

这个版本的优势是启动成本低，适合学习、演示和功能验证。

### 生产后端：`cloud-disk-prod`

生产后端已经新增为可选构建目标，启用方式：

```bash
cmake -S backend -B build-prod -DCLOUD_DISK_WITH_DROGON=ON
cmake --build build-prod
```

#### Drogon

用于：

- 路由管理。
- JSON 请求和响应。
- 多线程请求处理。
- 更接近真实 C++ 后端项目。

#### MySQL

保存长期数据：

- 用户账号。
- 文件夹树。
- 文件元数据。
- 物理对象信息。
- SHA-256 哈希。
- 对象引用计数。
- 分片上传会话。
- 已上传分片记录。
- 分享链接。
- 回收站状态。

MySQL 负责可靠持久化和事务一致性。

当前实现使用 MySQL 官方 C client 直连数据库。这样可以避免 Drogon 预编译包是否启用 MySQL backend 的不确定性，同时仍然是真实 MySQL 持久化。

#### Redis

保存登录会话：

- 登录态。
- token 到 user_id 的映射。
- token TTL 过期控制。

Redis 负责加速、过期控制和并发保护。

## 后续可继续升级

当前生产后端刻意没有做过度工程，后续可以继续补 OpenSSL SHA-256、HTTPS、对象存储、限流、后台清理任务和更完整的事务封装。

