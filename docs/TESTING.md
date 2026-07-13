# 测试与压测说明

## 测试目标

当前项目提供三类验证方式：

- `scripts/harness.sh`：端到端回归验证，适合每次改完代码后快速确认核心流程没有坏。
- `scripts/functional_test.py`：跨平台普通功能测试，适合在 Mac 和 Windows 上运行。
- `scripts/load_test.py`：本地压测脚本，适合说明项目做过压力测试，并观察吞吐和延迟。

这些脚本都会自动构建后端，并使用临时存储目录启动服务，不污染本地正式数据。

## 普通功能测试

运行：

```bash
python3 scripts/functional_test.py
```

覆盖范围：

- 健康检查。
- 用户注册和登录。
- 创建文件夹。
- 直接上传文件。
- 秒传和对象引用计数。
- 创建分享链接。
- 分享页可直接打开。
- 分享访问码校验。
- 公开分享下载。
- 多账号私有文件隔离。
- 分片上传初始化。
- 分片上传进度查询。
- 分片合并和下载校验。
- 文件软删除。
- 回收站列表。
- 文件恢复。
- 永久删除。
- 文件夹永久删除时递归清理子文件。

成功输出：

```text
FUNCTIONAL TEST OK
```

## 压测

运行：

```bash
python3 scripts/load_test.py --requests 100 --concurrency 10 --payload-size 1024
```

参数说明：

- `--requests`：压测流程数量。每个流程包含上传、下载、列表三次 HTTP 请求。
- `--concurrency`：并发 worker 数。
- `--payload-size`：每个上传文件的基础大小，单位是字节。

输出示例：

```text
LOAD TEST RESULT
flows=30 success=30 failed=0 success_rate=100.00%
http_requests=90 elapsed_sec=0.252 rps=357.83
latency_ms_avg=13.62
latency_ms_p50=7.82
latency_ms_p95=21.64
latency_ms_p99=106.14
```

面试时可以这样解释：

- 我没有只写接口，还补了自动化测试脚本。
- 普通功能测试覆盖了注册、登录、上传、下载、秒传、分片、分享页、公开下载、账号隔离、回收站等主链路。
- 压测脚本使用并发 worker 模拟多个用户同时上传、下载和查询文件。
- 压测输出包含成功率、RPS、平均延迟和 P50/P95/P99，能看出系统吞吐和尾延迟。
- 当前版本是本地单进程 HTTP 服务，压测主要用于验证接口稳定性和基本吞吐；后续接入 Drogon、多线程、MySQL 和 Redis 后，可以继续沿用这个脚本对比优化前后的结果。

