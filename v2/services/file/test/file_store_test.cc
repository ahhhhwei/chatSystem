#include "file/file_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

class FileStoreTest : public testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() /
                     "chat_system_v2_file_store_test";
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    std::filesystem::path test_path_;
};

TEST_F(FileStoreTest, PutThenGetReturnsTheSameBinaryContent) {
    chat::file::FileStore store(test_path_);
    const std::string original("hello\0binary", 12);
    std::string error;

    const auto file_id = store.put(original, error);
    ASSERT_TRUE(file_id.has_value()) << error;

    std::string loaded;
    ASSERT_TRUE(store.get(*file_id, loaded, error)) << error;
    EXPECT_EQ(original, loaded);
}

TEST_F(FileStoreTest, RejectsAPathInsteadOfAFileId) {
    chat::file::FileStore store(test_path_);
    std::string content;
    std::string error;

    EXPECT_FALSE(store.get("../../etc/passwd", content, error));
    EXPECT_EQ("invalid file_id", error);
}

}  // namespace
