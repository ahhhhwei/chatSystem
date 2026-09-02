#include <gtest/gtest.h> 

int Add(int a, int b) 
{
    return a + b;
}
int Sub(int a, int b) 
{
    return a - b;
}
// TEST(测试名称, 测试用例名称)
TEST(MathTest, TestAdd) 
{
    EXPECT_EQ(Add(1,2), 3);
    ASSERT_EQ(Add(-1, 1), 0);
}

TEST(MathTest, TestSub) 
{
    EXPECT_EQ(Sub(5,3), 2);
    EXPECT_EQ(Sub(2,7), -5);
}

TEST(StrTest, StrCmp)
{
    std::string str = "ahwei";
    ASSERT_EQ(str, "Ahwei");
    ASSERT_EQ(str, "ahwei");
}

int main (int argc, char* argv[])
{
    // 单元测试框架的初始化
    testing::InitGoogleTest(&argc, argv);
    // 开始所有的单元测试
    return RUN_ALL_TESTS(); 
}