# chatSystem V2

V2 按原课件的方式开发：每个微服务先独立实现、独立测试，最后通过 Gateway 联调。Qt 暂时不开发，后续直接使用 V1 客户端验证兼容性。

## 开发顺序

```text
FileServer
→ SpeechServer
→ MessageStoreServer
→ MessageTransmitServer
→ UserServer
→ FriendServer
→ GatewayServer
→ 全链路联调
```

## 当前阶段：FileServer

FileServer 已实现课件中的四个 RPC：

- `PutSingleFile`
- `GetSingleFile`
- `PutMultiFile`
- `GetMultiFile`

当前调用关系：

```text
file_client
    |
    | Protobuf + brpc RPC
    v
FileServiceImpl
    |
    v
FileStore
    |
    v
v2/data/file/
```

这一阶段先通过固定地址直连。FileServer 独立验证通过后，再加入 etcd 服务注册与发现。

## 构建

在仓库根目录执行：

```bash
cmake -S v2 -B v2/build -DCMAKE_BUILD_TYPE=Debug
cmake --build v2/build -j2
```

## 单元测试

```bash
cd v2/build
ctest --output-on-failure
cd ../..
```

## RPC 联调

打开终端一，启动 FileServer：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/file/file_server
```

打开终端二，运行客户端：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/file/file_client
```

客户端会上传一段文本，再根据服务端返回的 `file_id` 下载并校验内容。

FileServer 默认配置：

- RPC 地址：`127.0.0.1:10002`
- 存储目录：`v2/data/file`

可以使用 gflags 修改：

```bash
./v2/build/services/file/file_server \
  --port=11002 \
  --storage_path=./v2/data/file-test
```

## 当前依赖

- C++17
- CMake
- Protobuf
- brpc 1.10.0
- gflags
- spdlog
- GoogleTest

构建环境已经安装完成。后续服务用到 Redis、RabbitMQ、MySQL、etcd 时再逐项增加，不提前把所有中间件混在一起。
