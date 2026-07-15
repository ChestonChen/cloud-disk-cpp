#include "prod/MySqlDatabase.h"

#include <stdexcept>
#include <utility>

namespace cloud_disk {

// 建立 MySQL 连接，并统一设置字符集。
MySqlDatabase::MySqlDatabase(std::string host,
                             int port,
                             std::string database,
                             std::string user,
                             std::string password)
    : connection_(mysql_init(nullptr)) {
    if (!connection_) {
        throw std::runtime_error("mysql init failed");
    }
    if (!mysql_real_connect(connection_,
                            host.c_str(),
                            user.c_str(),
                            password.c_str(),
                            database.c_str(),
                            static_cast<unsigned int>(port),
                            nullptr,
                            0)) {
        std::string error = mysql_error(connection_);
        mysql_close(connection_);
        connection_ = nullptr;
        throw std::runtime_error("mysql connect failed: " + error);
    }
    execute("SET NAMES utf8mb4");
}

// 析构时释放 MySQL 连接。
MySqlDatabase::~MySqlDatabase() {
    if (connection_) {
        mysql_close(connection_);
    }
}

// 执行查询 SQL，并把结果集转换成 DbRow 列表。
std::vector<DbRow> MySqlDatabase::query(const std::string& sql) const {
    std::lock_guard<std::mutex> lock(mutex_);
    run(sql);
    MYSQL_RES* result = mysql_store_result(connection_);
    if (!result) {
        return {};
    }

    int columnCount = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    std::vector<DbRow> rows;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        DbRow item;
        for (int i = 0; i < columnCount; ++i) {
            item[fields[i].name] = row[i] ? std::string(row[i], lengths[i]) : "";
        }
        rows.push_back(std::move(item));
    }
    mysql_free_result(result);
    return rows;
}

// 执行不返回结果集的 SQL。
void MySqlDatabase::execute(const std::string& sql) const {
    std::lock_guard<std::mutex> lock(mutex_);
    run(sql);
}

// 转义 SQL 字符串参数，避免用户输入中的特殊字符破坏语句。
std::string MySqlDatabase::quote(const std::string& value) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string escaped(value.size() * 2 + 1, '\0');
    unsigned long length = mysql_real_escape_string(
        connection_, &escaped[0], value.c_str(), static_cast<unsigned long>(value.size()));
    escaped.resize(length);
    return "'" + escaped + "'";
}

// 统一调用 mysql_query，失败时抛出异常。
void MySqlDatabase::run(const std::string& sql) const {
    if (mysql_query(connection_, sql.c_str()) != 0) {
        throw std::runtime_error("mysql query failed: " + std::string(mysql_error(connection_)));
    }
}

} // namespace cloud_disk
