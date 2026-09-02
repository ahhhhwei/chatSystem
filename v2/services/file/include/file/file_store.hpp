#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace chat::file
{

    class FileStore
    {
    public:
        // explicit 禁止编译器进行没有明确写出来的隐式类型转换（单参数的构造函数支持隐式类型转换）
        explicit FileStore(fs::path storage_path);

        // 保存二进制内容，成功时返回新生成的 file_id。
        // optional 表示一个值可能存在，也可能不存在，不存在时返回 std::nullopt
        std::optional<std::string> put(std::string_view content, std::string &error);

        // 根据 file_id 读取二进制内容。
        bool get(std::string_view file_id, std::string &content, std::string &error) const;

        const fs::path &storage_path() const;

    private:
        static std::string make_file_id();
        static bool is_valid_file_id(std::string_view file_id);

        fs::path storage_path_;
    };

} // namespace chat::file
