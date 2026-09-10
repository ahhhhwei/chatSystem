#pragma once

#include "message.pb.h"
#include "message/file_client.hpp"
#include "message/message_store.hpp"

#include <memory>
#include <string>
#include <vector>

namespace chat::message {

class MessageServiceImpl final : public ahwei_im::MsgStorageService {
public:
    MessageServiceImpl(
        std::shared_ptr<MessageRepository> repository, // 消息存储，负责保存消息/查询消息
        std::shared_ptr<FileClient> file_client);      // 文件客户端，负责上传/下载文件

    // 与 v1 的 RabbitMQ 消费回调 onMessage 对应。后续接入 MQ 时可直接
    // 把消费到的 MessageInfo 交给这两个接口。
    bool store_message(ahwei_im::MessageInfo message, std::string& error);
    bool on_message(const void* data, std::size_t size, std::string& error);

    void GetHistoryMsg(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetHistoryMsgReq* request,
        ahwei_im::GetHistoryMsgRsp* response,
        google::protobuf::Closure* done) override;

    void GetRecentMsg(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetRecentMsgReq* request,
        ahwei_im::GetRecentMsgRsp* response,
        google::protobuf::Closure* done) override;

    void MsgSearch(
        google::protobuf::RpcController* controller,
        const ahwei_im::MsgSearchReq* request,
        ahwei_im::MsgSearchRsp* response,
        google::protobuf::Closure* done) override;

    void StoreMessage(
        google::protobuf::RpcController* controller,
        const ahwei_im::StoreMessageReq* request,
        ahwei_im::StoreMessageRsp* response,
        google::protobuf::Closure* done) override;

private:
    bool hydrate_file_contents(
        const std::string& request_id,
        std::vector<ahwei_im::MessageInfo>& messages,
        std::string& error) const;

    std::shared_ptr<MessageRepository> repository_;
    std::shared_ptr<FileClient> file_client_;
};

}  // namespace chat::message
