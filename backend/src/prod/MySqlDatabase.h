#pragma once

#include "prod/Types.h"

#include <mysql/mysql.h>

#include <mutex>
#include <string>
#include <vector>

namespace cloud_disk {

// 封装 MySQL C API，提供线程安全的查询、执行和字符串转义能力。
class MySqlDatabase {
public:
    // 建立 MySQL 连接，并设置 utf8mb4 字符集。
    MySqlDatabase(std::string host,
                  int port,
                  std::string database,
                  std::string user,
                  std::string password);

    // 关闭 MySQL 连接。
    ~MySqlDatabase();

    MySqlDatabase(const MySqlDatabase&) = delete;
    MySqlDatabase& operator=(const MySqlDatabase&) = delete;

    // 执行 SELECT 查询，并把结果转换成字段名到字符串值的映射。
    std::vector<DbRow> query(const std::string& sql) const;

    // 执行 INSERT、UPDATE、DELETE 等不需要返回结果的 SQL。
    void execute(const std::string& sql) const;

    // 对用户输入做 SQL 字符串转义，防止引号等特殊字符破坏 SQL。
    std::string quote(const std::string& value) const;

private:
    // 调用 mysql_query，失败时抛出包含 MySQL 错误信息的异常。
    void run(const std::string& sql) const;

    MYSQL* connection_ = nullptr;
    mutable std::mutex mutex_;
};

}
