#include <string>
#include <memory>
#include <iostream>

#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <odb/mysql/database.hxx>

#include "person.hxx"
#include "person-odb.hxx"

int main()
{
    // 创建 MySQL 数据库连接
    //
    // 参数依次是：
    // 用户名
    // 密码
    // 数据库
    // 地址
    // 端口
    // socket
    // 字符集

    std::shared_ptr<odb::database> db(
        new odb::mysql::database(
            "root",
            "wenjiawei",
            "mytest",
            "127.0.0.1",
            0,
            0,
            "utf8"));

    // 获取当前时间
    ptime p =
        boost::posix_time::second_clock::local_time();

    Person zhang("小张", 18, p);
    Person wang("小王", 19, p);

    // ODB 查询类型
    typedef odb::query<Person> query;
    typedef odb::result<Person> result;

    /*
     * 插入数据
     */
    {
        odb::transaction t(db->begin());

        unsigned long zid = db->persist(zhang);
        unsigned long wid = db->persist(wang);

        std::cout << "张 ID: " << zid << std::endl;
        std::cout << "王 ID: " << wid << std::endl;

        t.commit();
    }

    /*
     * 查询所有 Person
     */
    {
        odb::transaction t(db->begin());

        result r(db->query<Person>());

        for (result::iterator i = r.begin();
             i != r.end();
             ++i)
        {
            std::cout
                << "Hello, "
                << i->name()
                << " "
                << i->age()
                << " "
                << i->update()
                << std::endl;
        }

        t.commit();
    }

    return 0;
}