#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace chat::file {

// 把文件内容保存到磁盘，根据 file_id 从磁盘读取文件
class FileStore {
public:
    // explicit 防止隐式类型转换，单参数的构造函数支持隐式类型转换
    explicit FileStore(std::filesystem::path storage_path);

    // 保存文件
    std::optional<std::string> put(
        std::string_view content,
        std::string& error);

    // 读取文件
    bool get(
        std::string_view file_id,
        std::string& content,
        std::string& error) const;

    // 返回当前存储目录
    const std::filesystem::path& storage_path() const noexcept; // noexcept 这个函数承诺不会抛出异常

private:
    // 工具函数，生成唯一文件 id
    static std::string make_file_id();
    // 工具函数，检查 file_id 是否合法
    static bool is_valid_file_id(std::string_view file_id) noexcept;

    std::filesystem::path storage_path_;
};

}  // namespace chat::file
