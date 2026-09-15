# 聊天客户端 V3

V3 是独立的跨平台桌面客户端工程，目标是生成可以直接发给测试人员的
Windows 免安装压缩包。它拥有自己的 Qt 源码、Protobuf 协议和图片资源，
构建时不读取 V1 或 V2 目录。

## 目录

```text
v3/
├── CMakeLists.txt
├── contracts/                 Protobuf 协议
├── client/qt/                 Qt 桌面客户端
├── config/server.ini.example  服务器配置示例
└── scripts/build_windows.ps1  Windows 构建及打包脚本
```

## 在 Windows 上生成免安装包

准备以下软件：

- Windows 10 或 Windows 11，64 位
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”
- CMake
- Qt 5.15.x 的 `msvc2019_64` 组件
- vcpkg

在 PowerShell 中进入 `v3`，执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows.ps1 `
  -QtDir "C:\Qt\5.15.2\msvc2019_64" `
  -VcpkgRoot "C:\vcpkg" `
  -HttpUrl "http://服务器地址:9000" `
  -WebSocketUrl "ws://服务器地址:9001"
```

脚本会完成以下操作：

1. 使用 vcpkg 安装静态 Protobuf 运行库。
2. 仅编译 Qt 客户端，不编译 bRPC 后端服务。
3. 使用 `windeployqt` 收集 Qt DLL 和 `qwindows.dll`。
4. 写入与程序同目录的 `server.ini`。
5. 生成 `dist/Chat-Windows.zip`。

把 `Chat-Windows.zip` 发给测试人员。对方解压后双击 `Chat.exe`，不需要安装
Qt、Protobuf 或 noVNC。

## 修改服务器地址

Windows 包中的 `server.ini` 格式如下：

```ini
[server]
http_url=http://192.0.2.10:9000
ws_url=ws://192.0.2.10:9001
```

也可以临时通过命令行覆盖：

```powershell
.\Chat.exe `
  --http-url=http://192.0.2.10:9000 `
  --ws-url=ws://192.0.2.10:9001
```

`127.0.0.1` 只适合客户端和服务器在同一台电脑上的情况。发给其他人测试时，
必须填写对方能够访问的服务器局域网、虚拟局域网或公网地址。

## 服务器要求

服务器必须允许测试人员访问两个端口：

- `9000`：HTTP 请求
- `9001`：WebSocket 实时消息

网关需要监听可访问的网卡地址，而不是只监听 `127.0.0.1`。发布前使用：

```bash
ss -ltnp | grep -E ':9000|:9001'
```

确认监听地址。当前协议是明文 HTTP/WS，不建议直接暴露到不受信任的公网。
同学测试优先使用受控的局域网或私人虚拟局域网；公网部署应增加 HTTPS/WSS、
访问控制和限流。

## Linux 独立构建检查

Linux 上可以用下面的命令验证 V3 工程没有依赖 V2：

```bash
cmake -S v3 -B v3/build -DBUILD_TESTING=ON
cmake --build v3/build -j2
cd v3/build
ctest --output-on-failure
```
