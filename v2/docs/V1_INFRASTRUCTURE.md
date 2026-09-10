# V1 基础设施兼容模式

V2 现在保留两种运行方式：默认 `local` 适合逐个服务学习和单元测试；
`--infrastructure_mode=v1` 使用与 V1 相同的基础设施职责和 etcd 路径。

| 组件 | V2 中的职责 | 本机地址 |
|---|---|---|
| MySQL | 用户、好友申请、好友关系、会话、成员、消息元数据 | `127.0.0.1:3306` |
| Redis | 登录会话、一次性验证码、WebSocket 在线状态 | `127.0.0.1:6379` |
| etcd | `/service/<service>/...` 注册、TTL 保活、逐请求发现 | `127.0.0.1:2379` |
| RabbitMQ | `msg_exchange` → `msg_queue` 异步消息持久化 | `127.0.0.1:5672` |
| Elasticsearch 7.17.21 | 用户和会话内文本消息搜索 | `127.0.0.1:9200` |

所有中间件默认只监听本机，避免把开发数据库和消息队列暴露到公网。

## 第一次安装

在仓库根目录执行：

```bash
sudo ./v2/infrastructure/scripts/install_v1_infrastructure.sh
cmake -S v2 -B v2/build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build v2/build -j2
```

安装脚本会创建开发数据库 `ahwei_chat` 和用户 `chat`。默认开发密码是
`chat_dev_only`；可在执行 bootstrap 前通过 `CHAT_MYSQL_PASSWORD` 修改。

## 启动完整后端

```bash
./v2/infrastructure/scripts/start_v1_stack.sh
./v2/infrastructure/scripts/status_v1_infrastructure.sh
```

启动顺序由脚本处理：File → User → Message → Friend → Transmit → Gateway。
日志位于 `v2/run/v1/logs/`。停止业务进程（不会删除数据、不会停止中间件）：

```bash
./v2/infrastructure/scripts/stop_v1_stack.sh
```

## 启动 Qt 桌面端

服务器有图形桌面或已经启动 VNC/noVNC 时：

```bash
./v2/build/client/qt/chat_desktop \
  --http-url=http://127.0.0.1:9000 \
  --ws-url=ws://127.0.0.1:9001
```

如果 Qt 参数由界面配置，则直接运行 `./v2/build/client/qt/chat_desktop`，
并在登录页保持默认 Gateway 地址即可。

## 验证

不依赖中间件的回归测试：

```bash
cd v2/build && ctest --output-on-failure
```

真实中间件集成测试：

```bash
CHAT_RUN_INFRA_TESTS=1 ./v2/build/infrastructure_integration_test
```

集成测试使用带随机 UUID 的数据，成功后只清理自己创建的记录，不会清空已有业务表。
