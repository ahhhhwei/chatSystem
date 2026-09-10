# chatSystem V2

V2 按原课件的方式开发：每个微服务先独立实现、独立测试，最后通过 Gateway 联调。
V2 Qt 桌面端已经直接对接 Gateway，不再依赖 V1 客户端。

## 开发顺序

```text
FileServer
→ MessageStoreServer
→ MessageTransmitServer
→ UserServer
→ FriendServer
→ GatewayServer
→ Qt 桌面端
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

MessageStoreServer 按 V1 的协议和行为实现三个查询 RPC：

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
V1 的 MySQL、Elasticsearch 和 RabbitMQ 适配将在基础设施全链路阶段接入；
`on_message()` 已保留与 V1 RabbitMQ 消费回调相同的入口。
此外提供内部 `StoreMessage` RPC，用于在 RabbitMQ 接入前完成微服务全链路联调。

## 已完成：MessageTransmitServer（独立阶段）

MessageTransmitServer 实现 V1 的 `GetTransmitTarget` RPC：

- 根据 `user_id` 获取发送者资料；
- 生成 UUID 消息 ID 和当前时间戳；
- 将请求内容组装成完整 `MessageInfo`；
- 查询会话成员、校验发送者属于该会话并对成员去重；
- 先将完整消息投递给 MessageStoreServer；
- 投递成功后返回消息和 WebSocket 推送目标列表。

```text
NewMessageReq
    |
    v
MessageTransmitServer
    |-- UserClient -----------------> 发送者资料
    |-- SessionMemberRepository ----> 会话成员
    |-- MessagePublisher -----------> MessageStoreServer
    v
GetTransmitTargetRsp
    |-- MessageInfo
    `-- target_id_list
```

当前独立联调实现：

- `BrpcUserClient`：直连 UserServer 获取完整发送者资料；
- `BrpcFriendSessionMemberRepository`：从 FriendServer 获取真实会话成员；
- `BrpcMessagePublisher`：直连 MessageStoreServer 的内部投递 RPC。

这三个依赖都已抽象成接口。后续可切换为服务发现信道和 RabbitMQ publisher，
不需修改 `TransmitServiceImpl`。

## 已完成：UserServer（独立阶段）

UserServer 已实现 V1 的 11 个 RPC：

- 用户名注册、用户名登录；
- 获取手机验证码、手机号注册、手机号登录；
- 获取当前用户资料、批量获取用户资料；
- 修改头像、昵称、签名和手机号。

另外提供 `ResolveSession` 和 `RevokeSession` 两个内部 RPC，供后续 Gateway
完成鉴权和断开登录状态。V1 的协议字段号及请求语义保持不变，便于后续 Qt 端对接。
`SearchUsers` 是 FriendServer 使用的内部搜索 RPC。

```text
UserService RPC
    |-- UserStore ----------------------> v2/data/user/users.bin
    |-- SessionManager -----------------> 登录会话（带过期时间）
    |-- VerificationCodeManager --------> 一次性手机验证码
    `-- BrpcAvatarFileClient -----------> FileServer
```

与 V1 相比，当前实现有三项安全修正：

- 密码使用随机盐和 PBKDF2-HMAC-SHA256 保存，不写入明文密码；
- 验证码绑定手机号码、5 分钟过期且成功使用后立即作废；
- 昵称和手机号在注册及修改时都检查唯一性。

当前独立阶段不接入阿里云付费短信 SDK。开发验证码会打印到 UserServer
日志；`VerificationCodeSender` 已抽象成接口，最后接短信平台时只替换发送器。
用户资料持久化到本地追加日志，会话和验证码暂存内存；基础设施阶段可分别替换为
MySQL 和 Redis，不需要修改 RPC 协议。

## 已完成：FriendServer（独立阶段）

FriendServer 已实现 V1 的 9 个 RPC：

- 获取好友列表、删除好友；
- 发起、处理和查询好友申请；
- 搜索非好友用户；
- 获取会话列表、创建群聊、获取会话成员。

另外提供内部 `GetChatSessionMemberIds` RPC，供 MessageTransmitServer 查询推送
目标，避免为取得成员 ID 下载所有头像。

```text
FriendService RPC
    |-- FriendStore --------------------> v2/data/friend/friends.bin
    |-- BrpcUserDirectory -------------> UserServer
    `-- BrpcRecentMessageClient --------> MessageStoreServer

MessageTransmitServer
    `-- GetChatSessionMemberIds --------> FriendServer
```

好友关系、申请、会话和会话成员保存在同一份可恢复的事务快照中。同意好友申请时，
“删除申请、建立双向好友关系、创建单聊会话”会一次性提交，不会出现只成功一半的
数据。好友申请会严格匹配事件 ID 和双方身份；群聊创建会检查创建者、成员存在性和
成员去重。当前本地仓库后续可以替换为 MySQL，而不修改 FriendService RPC。

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

### MessageTransmitServer RPC 联调

启动 FileServer、UserServer、MessageStoreServer 和 FriendServer 后，再启动
MessageTransmitServer：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/transmit/transmit_server
```

发送文本消息：

```bash
./v2/build/services/transmit/transmit_client \
  --user_id=user-1 \
  --chat_session_id=demo-session \
  --type=text \
  --content=你好
```

发送真实文件：

```bash
./v2/build/services/transmit/transmit_client \
  --user_id=user-1 \
  --chat_session_id=demo-session \
  --type=file \
  --file_path=./v2/README.md
```

MessageTransmitServer 默认配置：

- RPC 地址：`127.0.0.1:10004`；
- UserServer 地址：`127.0.0.1:10003`；
- FriendServer 地址：`127.0.0.1:10006`；
- MessageStoreServer 地址：`127.0.0.1:10005`；

### UserServer RPC 联调

先启动 FileServer，再启动 UserServer：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/user/user_server
```

用户名注册和登录：

```bash
./v2/build/services/user/user_client \
  --operation=register --nickname=alice --password=abc123

./v2/build/services/user/user_client \
  --operation=login --nickname=alice --password=abc123
```

登录成功会输出 `session_id`。只用会话即可获取当前用户资料：

```bash
./v2/build/services/user/user_client \
  --operation=info --session_id=<上一步返回的 session_id>
```

手机验证码联调：

```bash
./v2/build/services/user/user_client \
  --operation=code --phone_number=13800000000
```

客户端会输出 `verify_code_id`，四位验证码在 UserServer 运行日志中。随后可调用：

```bash
./v2/build/services/user/user_client \
  --operation=phone_register \
  --phone_number=13800000000 \
  --verify_code_id=<验证码 ID> \
  --verify_code=<日志中的四位验证码>
```

UserServer 默认配置：

- RPC 地址：`127.0.0.1:10003`；
- FileServer 地址：`127.0.0.1:10002`；
- 用户数据：`v2/data/user/users.bin`；
- 验证码有效期：300 秒；
- 登录会话有效期：86400 秒。

### FriendServer RPC 联调

先启动 FileServer、UserServer 和 MessageStoreServer，再启动 FriendServer：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/friend/friend_server
```

用户 A 发起好友申请：

```bash
./v2/build/services/friend/friend_client \
  --operation=add \
  --session_id=<用户 A 的登录会话> \
  --peer_id=<用户 B 的 user_id>
```

命令返回 `event_id`。用户 B 查询并同意申请：

```bash
./v2/build/services/friend/friend_client \
  --operation=pending \
  --session_id=<用户 B 的登录会话>

./v2/build/services/friend/friend_client \
  --operation=process \
  --session_id=<用户 B 的登录会话> \
  --peer_id=<用户 A 的 user_id> \
  --event_id=<好友申请 event_id> \
  --agree=true
```

同意后会返回自动创建的单聊 `chat_session_id`。群聊创建示例：

```bash
./v2/build/services/friend/friend_client \
  --operation=create \
  --session_id=<创建者登录会话> \
  --chat_session_name=开发群 \
  --members=<创建者 user_id>,<成员 user_id>
```

FriendServer 默认配置：

- RPC 地址：`127.0.0.1:10006`；
- UserServer 地址：`127.0.0.1:10003`；
- MessageStoreServer 地址：`127.0.0.1:10005`；
- 好友、申请和会话数据：`v2/data/friend/friends.bin`。

## 已完成：GatewayServer

GatewayServer 已覆盖 V1 实际注册的 28 条 HTTP POST 路由，并保留 V1 Qt 客户端
使用的路径、Protobuf 字段号和响应类型。默认监听：

- HTTP：`0.0.0.0:9000`；
- WebSocket：`0.0.0.0:9001`。

HTTP 请求和响应正文都是序列化后的 Protobuf。登录、注册和获取手机验证码可直接
访问；其余接口先调用 UserServer 的 `ResolveSession`，然后用会话对应的真实
`user_id` 覆盖客户端字段，客户端不能通过伪造 `user_id` 越权。

```text
Qt / gateway_client
    |-- HTTP + Protobuf ----------> GatewayCore
    |                                  |-- UserServer
    |                                  |-- FriendServer
    |                                  |-- MessageStoreServer
    |                                  |-- MessageTransmitServer
    |                                  |-- FileServer
    |                                  `-- SpeechServer（可选）
    |
    `-- WebSocket 二进制帧 -------> 在线连接表
                                       `-- NotifyMessage 推送
```

WebSocket 建连后的第一条二进制消息必须是 `ClientAuthenticationReq`。鉴权成功后，
Gateway 会按用户维护唯一在线连接，并推送五类 V1 通知：好友申请、好友申请处理、
会话创建、新消息和删除好友。协议层支持标准 Upgrade、客户端掩码、二进制帧、
ping/pong 和 close。旧连接被同一用户的新连接替换时，不会误删新连接或误注销新
会话。

V2 没有新增 websocketpp/Boost 运行依赖。WebSocket 传输层使用 POSIX socket 和
OpenSSL 完成 RFC 6455 所需功能；HTTP 复用仓库中 V1 已包含的免费
`cpp-httplib` 头文件。HTTP 请求体上限默认 256 MiB，可用
`--max_http_body_mb` 调整。

语音路由 `/service/speech/recognition` 和协议已经保留。默认不配置 SpeechServer
地址时，它返回结构化的“下游服务未配置”错误；以后接入语音服务时只需传入
`--speech_server`，不影响 Gateway 或 Qt 协议。

### GatewayServer 全链路联调

按以下顺序分别启动六个进程：

```bash
cd /root/Desktop/chatSystem
./v2/build/services/file/file_server
./v2/build/services/message/message_server
./v2/build/services/user/user_server
./v2/build/services/friend/friend_server
./v2/build/services/transmit/transmit_server
./v2/build/services/gateway/gateway_server
```

每条命令应在独立终端运行。随后执行：

```bash
./v2/build/services/gateway/gateway_client --operation=smoke
```

冒烟客户端会通过真实 Gateway 完成：

1. 注册并登录两个用户；
2. 为两个用户建立并鉴权 WebSocket；
3. 发起和同意好友申请，并校验双方推送；
4. 创建群聊，并校验双方会话创建推送；
5. 发送、推送并查询一条文本消息；
6. 上传和下载一份包含 `NUL`、`0xff` 的真实二进制文件。

Gateway 可用以下参数覆盖下游地址：

```bash
./v2/build/services/gateway/gateway_server \
  --http_port=9000 \
  --websocket_port=9001 \
  --user_server=127.0.0.1:10003 \
  --friend_server=127.0.0.1:10006 \
  --message_server=127.0.0.1:10005 \
  --transmit_server=127.0.0.1:10004 \
  --file_server=127.0.0.1:10002
```

Gateway 的核心转发和通知组合逻辑与 HTTP/WebSocket 真实传输层分别有自动化测试。
整个工程当前共 35 项测试。

## 已完成：Qt 桌面端

Qt 客户端使用 Qt 5.12 Widgets 开发，保持 V1 的 Protobuf + HTTP/WebSocket
通信方式，并直接复用 V1 的图片资源。已经实现：

- 用户名和手机号注册、登录；
- 个人资料查看与头像、昵称、签名、手机号修改；
- 好友搜索、申请、同意/拒绝、删除；
- 单聊会话、群聊创建和成员查看；
- 文本、图片、真实文件发送与下载；
- 最近消息、时间范围历史消息和关键字搜索；
- WebSocket 鉴权，以及好友、会话、消息实时通知和未读数。

语音按钮暂时禁用，对应尚未启用的可选 SpeechServer。图片和文件目前与 V1
一样一次性读入内存，超大文件的分片传输放在最后的基础设施增强阶段。

Ubuntu 构建依赖：

```bash
sudo apt install qtbase5-dev libqt5websockets5-dev xvfb openbox x11vnc novnc websockify
cmake -S v2 -B v2/build -DCMAKE_BUILD_TYPE=Debug -DBUILD_QT_CLIENT=ON
cmake --build v2/build -j2 --target chat_desktop
```

本机有 Linux 图形桌面时可以直接启动：

```bash
./v2/build/client/qt/chat_desktop \
  --http-url=http://127.0.0.1:9000 \
  --ws-url=ws://127.0.0.1:9001
```

通过 VSCode SSH 连接服务器时，仓库提供独立的 Xvfb + Openbox + x11vnc
开发桌面。服务端执行：

```bash
./v2/client/qt/scripts/start_remote_desktop.sh
```

最方便的查看方式是在 VSCode 的“端口”面板转发 `6080`，然后在本机浏览器打开：

```text
http://127.0.0.1:6080/vnc.html?autoconnect=1&resize=scale
```

也可以在自己的电脑上另开一个终端建立 VNC 的 SSH 隧道（替换登录信息）：

```bash
ssh -L 5901:127.0.0.1:5901 <服务器用户>@<服务器地址>
```

随后用任意 VNC Viewer 连接 `127.0.0.1:5901`。noVNC 的 6080 和 VNC 的
5901 都只监听服务器本机且不设额外密码，访问由 VSCode/SSH 端口转发保护，不能
把这两个端口直接开放到公网。停止桌面：

```bash
./v2/client/qt/scripts/stop_remote_desktop.sh
```

可用 `CHAT_QT_DISPLAY`、`CHAT_QT_SCREEN`、`CHAT_QT_VNC_PORT` 和
`CHAT_QT_NOVNC_PORT` 修改默认的 `:99`、`1440x900x24`、`5901` 和 `6080`。
进程日志保存在 `/tmp/ahwei-chat-v2-display-99`。
没有图形环境时也可以执行离屏启动测试：

```bash
QT_QPA_PLATFORM=offscreen ./v2/build/client/qt/chat_desktop --ui-smoke
```

## 当前依赖

- C++17
- CMake
- Protobuf
- brpc 1.10.0
- gflags
- spdlog
- GoogleTest
- OpenSSL（WebSocket 握手）
- cpp-httplib（仓库已有的单头文件）
- Qt 5.12 Widgets、Network、WebSockets（桌面端）

构建环境已经安装完成。后续服务用到 Redis、RabbitMQ、MySQL、Elasticsearch、etcd 时再逐项增加，不提前把所有中间件混在一起。
