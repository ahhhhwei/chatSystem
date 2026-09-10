#include "friend/friend_service.hpp"

#include "user/security.hpp"

#include <brpc/closure_guard.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chat::friend_service {
namespace {

template <typename Response>
void fail(
    Response* response,
    const std::string& request_id,
    const std::string& error) {
    response->set_request_id(request_id);
    response->set_success(false);
    response->set_errmsg(error);
}

template <typename Response>
void succeed(Response* response, const std::string& request_id) {
    response->set_request_id(request_id);
    response->set_success(true);
    response->clear_errmsg();
}

}  // namespace

FriendServiceImpl::FriendServiceImpl(
    std::shared_ptr<FriendRepository> repository,
    std::shared_ptr<UserDirectory> users,
    std::shared_ptr<RecentMessageClient> messages,
    IdGenerator id_generator)
    : repository_(std::move(repository)),
      users_(std::move(users)),
      messages_(std::move(messages)),
      id_generator_(
          id_generator ? std::move(id_generator) : chat::user::make_random_id) {
    if (!repository_ || !users_ || !messages_) {
        throw std::invalid_argument("FriendService dependencies cannot be null");
    }
}

void FriendServiceImpl::GetFriendList(
    google::protobuf::RpcController*,
    const ahwei_im::GetFriendListReq* request,
    ahwei_im::GetFriendListRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    const auto friend_ids = repository_->friends(user_id);
    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    if (!users_->get_multi(request->request_id(), friend_ids, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& friend_id : friend_ids) {
        response->add_friend_list()->CopyFrom(users.at(friend_id));
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::FriendRemove(
    google::protobuf::RpcController*,
    const ahwei_im::FriendRemoveReq* request,
    ahwei_im::FriendRemoveRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    if (request->peer_id().empty() || request->peer_id() == user_id) {
        fail(response, request->request_id(), "invalid peer_id");
        return;
    }
    if (!repository_->remove_friend(user_id, request->peer_id(), error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::FriendAdd(
    google::protobuf::RpcController*,
    const ahwei_im::FriendAddReq* request,
    ahwei_im::FriendAddRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    if (request->respondent_id().empty() ||
        request->respondent_id() == user_id) {
        fail(response, request->request_id(), "invalid respondent_id");
        return;
    }
    std::unordered_map<std::string, ahwei_im::UserInfo> found_users;
    if (!users_->get_multi(
            request->request_id(),
            {user_id, request->respondent_id()},
            found_users,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }

    std::string event_id;
    try {
        event_id = id_generator_();
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    if (event_id.empty() || !repository_->add_application(
            event_id,
            user_id,
            request->respondent_id(),
            error)) {
        if (error.empty()) {
            error = "cannot generate friend event_id";
        }
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
    response->set_notify_event_id(std::move(event_id));
}

void FriendServiceImpl::FriendAddProcess(
    google::protobuf::RpcController*,
    const ahwei_im::FriendAddProcessReq* request,
    ahwei_im::FriendAddProcessRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string respondent_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            respondent_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    if (request->notify_event_id().empty() ||
        request->apply_user_id().empty() ||
        request->apply_user_id() == respondent_id) {
        fail(response, request->request_id(), "invalid friend application");
        return;
    }

    std::string new_session_id;
    if (request->agree()) {
        try {
            new_session_id = id_generator_();
        } catch (const std::exception& exception) {
            fail(response, request->request_id(), exception.what());
            return;
        }
    }
    if (!repository_->process_application(
            request->notify_event_id(),
            request->apply_user_id(),
            respondent_id,
            request->agree(),
            new_session_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
    if (request->agree()) {
        response->set_new_session_id(std::move(new_session_id));
    }
}

void FriendServiceImpl::FriendSearch(
    google::protobuf::RpcController*,
    const ahwei_im::FriendSearchReq* request,
    ahwei_im::FriendSearchRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    if (request->search_key().size() > 128) {
        fail(response, request->request_id(), "search key is too long");
        return;
    }
    auto excluded = repository_->friends(user_id);
    excluded.push_back(user_id);
    std::vector<ahwei_im::UserInfo> users;
    if (!users_->search(
            request->request_id(),
            request->search_key(),
            excluded,
            users,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& user : users) {
        response->add_user_info()->CopyFrom(user);
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::GetPendingFriendEventList(
    google::protobuf::RpcController*,
    const ahwei_im::GetPendingFriendEventListReq* request,
    ahwei_im::GetPendingFriendEventListRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    const auto applications = repository_->pending_applications(user_id);
    std::vector<std::string> applicant_ids;
    for (const auto& application : applications) {
        applicant_ids.push_back(application.applicant_id());
    }
    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    if (!users_->get_multi(
            request->request_id(), applicant_ids, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& application : applications) {
        auto* event = response->add_event();
        event->set_event_id(application.event_id());
        event->mutable_sender()->CopyFrom(users.at(application.applicant_id()));
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::GetChatSessionList(
    google::protobuf::RpcController*,
    const ahwei_im::GetChatSessionListReq* request,
    ahwei_im::GetChatSessionListRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    const auto sessions = repository_->sessions_for(user_id);
    std::vector<std::string> single_peer_ids;
    std::unordered_map<std::string, std::string> single_peers;
    for (const auto& session : sessions) {
        if (session.type() != ahwei_im::internal::SINGLE_SESSION) {
            continue;
        }
        for (const auto& member_id : session.member_ids()) {
            if (member_id != user_id) {
                single_peers[session.chat_session_id()] = member_id;
                single_peer_ids.push_back(member_id);
                break;
            }
        }
    }
    std::unordered_map<std::string, ahwei_im::UserInfo> peer_users;
    if (!users_->get_multi(
            request->request_id(), single_peer_ids, peer_users, error)) {
        fail(response, request->request_id(), error);
        return;
    }

    for (const auto& session : sessions) {
        auto* info = response->add_chat_session_info_list();
        info->set_chat_session_id(session.chat_session_id());
        if (session.type() == ahwei_im::internal::SINGLE_SESSION) {
            const auto peer_id = single_peers.find(session.chat_session_id());
            if (peer_id == single_peers.end()) {
                fail(response, request->request_id(), "invalid single chat session");
                response->clear_chat_session_info_list();
                return;
            }
            const auto& peer = peer_users.at(peer_id->second);
            info->set_single_chat_friend_id(peer.user_id());
            info->set_chat_session_name(peer.nickname());
            info->set_avatar(peer.avatar());
        } else {
            info->set_chat_session_name(session.chat_session_name());
        }

        std::optional<ahwei_im::MessageInfo> latest;
        std::string latest_error;
        if (messages_->latest(
                request->request_id(),
                session.chat_session_id(),
                latest,
                latest_error) && latest) {
            info->mutable_prev_message()->CopyFrom(*latest);
        } else if (!latest_error.empty()) {
            spdlog::warn(
                "cannot load latest message for session {}: {}",
                session.chat_session_id(),
                latest_error);
        }
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::ChatSessionCreate(
    google::protobuf::RpcController*,
    const ahwei_im::ChatSessionCreateReq* request,
    ahwei_im::ChatSessionCreateRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    if (request->chat_session_name().empty() ||
        request->chat_session_name().size() > 128) {
        fail(response, request->request_id(), "invalid chat session name");
        return;
    }
    std::vector<std::string> members;
    std::unordered_set<std::string> unique_members;
    for (const auto& member_id : request->member_id_list()) {
        if (member_id.empty()) {
            fail(response, request->request_id(), "member_id cannot be empty");
            return;
        }
        if (unique_members.insert(member_id).second) {
            members.push_back(member_id);
        }
    }
    if (unique_members.find(user_id) == unique_members.end()) {
        fail(response, request->request_id(), "creator must be a chat member");
        return;
    }
    if (members.size() < 2) {
        fail(response, request->request_id(), "group chat needs at least two members");
        return;
    }
    std::unordered_map<std::string, ahwei_im::UserInfo> found_users;
    if (!users_->get_multi(
            request->request_id(), members, found_users, error)) {
        fail(response, request->request_id(), error);
        return;
    }

    std::string chat_session_id;
    try {
        chat_session_id = id_generator_();
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    if (chat_session_id.empty() || !repository_->create_group(
            chat_session_id,
            request->chat_session_name(),
            members,
            error)) {
        if (error.empty()) {
            error = "cannot generate chat_session_id";
        }
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
    auto* info = response->mutable_chat_session_info();
    info->set_chat_session_id(std::move(chat_session_id));
    info->set_chat_session_name(request->chat_session_name());
}

void FriendServiceImpl::GetChatSessionMember(
    google::protobuf::RpcController*,
    const ahwei_im::GetChatSessionMemberReq* request,
    ahwei_im::GetChatSessionMemberRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_actor(
            request->request_id(),
            request->user_id(),
            request->session_id(),
            user_id,
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    const auto session = repository_->session(request->chat_session_id());
    if (!session) {
        fail(response, request->request_id(), "chat session not found");
        return;
    }
    if (!repository_->is_member(request->chat_session_id(), user_id)) {
        fail(response, request->request_id(), "user is not a chat session member");
        return;
    }
    const std::vector<std::string> members(
        session->member_ids().begin(), session->member_ids().end());
    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    if (!users_->get_multi(request->request_id(), members, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& member_id : members) {
        response->add_member_info_list()->CopyFrom(users.at(member_id));
    }
    succeed(response, request->request_id());
}

void FriendServiceImpl::GetChatSessionMemberIds(
    google::protobuf::RpcController*,
    const ahwei_im::GetChatSessionMemberIdsReq* request,
    ahwei_im::GetChatSessionMemberIdsRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    const auto session = repository_->session(request->chat_session_id());
    if (!session) {
        fail(response, request->request_id(), "chat session not found");
        return;
    }
    for (const auto& member_id : session->member_ids()) {
        response->add_member_id_list(member_id);
    }
    succeed(response, request->request_id());
}

bool FriendServiceImpl::resolve_actor(
    const std::string& request_id,
    const std::string& requested_user_id,
    const std::string& session_id,
    std::string& user_id,
    std::string& error) {
    user_id.clear();
    error.clear();
    if (session_id.empty()) {
        if (requested_user_id.empty()) {
            error = "user_id or session_id is required";
            return false;
        }
        user_id = requested_user_id;
        return true;
    }
    if (!users_->resolve_session(request_id, session_id, user_id, error)) {
        return false;
    }
    if (!requested_user_id.empty() && requested_user_id != user_id) {
        user_id.clear();
        error = "session does not belong to user_id";
        return false;
    }
    return true;
}

}  // namespace chat::friend_service
