#include "file/file_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

bool read_local_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad();
}

bool write_local_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

void show_usage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << "\n"
              << "  " << program << " put <source-file>\n"
              << "  " << program << " get <file-id> <output-file>\n";
}

int run_round_trip(chat::file::FileStore& store) {
    const std::string original = "hello from the file service demo\n";
    std::string error;
    const auto file_id = store.put(original, error);
    if (!file_id) {
        std::cerr << "put failed: " << error << '\n';
        return 1;
    }

    std::string loaded;
    if (!store.get(*file_id, loaded, error)) {
        std::cerr << "get failed: " << error << '\n';
        return 1;
    }

    std::cout << "storage: " << store.storage_path() << '\n'
              << "file_id: " << *file_id << '\n'
              << "content: " << loaded;
    return loaded == original ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        chat::file::FileStore store("file_demo_data");

        if (argc == 1) {
            return run_round_trip(store);
        }

        const std::string command = argv[1];
        if (command == "put" && argc == 3) {
            std::string content;
            if (!read_local_file(argv[2], content)) {
                std::cerr << "cannot read source file: " << argv[2] << '\n';
                return 1;
            }

            std::string error;
            const auto file_id = store.put(content, error);
            if (!file_id) {
                std::cerr << "put failed: " << error << '\n';
                return 1;
            }

            std::cout << *file_id << '\n';
            return 0;
        }

        if (command == "get" && argc == 4) {
            std::string content;
            std::string error;
            if (!store.get(argv[2], content, error)) {
                std::cerr << "get failed: " << error << '\n';
                return 1;
            }
            if (!write_local_file(argv[3], content)) {
                std::cerr << "cannot write output file: " << argv[3] << '\n';
                return 1;
            }
            std::cout << "saved to " << argv[3] << '\n';
            return 0;
        }

        show_usage(argv[0]);
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "startup failed: " << exception.what() << '\n';
        return 1;
    }
}
