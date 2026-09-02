#pragma once

#include <string>
#include <cstddef>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <odb/core.hxx>

// 简化 boost 时间类型
typedef boost::posix_time::ptime ptime;

// 告诉 ODB：Person 是一个持久化对象
#pragma db object
class Person
{
public:
    Person(const std::string &name,
           int age,
           const ptime &update)
        : _age(age),
          _name(name),
          _update(update)
    {
    }

    void age(int val)
    {
        _age = val;
    }

    int age() const
    {
        return _age;
    }

    void name(const std::string &val)
    {
        _name = val;
    }

    std::string name() const
    {
        return _name;
    }

    void update(const ptime &val)
    {
        _update = val;
    }

    std::string update() const
    {
        return boost::posix_time::to_simple_string(_update);
    }

private:
    // 允许 ODB 访问私有成员
    friend class odb::access;

    // ODB 加载对象时需要默认构造函数
    Person()
    {
    }

// 主键，auto 表示数据库自动生成
#pragma db id auto
    unsigned long _id;

    unsigned short _age;

    std::string _name;

// MySQL TIMESTAMP 类型，不允许 NULL
#pragma db type("TIMESTAMP") not_null
    boost::posix_time::ptime _update;
};