# chatSystem V2

V2 按原课件的方式开发：每个微服务先独立实现、独立测试，最后通过 Gateway 联调。Qt 暂时不开发，后续直接使用 V1 客户端验证兼容性。

## 开发顺序

```text
FileServer
→ MessageStoreServer
→ MessageTransmitServer
→ UserServer
→ FriendServer
→ GatewayServer
→ 全链路联调
→ SpeechServer（可选，最后开发）
```

## 已完成：FileServer

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

## 已完成：MessageStoreServer

MessageStoreServer 按 V1 的协议和行为实现三个 RPC：

- `GetHistoryMsg`：按闭区间查询历史消息；
- `GetRecentMsg`：获取最近 N 条消息，支持 `cur_time`；
- `MsgSearch`：在会话内搜索文本消息。

消息入库行为也与 V1 一致：

- 文本消息直接保存正文；
- 图片、文件和语音消息先把二进制正文上传至 FileServer；
- 消息仓库只保存 `file_id`、文件名和文件大小；
- 查询历史/最近消息时，批量调用 FileServer 补全二进制内容。

```text
MessageInfo (RabbitMQ 消费入口预留)
    |
    v
MessageServiceImpl::on_message()
    |----------------------> FileServer (保存文件正文)
    v
MessageStore
    |
    v
v2/data/message/messages.bin
```

当前独立阶段使用可恢复的本地追加日志持久化，并使用子串匹配完成文本搜索。
V1 的 MySQL、Elasticsearch 和 RabbitMQ 适配将在 MessageTransmit 全链路阶段接入；
`on_message()` 已保留与 V1 RabbitMQ 消费回调相同的入口。

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

### MessageStoreServer RPC 联调

启动 MessageStoreServer：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/message/message_server
```

查询最近消息：

```bash
./v2/build/services/message/message_client \
  --operation=recent \
  --chat_session_id=demo-session \
  --msg_count=20
```

查询历史消息：

```bash
./v2/build/services/message/message_client \
  --operation=history \
  --chat_session_id=demo-session \
  --start_time=0 \
  --over_time=4102444800
```

搜索文本消息：

```bash
./v2/build/services/message/message_client \
  --operation=search \
  --chat_session_id=demo-session \
  --search_key=你好
```

MessageStoreServer 默认配置：

- RPC 地址：`127.0.0.1:10005`；
- FileServer 地址：`127.0.0.1:10002`；
- 消息元数据：`v2/data/message/messages.bin`。

## 当前依赖

- C++17
- CMake
- Protobuf
- brpc 1.10.0
- gflags
- spdlog
- GoogleTest

构建环境已经安装完成。后续服务用到 Redis、RabbitMQ、MySQL、Elasticsearch、etcd 时再逐项增加，不提前把所有中间件混在一起。
