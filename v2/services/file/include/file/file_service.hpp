#pragma once

#include "file/file_store.hpp"
#include "file.pb.h"

#include <filesystem>

namespace chat::file {

// 继承 protobuf 生成的那个接口
class FileServiceImpl final /* final：不允许再继承这个类 */: public ahwei_im::FileService { 
public:
    explicit FileServiceImpl(std::filesystem::path storage_path);

    void GetSingleFile(
        google::protobuf::RpcController* controller, // 当前这一次 rpc 调用的控制对象，可以携带一些 rpc 相关信息：错误状态、超时、连接信息、附件等
        const ahwei_im::GetSingleFileReq* request, 
        ahwei_im::GetSingleFileRsp* response, // proto里的 returns(...)
        google::protobuf::Closure* done) override; // override 检查其是否是基类虚函数的实现
        // 通常最终需要：done->Run(); 告诉 rpc 框架这次 rpc 已经处理完了

    void GetMultiFile(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetMultiFileReq* request,
        ahwei_im::GetMultiFileRsp* response,
        google::protobuf::Closure* done) override;

    void PutSingleFile(
        google::protobuf::RpcController* controller,
        const ahwei_im::PutSingleFileReq* request,
        ahwei_im::PutSingleFileRsp* response,
        google::protobuf::Closure* done) override;

    void PutMultiFile(
        google::protobuf::RpcController* controller,
        const ahwei_im::PutMultiFileReq* request,
        ahwei_im::PutMultiFileRsp* response,
        google::protobuf::Closure* done) override;

private:
    FileStore store_;
};

}  // namespace chat::file
