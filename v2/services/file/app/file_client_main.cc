#include "file.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <iostream>
#include <string>

DEFINE_string(server, "127.0.0.1:10002", "FileServer address");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in milliseconds");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    // 客户端到服务器的 rpc 通信通道
    brpc::Channel channel;
    // 客户端配置
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";         // 使用 brpc 的 baidu_std 协议
    options.timeout_ms = FLAGS_timeout_ms;  // 1s rpc 超过1秒还没有完成，就认为超时
    options.max_retry = 3;                  // 表示失败后最多尝试重试
    // 连接服务器
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "cannot connect to " << FLAGS_server << '\n';
        return 1;
    }

    // FileService_Stub：客户端代理
    ahwei_im::FileService_Stub stub(&channel);
    const std::string original = "hello from FileServer\n";

    // 准备 req
    ahwei_im::PutSingleFileReq put_request;
    put_request.set_request_id("file-roundtrip-put");
    put_request.mutable_file_data()->set_file_name("hello.txt");
    put_request.mutable_file_data()->set_file_size(original.size());
    put_request.mutable_file_data()->set_file_content(original);

    // 准备 res
    ahwei_im::PutSingleFileRsp put_response;
    // 这一次 PutSingleFile RPC 调用的状态。
    brpc::Controller put_controller;
    // stub.PutSingleFile()
    //         ↓
    // protobuf 序列化 put_request
    //         ↓
    // brpc::Channel
    //         ↓
    //       网络
    //         ↓
    // 127.0.0.1:10002
    //         ↓
    //   brpc::Server
    //         ↓
    //  找到 FileService
    //         ↓
    // FileServiceImpl::PutSingleFile()
    //         ↓
    // store_.put()
    //         ↓
    //      写入磁盘
    //         ↓
    //    设置 response
    //         ↓
    //      网络返回
    //         ↓
    // 填充 put_response
    stub.PutSingleFile(
        &put_controller,
        &put_request,
        &put_response,
        nullptr);
    //      网络 / rpc 框架失败 || 业务失败
    if (put_controller.Failed() || !put_response.success()) {
        std::cerr << "put failed: "
                  << (put_controller.Failed()
                          ? put_controller.ErrorText()
                          : put_response.errmsg())
                  << '\n';
        return 1;
    }

    // 测试下载
    ahwei_im::GetSingleFileReq get_request;
    get_request.set_request_id("file-roundtrip-get");
    get_request.set_file_id(put_response.file_info().file_id());

    ahwei_im::GetSingleFileRsp get_response;
    brpc::Controller get_controller;
    stub.GetSingleFile(
        &get_controller,
        &get_request,
        &get_response,
        nullptr);
    if (get_controller.Failed() || !get_response.success()) {
        std::cerr << "get failed: "
                  << (get_controller.Failed()
                          ? get_controller.ErrorText()
                          : get_response.errmsg())
                  << '\n';
        return 1;
    }

    if (get_response.file_data().file_content() != original) {
        std::cerr << "round trip failed: content changed\n";
        return 1;
    }

    std::cout << "FileServer round trip succeeded\n"
              << "file_id: " << put_response.file_info().file_id() << '\n'
              << "content: " << get_response.file_data().file_content();
    return 0;
}
