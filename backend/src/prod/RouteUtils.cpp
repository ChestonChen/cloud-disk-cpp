#include "prod/RouteUtils.h"

#include "utils/Json.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace cloud_disk {

// 把 int64 数字转成 SQL 字符串。
std::string sqlNumber(std::int64_t value) {
    return std::to_string(value);
}

// 把 uint64 数字转成 SQL 字符串。
std::string sqlNumber(std::uint64_t value) {
    return std::to_string(value);
}

// 从数据库行读取 int64 字段。
std::int64_t asInt64(const DbRow& row, const std::string& name) {
    return std::stoll(row.at(name));
}

// 从数据库行读取 uint64 字段。
std::uint64_t asUInt64(const DbRow& row, const std::string& name) {
    return static_cast<std::uint64_t>(std::stoull(row.at(name)));
}

// 从数据库行读取布尔字段。
bool asBool(const DbRow& row, const std::string& name) {
    return row.at(name) == "1" || row.at(name) == "true";
}

// 根据用户名和密码生成密码摘要。
std::string hashPassword(const std::string& username, const std::string& password) {
    std::hash<std::string> hasher;
    std::ostringstream out;
    out << std::hex << hasher("cloud-disk:" + username + ":" + password);
    return out.str();
}

// 生成较长的登录 token。
std::string newToken() {
    static std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) {
        out << std::hex << std::setw(16) << std::setfill('0') << rng();
    }
    return out.str();
}

// 生成分享链接中的短 token。
std::string newShareToken() {
    static std::mt19937_64 rng(std::random_device {}());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << rng();
    return out.str();
}

// 校验用户名是否满足项目约定的格式：字母、数字、下划线、短横线。
bool validUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 64) {
        return false;
    }
    for (char ch : username) {
        bool isDigit = ch >= '0' && ch <= '9';
        bool isUpper = ch >= 'A' && ch <= 'Z';
        bool isLower = ch >= 'a' && ch <= 'z';
        bool isMark = ch == '_' || ch == '-';
        if (!(isDigit || isUpper || isLower || isMark)) {
            return false;
        }
    }
    return true;
}

// 转义 HTML 特殊字符，避免分享页显示异常。
std::string htmlEscape(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

// 根据请求头生成服务基础地址。
std::string baseUrl(const drogon::HttpRequestPtr& req) {
    auto proto = req->getHeader("x-forwarded-proto");
    auto host = req->getHeader("host");
    if (proto.empty()) {
        proto = "http";
    }
    return proto + "://" + host;
}

// 构造文本响应，并统一加上跨域头。
drogon::HttpResponsePtr textResponse(drogon::HttpStatusCode status,
                                     const std::string& body,
                                     const std::string& contentType) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(status);
    response->setContentTypeString(contentType);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    response->setBody(body);
    return response;
}

// 构造 JSON 响应。
drogon::HttpResponsePtr jsonResponse(drogon::HttpStatusCode status, const std::string& body) {
    return textResponse(status, body, "application/json; charset=utf-8");
}

// 构造统一错误响应。
drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status,
                                      int code,
                                      const std::string& message) {
    return jsonResponse(status, errorJson(code, message));
}

// 根据用户 id 查询用户信息。
std::optional<UserContext> findUserById(const MySqlDatabase& db, std::int64_t userId) {
    auto rows = db.query("SELECT id, username, storage_used, storage_limit FROM users WHERE id = "
                         + sqlNumber(userId));
    if (rows.empty()) {
        return std::nullopt;
    }
    auto row = rows[0];
    return UserContext {
        asInt64(row, "id"),
        row["username"],
        asUInt64(row, "storage_used"),
        asUInt64(row, "storage_limit"),
    };
}

// 解析 Authorization token，并返回当前登录用户。
std::optional<UserContext> requireUser(const drogon::HttpRequestPtr& req,
                                       const MySqlDatabase& db,
                                       const RedisSessionStore& sessions) {
    const std::string prefix = "Bearer ";
    auto header = req->getHeader("authorization");
    if (header.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    auto userId = sessions.findUserId(header.substr(prefix.size()));
    if (!userId) {
        return std::nullopt;
    }
    return findUserById(db, *userId);
}

// 检查 parent_id 是否是一个有效目录。
bool parentExists(const MySqlDatabase& db, std::int64_t userId, std::int64_t parentId) {
    if (parentId == 0) {
        return true;
    }
    auto rows = db.query("SELECT id FROM files WHERE user_id = " + sqlNumber(userId)
                         + " AND id = " + sqlNumber(parentId)
                         + " AND is_dir = TRUE AND is_deleted = FALSE");
    return !rows.empty();
}

// 查询当前用户拥有且未删除的文件记录。
std::optional<FileRow> findActiveFile(const MySqlDatabase& db,
                                      std::int64_t userId,
                                      std::int64_t fileId) {
    auto rows = db.query(
        "SELECT id, parent_id, COALESCE(object_id, 0) object_id, name, size_bytes, is_dir "
        "FROM files WHERE user_id = "
        + sqlNumber(userId) + " AND id = " + sqlNumber(fileId) + " AND is_deleted = FALSE");
    if (rows.empty()) {
        return std::nullopt;
    }
    auto row = rows[0];
    return FileRow {
        asInt64(row, "id"),
        asInt64(row, "parent_id"),
        asInt64(row, "object_id"),
        row["name"],
        asUInt64(row, "size_bytes"),
        asBool(row, "is_dir"),
    };
}

// 把 FileRow 转成接口返回用的 JSON。
std::string fileJson(const FileRow& file, bool isDeleted) {
    return jsonObject({
        {"id", std::to_string(file.id)},
        {"parent_id", std::to_string(file.parentId)},
        {"object_id", std::to_string(file.objectId)},
        {"name", file.name},
        {"is_dir", file.isDir ? "true" : "false"},
        {"is_deleted", isDeleted ? "true" : "false"},
        {"size_bytes", std::to_string(file.sizeBytes)},
    });
}

// 根据文件对象 id 查找实际磁盘路径。
std::optional<std::string> objectPath(const MySqlDatabase& db, std::int64_t objectId) {
    auto rows = db.query("SELECT storage_path FROM file_objects WHERE id = " + sqlNumber(objectId));
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows[0]["storage_path"];
}

namespace {

bool parseByteRange(const std::string& header,
                    std::uint64_t fileSize,
                    std::uint64_t& start,
                    std::uint64_t& end) {
    const std::string prefix = "bytes=";
    if (header.rfind(prefix, 0) != 0 || fileSize == 0) {
        return false;
    }
    auto spec = header.substr(prefix.size());
    auto dash = spec.find('-');
    if (dash == std::string::npos) {
        return false;
    }
    auto startText = spec.substr(0, dash);
    auto endText = spec.substr(dash + 1);
    if (startText.empty()) {
        return false;
    }
    start = static_cast<std::uint64_t>(std::stoull(startText));
    end = endText.empty() ? (fileSize - 1) : static_cast<std::uint64_t>(std::stoull(endText));
    if (start >= fileSize || start > end) {
        return false;
    }
    if (end >= fileSize) {
        end = fileSize - 1;
    }
    return true;
}

}

drogon::HttpResponsePtr fileDownloadResponse(const std::string& path,
                                             const std::string& filename,
                                             const std::string& rangeHeader) {
    if (rangeHeader.empty()) {
        auto response = drogon::HttpResponse::newFileResponse(path, filename);
        response->addHeader("Accept-Ranges", "bytes");
        response->addHeader("Access-Control-Expose-Headers", "Content-Disposition, Accept-Ranges, Content-Range");
        return response;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return errorResponse(drogon::k404NotFound, 404, "object not found");
    }
    auto fileSize = static_cast<std::uint64_t>(in.tellg());
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    if (!parseByteRange(rangeHeader, fileSize, start, end)) {
        auto response = errorResponse(drogon::k416RequestedRangeNotSatisfiable, 416, "invalid range");
        response->addHeader("Content-Range", "bytes */" + std::to_string(fileSize));
        return response;
    }

    auto length = end - start + 1;
    std::string body(static_cast<std::size_t>(length), '\0');
    in.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    in.read(body.data(), static_cast<std::streamsize>(length));

    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k206PartialContent);
    response->setContentTypeString("application/octet-stream");
    response->addHeader("Accept-Ranges", "bytes");
    response->addHeader("Content-Range",
                        "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/"
                            + std::to_string(fileSize));
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    response->addHeader("Access-Control-Expose-Headers", "Content-Disposition, Accept-Ranges, Content-Range");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->setBody(std::move(body));
    return response;
}

}
