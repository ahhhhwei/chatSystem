#include "user/avatar_file_client.hpp"

#include "file.pb.h"

#include <brpc/controller.h>

#include <stdexcept>
#include <unordered_set>

namespace chat::user {

BrpcAvatarFileClient::BrpcAvatarFileClient(
    std::string server_address,
    std::int32_t timeout_ms) {
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeout_ms;
    options.max_retry = 3;
    if (channel_.Init(server_address.c_str(), &options) != 0) {
        throw std::runtime_error(
            "cannot initialize FileServer channel: " + server_address);
    }
}

bool BrpcAvatarFileClient::put(
    const std::string& request_id,
    const std::string& content,
    std::string& file_id,
    std::string& error) {
    file_id.clear();
    error.clear();

    ahwei_im::PutSingleFileReq request;
    request.set_request_id(request_id);
    request.mutable_file_data()->set_file_name("");
    request.mutable_file_data()->set_file_size(
        static_cast<std::int64_t>(content.size()));
    request.mutable_file_data()->set_file_content(content);

    ahwei_im::PutSingleFileRsp response;
    brpc::Controller controller;
    ahwei_im::FileService_Stub stub(&channel_);
    stub.PutSingleFile(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    if (response.file_info().file_id().empty()) {
        error = "FileServer returned an empty file_id";
        return false;
    }
    file_id = response.file_info().file_id();
    return true;
}

bool BrpcAvatarFileClient::get_multi(
    const std::string& request_id,
    const std::vector<std::string>& file_ids,
    std::unordered_map<std::string, std::string>& files,
    std::string& error) {
    files.clear();
    error.clear();
    if (file_ids.empty()) {
        return true;
    }

    ahwei_im::GetMultiFileReq request;
    request.set_request_id(request_id);
    std::unordered_set<std::string> unique_ids;
    for (const auto& file_id : file_ids) {
        if (unique_ids.insert(file_id).second) {
            request.add_file_id_list(file_id);
        }
    }

    ahwei_im::GetMultiFileRsp response;
    brpc::Controller controller;
    ahwei_im::FileService_Stub stub(&channel_);
    stub.GetMultiFile(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    for (const auto& item : response.file_data()) {
        files.emplace(item.first, item.second.file_content());
    }
    for (const auto& file_id : unique_ids) {
        if (files.find(file_id) == files.end()) {
            files.clear();
            error = "FileServer did not return file_id: " + file_id;
            return false;
        }
    }
    return true;
}

}  // namespace chat::user
