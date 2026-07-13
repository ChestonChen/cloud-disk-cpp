# 桌面 App 使用说明

## 普通用户怎么使用

目标体验：

1. 下载 `Cloud Disk_0.1.0_aarch64.dmg`。
2. 双击打开 DMG。
3. 把 `Cloud Disk.app` 拖到 `Applications`。
4. 打开 `Cloud Disk`。
5. 在登录页注册或登录。
6. 进入网盘工作台后上传、下载、创建文件夹、分享和使用回收站。

用户不需要手动启动后端，也不需要知道 `8080` 端口是什么。

## 当前 Mac 构建产物

本机已经构建出 Mac Apple Silicon 版本：

```text
src-tauri/target/release/bundle/dmg/Cloud Disk_0.1.0_aarch64.dmg
```

也有未打包的 `.app`：

```text
src-tauri/target/release/bundle/macos/Cloud Disk.app
```

## 开发者如何重新构建

先安装依赖：

```bash
npm install
```

构建桌面 App：

```bash
npm run desktop:build
```

构建时会自动：

1. 编译 C++ 后端 `cloud-disk`。
2. 把后端复制为 Tauri sidecar。
3. 打包 Web UI。
4. 生成 `.app` 和 `.dmg`。

## 数据保存在哪里

桌面 App 会自动启动本地 C++ 后端，并把用户数据保存在系统应用数据目录下。

也就是说：

- 每个用户电脑上的数据是本地数据。
- 不需要公网服务器。
- 不同电脑之间不会自动同步。
- 如果要多设备同步或多人访问同一套数据，需要后续做公共后端。

## Windows 说明

当前是在 Mac 上构建出的 Mac 版本。

Windows 版本需要在 Windows 机器或 GitHub Actions Windows Runner 上执行：

```bash
npm install
npm run desktop:build
```

同一套 Tauri 配置可以继续复用，但 Windows 产物会是 `.msi` 或 `.exe` 安装包。

