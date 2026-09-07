#include "file/file_service.hpp"

#include <brpc/closure_guard.h>
#include <spdlog/spdlog.h>

#include <string>
#include <utility>

namespace chat::file {

FileServiceImpl::FileServiceImpl(std::filesystem::path storage_path)
    : store_(std::move(storage_path)) {}

void FileServiceImpl::GetSingleFile(
    google::protobuf::RpcController*,
    const ahwei_im::GetSingleFileReq* request,
    ahwei_im::GetSingleFileRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    response->set_request_id(request->request_id());

    std::string content;
    std::string error;
    if (!store_.get(request->file_id(), content, error)) {
        response->set_success(false);
        response->set_errmsg(error);
        spdlog::warn("get file {} failed: {}", request->file_id(), error);
        return;
    }

    response->set_success(true);
    response->mutable_file_data()->set_file_id(request->file_id());
    response->mutable_file_data()->set_file_content(std::move(content));
}

void FileServiceImpl::GetMultiFile(
    google::protobuf::RpcController*,
    const ahwei_im::GetMultiFileReq* request,
    ahwei_im::GetMultiFileRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    response->set_request_id(request->request_id());

    for (const auto& file_id : request->file_id_list()) {
        std::string content;
        std::string error;
        if (!store_.get(file_id, content, error)) {
            response->set_success(false);
            response->set_errmsg(error);
            response->clear_file_data();
            spdlog::warn("get file {} failed: {}", file_id, error);
            return;
        }

        auto& data = (*response->mutable_file_data())[file_id];
        data.set_file_id(file_id);
        data.set_file_content(std::move(content));
    }

    response->set_success(true);
}

void FileServiceImpl::PutSingleFile(
    google::protobuf::RpcController*,
    const ahwei_im::PutSingleFileReq* request,
    ahwei_im::PutSingleFileRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    response->set_request_id(request->request_id());

    std::string error;
    const auto file_id = store_.put(
        request->file_data().file_content(),
        error);
    if (!file_id) {
        response->set_success(false);
        response->set_errmsg(error);
        spdlog::error("put file failed: {}", error);
        return;
    }

    response->set_success(true);
    response->mutable_file_info()->set_file_id(*file_id);
    response->mutable_file_info()->set_file_name(
        request->file_data().file_name());
    response->mutable_file_info()->set_file_size(
        request->file_data().file_content().size());
}

void FileServiceImpl::PutMultiFile(
    google::protobuf::RpcController*,
    const ahwei_im::PutMultiFileReq* request,
    ahwei_im::PutMultiFileRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    response->set_request_id(request->request_id());

    for (const auto& file_data : request->file_data()) {
        std::string error;
        const auto file_id = store_.put(file_data.file_content(), error);
        if (!file_id) {
            response->set_success(false);
            response->set_errmsg(error);
            response->clear_file_info();
            spdlog::error("put file failed: {}", error);
            return;
        }

        auto* file_info = response->add_file_info();
        file_info->set_file_id(*file_id);
        file_info->set_file_name(file_data.file_name());
        file_info->set_file_size(file_data.file_content().size());
    }

    response->set_success(true);
}

}  // namespace chat::file
