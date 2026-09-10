#pragma once

#include "friend/dependencies.hpp"
#include "friend/friend_store.hpp"
#include "friend.pb.h"

#include <functional>
#include <memory>
#include <string>

namespace chat::friend_service {

class FriendServiceImpl final : public ahwei_im::FriendService {
public:
    using IdGenerator = std::function<std::string()>;

    FriendServiceImpl(
        std::shared_ptr<FriendRepository> repository,
        std::shared_ptr<UserDirectory> users,
        std::shared_ptr<RecentMessageClient> messages,
        IdGenerator id_generator = {});

    void GetFriendList(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetFriendListReq* request,
        ahwei_im::GetFriendListRsp* response,
        google::protobuf::Closure* done) override;
    void FriendRemove(
        google::protobuf::RpcController* controller,
        const ahwei_im::FriendRemoveReq* request,
        ahwei_im::FriendRemoveRsp* response,
        google::protobuf::Closure* done) override;
    void FriendAdd(
        google::protobuf::RpcController* controller,
        const ahwei_im::FriendAddReq* request,
        ahwei_im::FriendAddRsp* response,
        google::protobuf::Closure* done) override;
    void FriendAddProcess(
        google::protobuf::RpcController* controller,
        const ahwei_im::FriendAddProcessReq* request,
        ahwei_im::FriendAddProcessRsp* response,
        google::protobuf::Closure* done) override;
    void FriendSearch(
        google::protobuf::RpcController* controller,
        const ahwei_im::FriendSearchReq* request,
        ahwei_im::FriendSearchRsp* response,
        google::protobuf::Closure* done) override;
    void GetChatSessionList(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetChatSessionListReq* request,
        ahwei_im::GetChatSessionListRsp* response,
        google::protobuf::Closure* done) override;
    void ChatSessionCreate(
        google::protobuf::RpcController* controller,
        const ahwei_im::ChatSessionCreateReq* request,
        ahwei_im::ChatSessionCreateRsp* response,
        google::protobuf::Closure* done) override;
    void GetChatSessionMember(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetChatSessionMemberReq* request,
        ahwei_im::GetChatSessionMemberRsp* response,
        google::protobuf::Closure* done) override;
    void GetPendingFriendEventList(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetPendingFriendEventListReq* request,
        ahwei_im::GetPendingFriendEventListRsp* response,
        google::protobuf::Closure* done) override;
    void GetChatSessionMemberIds(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetChatSessionMemberIdsReq* request,
        ahwei_im::GetChatSessionMemberIdsRsp* response,
        google::protobuf::Closure* done) override;

private:
    bool resolve_actor(
        const std::string& request_id,
        const std::string& requested_user_id,
        const std::string& session_id,
        std::string& user_id,
        std::string& error);

    std::shared_ptr<FriendRepository> repository_;
    std::shared_ptr<UserDirectory> users_;
    std::shared_ptr<RecentMessageClient> messages_;
    IdGenerator id_generator_;
};

}  // namespace chat::friend_service
