#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>
#include <iostream>

int main()
{
    // 设置全局的刷新策略
    spdlog::flush_every(std::chrono::seconds(1));       // 每秒刷新
    spdlog::flush_on(spdlog::level::level_enum::debug); // 遇到debug以上等级立即刷新
    // 设置全局的日志输出等级（每个日志器还可以独立进行设置）
    spdlog::set_level(spdlog::level::level_enum::debug);

    // 创建异步日志器
    auto logger = spdlog::stdout_color_mt<spdlog::async_factory>("async-logger");       // 标准输出
    // 设置日志器的刷新策略，以及日志器的输出等级
    logger->flush_on(spdlog::level::level_enum::debug);
    logger->set_level(spdlog::level::level_enum::debug);

    // 设置日志输出格式
    logger->set_pattern("[%n][%H:%M:%S][%t][%-8l] %v"); // -8：格式化对齐规则：左对齐，固定占 8 个字符宽度
    // 进行简单的日志输出
    logger->trace("你好！{}", "ahwei");
    logger->debug("你好！{}", "ahwei");
    logger->info("你好！{}", "ahwei");
    logger->warn("你好！{}", "ahwei");
    logger->error("你好！{}", "ahwei");
    logger->critical("你好！{}", "ahwei");
    std::cout << "log done!" << std::endl;

    return 0;
}