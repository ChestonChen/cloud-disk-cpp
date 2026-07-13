# Cloud Disk C++ 实现计划

## 1. 产品目标

实现一个轻量级个人网盘，让用户一眼就能理解它能做什么：

- 注册和登录。
- 创建文件夹并浏览文件。
- 上传和下载文件。
- 大文件上传中断后可以继续上传。
- 相同文件内容可以通过秒传复用。
- 通过公开链接分享文件，并可选访问码。
- 删除文件先进入回收站，再支持永久删除。

实现时仍然要体现后端工程能力：认证、元数据建模、内容寻址存储、上传会话、后台清理，以及可部署的服务结构。

## 2. MVP 范围

### 用户模块

- 使用用户名和密码注册。
- 登录时校验密码。
- 签发短期访问令牌和长期刷新令牌。
- 退出登录时吊销刷新令牌。
- 查询当前用户资料和存储空间使用量。

### 文件模块

- 创建文件夹。
- 按父文件夹列出文件。
- 小文件直接上传。
- 大文件分片上传。
- 查询已上传分片，用于断点续传。
- 合并分片并完成上传。
- 下载文件。
- 重命名、移动和软删除文件。

### 存储模块

- 按 SHA-256 哈希存储物理文件内容。
- 用户可见的文件元数据和物理对象分离保存。
- 维护对象引用计数，避免过早删除被多个文件复用的内容。
- 物理文件保存到 `storage/objects/<first-two-hash-chars>/<sha256>`。
- 临时分片保存到 `storage/tmp/<upload_id>/<chunk_index>.part`。

### 分享模块

- 为文件或文件夹创建公开分享链接。
- 可选 4 位访问码。
- 可选过期时间。
- 记录查看次数和下载次数。
- 支持禁用分享链接。

### 回收站模块

- 软删除用户文件。
- 恢复已删除文件。
- 永久删除文件并减少对象引用计数。
- 清空回收站。

## 3. 技术选型

### 后端

- 语言：C++17。
- HTTP 框架：Drogon。
- 数据库：MySQL，用于可靠保存元数据。
- 缓存/会话：Redis，用于刷新令牌、限流和后续上传锁。
- 哈希：OpenSSL EVP，用于 SHA-256。
- 构建：CMake。

### 前端

第一版 MVP 保持前端尽量简单：

- 先使用静态 HTML + JavaScript 页面，后续可以改成小型 Vue 页面。
- 优先证明后端流程可以跑通。
- 尽早提供 API 示例和 curl 脚本。

### 部署

- 先支持本地开发。
- 后端 MVP 跑通后再加入 Docker Compose。
- Compose 服务包括：backend、mysql、redis。

## 4. 后端架构

```text
backend/
├── src/
│   ├── controllers/     # HTTP 请求处理
│   ├── services/        # 业务逻辑
│   ├── repositories/    # SQL 访问和持久化
│   ├── models/          # 领域模型和数据结构
│   ├── middlewares/     # 鉴权、限流
│   ├── utils/           # 哈希、JWT、响应、文件系统工具
│   ├── jobs/            # 清理任务
│   └── config/          # 运行时配置解析
├── sql/                 # 数据库迁移脚本
└── tests/               # 单元测试和集成测试
```

### 分层规则

- Controller 负责校验 HTTP 请求形态，并调用 Service。
- Service 负责业务决策和事务边界。
- Repository 负责 SQL 和数据库行到模型的映射。
- 工具层不能依赖 Controller 或 Repository。
- 所有文件路径在访问磁盘前都必须经过文件系统工具处理。

## 5. 数据库设计

### users

- `id`
- `username`
- `password_hash`
- `display_name`
- `storage_used`
- `storage_limit`
- `created_at`
- `updated_at`

### file_objects

物理文件内容对象。

- `id`
- `sha256`
- `size_bytes`
- `storage_path`
- `ref_count`
- `created_at`

### files

用户可见的文件树。

- `id`
- `user_id`
- `parent_id`
- `object_id`
- `name`
- `is_dir`
- `is_deleted`
- `deleted_at`
- `created_at`
- `updated_at`

对于文件夹，`object_id` 为空。

### upload_sessions

- `id`
- `upload_id`
- `user_id`
- `parent_id`
- `filename`
- `sha256`
- `size_bytes`
- `chunk_size`
- `total_chunks`
- `status`
- `created_at`
- `updated_at`
- `expires_at`

### upload_chunks

- `id`
- `upload_id`
- `chunk_index`
- `size_bytes`
- `created_at`

### shares

- `id`
- `share_token`
- `access_code_hash`
- `user_id`
- `file_id`
- `expires_at`
- `allow_download`
- `view_count`
- `download_count`
- `is_active`
- `created_at`

## 6. 关键实现流程

### 直接上传

1. 校验请求鉴权信息。
2. 校验文件名和父文件夹归属。
3. 将请求体读取到临时文件。
4. 计算 SHA-256。
5. 如果对象已存在，增加 `ref_count`；否则把临时文件移动到内容寻址对象路径。
6. 创建 `files` 元数据记录。
7. 只按用户新增的逻辑文件大小更新空间使用量。

### 分片上传

1. `init`：创建或复用上传会话。
2. `upload`：将单个分片写入 `storage/tmp/<upload_id>`。
3. `progress`：返回已上传的分片序号。
4. `complete`：校验所有分片，合并成临时文件，计算哈希，创建或复用对象，创建元数据记录，并清理临时分片。

### 下载

1. 校验用户鉴权，或校验公开分享。
2. 解析文件元数据。
3. 拒绝直接下载文件夹。
4. 通过 Drogon 响应流式返回物理对象文件。

### 删除和清理

1. 软删除只标记 `files.is_deleted = true`。
2. 永久删除时减少 `file_objects.ref_count`。
3. 如果 `ref_count` 变为 0，则删除物理对象。
4. 后台清理任务删除过期上传会话和孤儿临时分片。

## 7. 计划里程碑

### 里程碑 1：后端骨架

- CMake 项目。
- Drogon 应用启动。
- 配置加载。
- 健康检查接口。
- 统一 JSON 响应工具。

### 里程碑 2：认证

- 用户表结构。
- 注册、登录、退出。
- 密码哈希。
- JWT 访问令牌。
- 使用 Redis 保存刷新令牌。
- 鉴权中间件。

### 里程碑 3：文件树和直接上传

- 创建文件夹。
- 文件列表。
- 直接上传和下载。
- 内容寻址对象存储。

### 里程碑 4：分片上传

- 初始化上传会话。
- 上传分片。
- 查询上传进度。
- 完成上传并合并分片。
- 过期临时文件清理任务。

### 里程碑 5：分享和回收站

- 分享链接。
- 访问码。
- 公开元数据和下载。
- 软删除、恢复、永久删除。

### 里程碑 6：完善

- Docker Compose。
- API 示例。
- 基础测试。
- README 架构图。
- 如果时间允许，补一个简单网页。

