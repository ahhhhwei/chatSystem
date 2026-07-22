#include <jsoncpp/json/json.h>
#include <iostream>
#include <sstream>
#include <memory>

bool Serialize(const Json::Value &val, std::string &dst)
{
    // 先定义Json::StreamWriter 工厂类 Json::StreamWriterBuilder
    Json::StreamWriterBuilder swb;
    std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());

    // 工厂：
    // 普通对象创建：
    // Car car;
    // Car* car = new Car();
    // 工厂模式则是：
    // CarFactory factory;
    // Car* car = factory.createCar();
    // 上面代码中的 swb.newStreamWriter(); 表示根据当前配置，创建一个 JSON 写入器
    // StreamWriterBuilder
    //     ↓ 创建
    //     StreamWriter
    //     ↓ 使用
    //         将Json::Value写入输出流
    //             Json::StreamWriterBuilder swb;

    // Json::StreamWriter *writer = swb.newStreamWriter();

    // 通过 write 接口进行序列化
    std::stringstream ss;          // 内存中临时的文本容器，但它提供流式读写接口
    int ret = sw->write(val, &ss); // write 不直接返回字符串，因为流不一定是写入字符串，还有可能写入文件，或是标准输出等
    // 写入文件
    // std::ofstream ofs("student.json");
    // sw->write(val, &ofs);
    // 写入标准输出
    // sw->write(val, &std::cout);
    if (ret != 0)
    {
        std::cout << "Json 序列化失败！" << std::endl;
    }
    dst = ss.str();
    return true;
}

bool UnSerialize(const std::string &src, Json::Value &val)
{
    Json::CharReaderBuilder crb;
    std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
    std::string err;
    bool ret = cr->parse(src.c_str(), src.c_str() + src.size(), &val, &err);
    if (ret == false)
    {
        std::cout << "Json 反序列化失败：" << err << std::endl;
        return false;
    }
    return true;
}

int main()
{
    char name[] = "张三";
    int age = 18;
    float score[3] = {88, 89.5, 99};

    Json::Value stu;
    stu["姓名"] = name;
    stu["年龄"] = age;
    stu["成绩"].append(score[0]);
    stu["成绩"].append(score[1]);
    stu["成绩"].append(score[2]);

    std::string stu_str;
    bool ret = Serialize(stu, stu_str);
    if (ret == false)
        return -1;
    std::cout << stu_str << std::endl;

    Json::Value val;
    ret = UnSerialize(stu_str, val);
    if (ret == false)
        return -1;
    std::cout << val["姓名"].asString() << std::endl;
    std::cout << val["年龄"].asInt() << std::endl;
    int sz = (int)val["成绩"].size();
    for (int i = 0; i < sz; i++)
    {
        std::cout << val["成绩"][i].asFloat() << std::endl;
    }
    return 0;
}
