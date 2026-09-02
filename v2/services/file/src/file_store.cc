#include "file/file_store.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chat::file
{
    FileStore::FileStore(fs::path storage_path)
        : storage_path_(std::move(storage_path))
    {
        std::error_code error;
        fs::create_directories(storage_path_, error);
        if (error)
        {
            throw std::runtime_error("cannot create storage directory: " + error.message());
        }
    }

    std::optional<std::string> FileStore::put(std::string_view content, std::string &error)
    {
        error.clear();

        const std::string file_id = make_file_id();

        const auto final_path = storage_path_ / file_id;
        const auto temporary_path = storage_path_ / (file_id + ".tmp");

        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "cannot open temporary file for writing";
            return std::nullopt;
        }

        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output)
        {
            std::error_code ignored;
            fs::remove(temporary_path, ignored);
            error = "cannot write file content";
            return std::nullopt;
        }

        std::error_code rename_error;
        fs::rename(temporary_path, final_path, rename_error);
        if (rename_error)
        {
            std::error_code ignored;
            fs::remove(temporary_path, ignored);
            error = "cannot finish file write: " + rename_error.message();
            return std::nullopt;
        }

        return file_id;
    }

    bool FileStore::get(std::string_view file_id, std::string &content, std::string &error) const
    {
        content.clear();
        error.clear();

        if (!is_valid_file_id(file_id))
        {
            error = "invalid file_id";
            return false;
        }

        std::ifstream input(storage_path_ / std::string(file_id), std::ios::binary);
        if (!input)
        {
            error = "file not found";
            return false;
        }

        content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (input.bad())
        {
            content.clear();
            error = "cannot read file content";
            return false;
        }

        return true;
    }

    const fs::path &FileStore::storage_path() const
    {
        return storage_path_;
    }

    std::string FileStore::make_file_id()
    {
        thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(0, 255);

        std::array<std::uint8_t, 16> bytes{};
        for (auto &byte : bytes)
        {
            byte = static_cast<std::uint8_t>(distribution(generator));
        }

        // RFC 4122 version 4 UUID and variant bits.
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (index == 4 || index == 6 || index == 8 || index == 10)
            {
                result << '-';
            }
            result << std::setw(2) << static_cast<int>(bytes[index]);
        }
        return result.str();
    }

    bool FileStore::is_valid_file_id(std::string_view file_id)
    {
        if (file_id.size() != 36)
        {
            return false;
        }

        for (std::size_t index = 0; index < file_id.size(); ++index)
        {
            const bool is_separator = index == 8 || index == 13 || index == 18 || index == 23;
            const auto character = static_cast<unsigned char>(file_id[index]);
            if (is_separator ? file_id[index] != '-' : std::isxdigit(character) == 0)
            {
                return false;
            }
        }
        return true;
    }

} // namespace chat::file
