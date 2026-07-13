#include "utils/Hash.h"
#include "utils/Json.h"
#include "utils/Path.h"

#include <drogon/drogon.h>
#include <mysql/mysql.h>

#include <arpa/inet.h>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace cloud_disk;

namespace {

struct UserContext {
    std::int64_t id = 0;
    std::string username;
    std::uint64_t storageUsed = 0;
    std::uint64_t storageLimit = 0;
};

struct FileRow {
    std::int64_t id = 0;
    std::int64_t parentId = 0;
    std::int64_t objectId = 0;
    std::string name;
    std::uint64_t sizeBytes = 0;
    bool isDir = false;
};

using DbRow = std::map<std::string, std::string>;

class MySqlDatabase {
public:
    MySqlDatabase(std::string host,
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

    ~MySqlDatabase() {
        if (connection_) {
            mysql_close(connection_);
        }
    }

    MySqlDatabase(const MySqlDatabase&) = delete;
    MySqlDatabase& operator=(const MySqlDatabase&) = delete;

    std::vector<DbRow> query(const std::string& sql) const {
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

    void execute(const std::string& sql) const {
        run(sql);
    }

    std::string quote(const std::string& value) const {
        std::string escaped(value.size() * 2 + 1, '\0');
        unsigned long length = mysql_real_escape_string(
            connection_, &escaped[0], value.c_str(), static_cast<unsigned long>(value.size()));
        escaped.resize(length);
        return "'" + escaped + "'";
    }

private:
    void run(const std::string& sql) const {
        if (mysql_query(connection_, sql.c_str()) != 0) {
            throw std::runtime_error("mysql query failed: " + std::string(mysql_error(connection_)));
        }
    }

    MYSQL* connection_ = nullptr;
};

class RedisSessionStore {
public:
    RedisSessionStore(std::string host, int port, int ttlSeconds)
        : host_(std::move(host)), port_(port), ttlSeconds_(ttlSeconds) {}

    void save(const std::string& token, std::int64_t userId) const {
        command({"SETEX", key(token), std::to_string(ttlSeconds_), std::to_string(userId)});
    }

    std::optional<std::int64_t> findUserId(const std::string& token) const {
        auto value = command({"GET", key(token)});
        if (!value || value->empty()) {
            return std::nullopt;
        }
        return std::stoll(*value);
    }

private:
    static std::string key(const std::string& token) {
        return "cloud_disk:session:" + token;
    }

    std::optional<std::string> command(const std::vector<std::string>& parts) const {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            throw std::runtime_error("redis socket failed");
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port_));
        if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) <= 0
            || ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(fd);
            throw std::runtime_error("redis connect failed");
        }

        std::ostringstream request;
        request << "*" << parts.size() << "\r\n";
        for (const auto& part : parts) {
            request << "$" << part.size() << "\r\n" << part << "\r\n";
        }
        std::string wire = request.str();
        ::send(fd, wire.data(), wire.size(), 0);

        auto line = readLine(fd);
        if (line.empty()) {
            ::close(fd);
            throw std::runtime_error("redis empty response");
        }
        char type = line[0];
        std::optional<std::string> result;
        if (type == '+') {
            result = line.substr(1);
        } else if (type == '$') {
            int size = std::stoi(line.substr(1));
            if (size >= 0) {
                result = readBytes(fd, static_cast<std::size_t>(size));
                readBytes(fd, 2); // trailing CRLF
            }
        } else if (type == '-') {
            ::close(fd);
            throw std::runtime_error("redis error: " + line.substr(1));
        }
        ::close(fd);
        return result;
    }

    static std::string readLine(int fd) {
        std::string out;
        char ch = 0;
        while (::recv(fd, &ch, 1, 0) == 1) {
            if (ch == '\r') {
                ::recv(fd, &ch, 1, 0);
                break;
            }
            out.push_back(ch);
        }
        return out;
    }

    static std::string readBytes(int fd, std::size_t size) {
        std::string out(size, '\0');
        std::size_t offset = 0;
        while (offset < size) {
            ssize_t n = ::recv(fd, &out[offset], size - offset, 0);
            if (n <= 0) {
                throw std::runtime_error("redis read failed");
            }
            offset += static_cast<std::size_t>(n);
        }
        return out;
    }

    std::string host_;
    int port_ = 6379;
    int ttlSeconds_ = 86400;
};

std::string envString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

int envInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::atoi(value) : fallback;
}

std::string sqlNumber(std::int64_t value) {
    return std::to_string(value);
}

std::string sqlNumber(std::uint64_t value) {
    return std::to_string(value);
}

std::int64_t asInt64(const DbRow& row, const std::string& name) {
    return std::stoll(row.at(name));
}

std::uint64_t asUInt64(const DbRow& row, const std::string& name) {
    return static_cast<std::uint64_t>(std::stoull(row.at(name)));
}

bool asBool(const DbRow& row, const std::string& name) {
    return row.at(name) == "1" || row.at(name) == "true";
}

std::string hashPassword(const std::string& username, const std::string& password) {
    std::hash<std::string> hasher;
    std::ostringstream out;
    out << std::hex << hasher("cloud-disk:" + username + ":" + password);
    return out.str();
}

std::string newToken() {
    static std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) {
        out << std::hex << std::setw(16) << std::setfill('0') << rng();
    }
    return out.str();
}

std::string newShareToken() {
    static std::mt19937_64 rng(std::random_device {}());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << rng();
    return out.str();
}

bool validUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 64) {
        return false;
    }
    for (char ch : username) {
        bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

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

std::string baseUrl(const drogon::HttpRequestPtr& req) {
    auto proto = req->getHeader("x-forwarded-proto");
    auto host = req->getHeader("host");
    if (proto.empty()) {
        proto = "http";
    }
    return proto + "://" + host;
}

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

drogon::HttpResponsePtr jsonResponse(drogon::HttpStatusCode status, const std::string& body) {
    return textResponse(status, body, "application/json; charset=utf-8");
}

drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status, int code, const std::string& message) {
    return jsonResponse(status, errorJson(code, message));
}

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

bool parentExists(const MySqlDatabase& db, std::int64_t userId, std::int64_t parentId) {
    if (parentId == 0) {
        return true;
    }
    auto rows = db.query("SELECT id FROM files WHERE user_id = " + sqlNumber(userId)
                         + " AND id = " + sqlNumber(parentId)
                         + " AND is_dir = TRUE AND is_deleted = FALSE");
    return !rows.empty();
}

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

std::string fileJson(const FileRow& file) {
    return jsonObject({
        {"id", std::to_string(file.id)},
        {"parent_id", std::to_string(file.parentId)},
        {"object_id", std::to_string(file.objectId)},
        {"name", file.name},
        {"is_dir", file.isDir ? "true" : "false"},
        {"is_deleted", "false"},
        {"size_bytes", std::to_string(file.sizeBytes)},
    });
}

std::optional<std::string> objectPath(const MySqlDatabase& db, std::int64_t objectId) {
    auto rows = db.query("SELECT storage_path FROM file_objects WHERE id = " + sqlNumber(objectId));
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows[0]["storage_path"];
}

void registerRoutes(const MySqlDatabase& db,
                    const RedisSessionStore& sessions,
                    const std::filesystem::path& storageRoot,
                    const std::filesystem::path& webRoot) {
    ensureDirectory(storageRoot / "objects");

    drogon::app().registerHandler("/health",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(jsonResponse(drogon::k200OK,
                                  okJson(jsonObject({{"service", "cloud-disk-prod"},
                                                     {"status", "healthy"},
                                                     {"stack", "drogon-mysql-redis"}}))));
        },
        {drogon::Get});

    drogon::app().registerHandler("/",
        [webRoot](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(drogon::HttpResponse::newFileResponse((webRoot / "index.html").string()));
        },
        {drogon::Get});

    drogon::app().registerHandler("/styles.css",
        [webRoot](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(drogon::HttpResponse::newFileResponse((webRoot / "styles.css").string()));
        },
        {drogon::Get});

    drogon::app().registerHandler("/app.js",
        [webRoot](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(drogon::HttpResponse::newFileResponse((webRoot / "app.js").string()));
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/user/register",
        [&db](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto body = parseFlatJsonObject(std::string(req->body()));
                auto username = body["username"];
                auto password = body["password"];
                if (!validUsername(username) || password.size() < 6 || password.size() > 128) {
                    callback(errorResponse(drogon::k400BadRequest, 400, "invalid username or password"));
                    return;
                }
                db.execute("INSERT INTO users(username, password_hash, display_name) VALUES("
                           + db.quote(username) + ", "
                           + db.quote(hashPassword(username, password)) + ", "
                           + db.quote(username) + ")");
                auto rows = db.query("SELECT id, username FROM users WHERE username = " + db.quote(username));
                callback(jsonResponse(drogon::k201Created,
                                      okJson(jsonObject({{"id", rows[0]["id"]},
                                                         {"username", username}}))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k400BadRequest, 400, ex.what()));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler("/api/user/login",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto body = parseFlatJsonObject(std::string(req->body()));
                auto username = body["username"];
                auto rows = db.query("SELECT id, password_hash FROM users WHERE username = " + db.quote(username));
                if (rows.empty() || rows[0]["password_hash"] != hashPassword(username, body["password"])) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "invalid username or password"));
                    return;
                }
                auto token = newToken();
                sessions.save(token, asInt64(rows[0], "id"));
                callback(jsonResponse(drogon::k200OK, okJson(jsonObject({{"token", token}}))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k401Unauthorized, 401, ex.what()));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler("/api/user/me",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                callback(jsonResponse(drogon::k200OK,
                                      okJson(jsonObject({{"id", std::to_string(user->id)},
                                                         {"username", user->username},
                                                         {"storage_used", std::to_string(user->storageUsed)},
                                                         {"storage_limit", std::to_string(user->storageLimit)}}))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k401Unauthorized, 401, ex.what()));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/folders",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                auto body = parseFlatJsonObject(std::string(req->body()));
                auto name = body["name"];
                std::int64_t parentId = std::stoll(body["parent_id"]);
                if (!isValidName(name) || !parentExists(db, user->id, parentId)) {
                    callback(errorResponse(drogon::k400BadRequest, 400, "invalid folder request"));
                    return;
                }
                db.execute("INSERT INTO files(user_id, parent_id, name, is_dir) VALUES("
                           + sqlNumber(user->id) + ", " + sqlNumber(parentId) + ", "
                           + db.quote(name) + ", TRUE)");
                auto rows = db.query(
                    "SELECT id FROM files WHERE user_id = " + sqlNumber(user->id)
                    + " AND parent_id = " + sqlNumber(parentId)
                    + " AND name = " + db.quote(name)
                    + " AND is_deleted = FALSE ORDER BY id DESC LIMIT 1");
                FileRow file {asInt64(rows[0], "id"), parentId, 0, name, 0, true};
                callback(jsonResponse(drogon::k201Created, okJson(fileJson(file))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k400BadRequest, 400, ex.what()));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler("/api/files",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                std::int64_t parentId = req->getParameter("parent_id").empty()
                                            ? 0
                                            : std::stoll(req->getParameter("parent_id"));
                auto rows = db.query(
                    "SELECT id, parent_id, COALESCE(object_id, 0) object_id, name, size_bytes, is_dir "
                    "FROM files WHERE user_id = "
                    + sqlNumber(user->id) + " AND parent_id = " + sqlNumber(parentId)
                    + " AND is_deleted = FALSE ORDER BY is_dir DESC, id DESC");
                std::vector<std::string> items;
                for (const auto& row : rows) {
                    items.push_back(fileJson(FileRow {asInt64(row, "id"),
                                                     asInt64(row, "parent_id"),
                                                     asInt64(row, "object_id"),
                                                     row.at("name"),
                                                     asUInt64(row, "size_bytes"),
                                                     asBool(row, "is_dir")}));
                }
                callback(jsonResponse(drogon::k200OK, okJson(jsonArray(items))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k400BadRequest, 400, ex.what()));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/files/upload",
        [&db, &sessions, storageRoot](const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                std::int64_t parentId = req->getParameter("parent_id").empty()
                                            ? 0
                                            : std::stoll(req->getParameter("parent_id"));
                auto name = req->getParameter("name");
                std::string content(req->body());
                if (!isValidName(name) || content.empty() || !parentExists(db, user->id, parentId)) {
                    callback(errorResponse(drogon::k400BadRequest, 400, "invalid upload request"));
                    return;
                }
                auto dup = db.query(
                    "SELECT id FROM files WHERE user_id = " + sqlNumber(user->id)
                    + " AND parent_id = " + sqlNumber(parentId)
                    + " AND name = " + db.quote(name) + " AND is_deleted = FALSE");
                if (!dup.empty()) {
                    callback(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                    return;
                }

                auto sha = contentHash(content);
                auto objects = db.query("SELECT id FROM file_objects WHERE sha256 = " + db.quote(sha));
                std::int64_t objectId = 0;
                if (objects.empty()) {
                    auto objectDir = ensureDirectory(storageRoot / "objects" / sha.substr(0, 2));
                    auto objectFile = objectDir / sha;
                    std::ofstream out(objectFile, std::ios::binary | std::ios::trunc);
                    out.write(content.data(), static_cast<std::streamsize>(content.size()));
                    auto contentSize = static_cast<std::uint64_t>(content.size());
                    db.execute("INSERT INTO file_objects(sha256, size_bytes, storage_path, ref_count) VALUES("
                               + db.quote(sha) + ", " + sqlNumber(contentSize) + ", "
                               + db.quote(objectFile.string()) + ", 1)");
                    objectId = asInt64(db.query("SELECT id FROM file_objects WHERE sha256 = " + db.quote(sha))[0], "id");
                } else {
                    objectId = asInt64(objects[0], "id");
                    db.execute("UPDATE file_objects SET ref_count = ref_count + 1 WHERE id = "
                               + sqlNumber(objectId));
                }
                db.execute("INSERT INTO files(user_id, parent_id, object_id, name, size_bytes, is_dir) VALUES("
                           + sqlNumber(user->id) + ", " + sqlNumber(parentId) + ", "
                           + sqlNumber(objectId) + ", " + db.quote(name) + ", "
                           + sqlNumber(static_cast<std::uint64_t>(content.size())) + ", FALSE)");
                db.execute("UPDATE users SET storage_used = storage_used + "
                           + sqlNumber(static_cast<std::uint64_t>(content.size()))
                           + " WHERE id = " + sqlNumber(user->id));
                auto id = asInt64(db.query(
                    "SELECT id FROM files WHERE user_id = " + sqlNumber(user->id)
                    + " AND object_id = " + sqlNumber(objectId)
                    + " AND name = " + db.quote(name) + " ORDER BY id DESC LIMIT 1")[0], "id");
                callback(jsonResponse(drogon::k201Created,
                                      okJson(jsonObject({{"id", std::to_string(id)},
                                                         {"parent_id", std::to_string(parentId)},
                                                         {"object_id", std::to_string(objectId)},
                                                         {"name", name},
                                                         {"is_dir", "false"},
                                                         {"is_deleted", "false"},
                                                         {"size_bytes", std::to_string(content.size())},
                                                         {"sha256", sha}}))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k400BadRequest, 400, ex.what()));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler("/api/files/download",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                auto file = findActiveFile(db, user->id, std::stoll(req->getParameter("id")));
                if (!file || file->isDir) {
                    callback(errorResponse(drogon::k404NotFound, 404, "file not found"));
                    return;
                }
                auto path = objectPath(db, file->objectId);
                if (!path) {
                    callback(errorResponse(drogon::k404NotFound, 404, "object not found"));
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(*path, file->name);
                response->addHeader("Access-Control-Expose-Headers", "Content-Disposition");
                callback(response);
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k404NotFound, 404, ex.what()));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/shares",
        [&db, &sessions](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                auto body = parseFlatJsonObject(std::string(req->body()));
                auto fileId = std::stoll(body["file_id"]);
                auto file = findActiveFile(db, user->id, fileId);
                if (!file || file->isDir) {
                    callback(errorResponse(drogon::k404NotFound, 404, "file not found"));
                    return;
                }
                auto token = newShareToken();
                bool allowDownload = body["allow_download"] != "false";
                db.execute("INSERT INTO shares(share_token, access_code, user_id, file_id, allow_download) VALUES("
                           + db.quote(token) + ", " + db.quote(body["access_code"]) + ", "
                           + sqlNumber(user->id) + ", " + sqlNumber(fileId) + ", "
                           + (allowDownload ? "TRUE" : "FALSE") + ")");
                auto url = baseUrl(req) + "/share?token=" + token;
                callback(jsonResponse(drogon::k201Created,
                                      okJson(jsonObject({{"token", token},
                                                         {"url", url},
                                                         {"api_url", baseUrl(req) + "/api/public/share?token=" + token},
                                                         {"download_url", baseUrl(req) + "/api/public/download?token=" + token},
                                                         {"allow_download", allowDownload ? "true" : "false"}}))));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k400BadRequest, 400, ex.what()));
            }
        },
        {drogon::Post});

    auto publicShare = [&db](const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             bool html) {
        try {
            auto rows = db.query(
                "SELECT s.share_token, s.access_code, s.allow_download, f.id file_id, f.name, f.size_bytes "
                "FROM shares s JOIN files f ON s.file_id = f.id "
                "WHERE s.share_token = "
                + db.quote(req->getParameter("token")) + " AND s.is_active = TRUE AND f.is_deleted = FALSE");
            if (rows.empty() || (!rows[0]["access_code"].empty()
                                 && rows[0]["access_code"] != req->getParameter("code"))) {
                callback(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
                return;
            }
            db.execute("UPDATE shares SET view_count = view_count + 1 WHERE share_token = "
                       + db.quote(req->getParameter("token")));
            auto row = rows[0];
            if (html) {
                std::ostringstream page;
                page << "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
                     << "<title>Cloud Disk 分享</title><body style=\"font-family:sans-serif;max-width:680px;"
                     << "margin:64px auto;line-height:1.7\"><h1>Cloud Disk 文件分享</h1>"
                     << "<p>文件名：<strong>" << htmlEscape(row["name"]) << "</strong></p>"
                     << "<p>大小：" << row["size_bytes"] << " bytes</p>";
                if (asBool(row, "allow_download")) {
                    page << "<p><a href=\"/api/public/download?token=" << htmlEscape(req->getParameter("token"));
                    if (!req->getParameter("code").empty()) {
                        page << "&code=" << htmlEscape(req->getParameter("code"));
                    }
                    page << "\">下载文件</a></p>";
                }
                page << "</body></html>";
                callback(textResponse(drogon::k200OK, page.str(), "text/html; charset=utf-8"));
                return;
            }
            callback(jsonResponse(drogon::k200OK,
                                  okJson(jsonObject({{"token", row["share_token"]},
                                                     {"file_id", row["file_id"]},
                                                     {"name", row["name"]},
                                                     {"size_bytes", row["size_bytes"]},
                                                     {"allow_download", asBool(row, "allow_download") ? "true" : "false"}}))));
        } catch (const std::exception& ex) {
            callback(errorResponse(drogon::k404NotFound, 404, ex.what()));
        }
    };

    drogon::app().registerHandler("/share",
        [publicShare](const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            publicShare(req, std::move(callback), true);
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/public/share",
        [publicShare](const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            publicShare(req, std::move(callback), false);
        },
        {drogon::Get});

    drogon::app().registerHandler("/api/public/download",
        [&db](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto rows = db.query(
                    "SELECT s.access_code, s.allow_download, f.name, f.object_id "
                    "FROM shares s JOIN files f ON s.file_id = f.id "
                    "WHERE s.share_token = "
                    + db.quote(req->getParameter("token")) + " AND s.is_active = TRUE AND f.is_deleted = FALSE");
                if (rows.empty() || !asBool(rows[0], "allow_download")
                    || (!rows[0]["access_code"].empty()
                        && rows[0]["access_code"] != req->getParameter("code"))) {
                    callback(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
                    return;
                }
                auto path = objectPath(db, asInt64(rows[0], "object_id"));
                if (!path) {
                    callback(errorResponse(drogon::k404NotFound, 404, "object not found"));
                    return;
                }
                db.execute("UPDATE shares SET download_count = download_count + 1 WHERE share_token = "
                           + db.quote(req->getParameter("token")));
                callback(drogon::HttpResponse::newFileResponse(*path, rows[0]["name"]));
            } catch (const std::exception& ex) {
                callback(errorResponse(drogon::k404NotFound, 404, ex.what()));
            }
        },
        {drogon::Get});
}

} // namespace

int main() {
    auto config = envString("CLOUD_DISK_DROGON_CONFIG", "./backend/config/drogon.json");
    if (std::filesystem::exists(config)) {
        drogon::app().loadConfigFile(config);
    }

    auto storageRoot = std::filesystem::path(envString("CLOUD_DISK_STORAGE", "./storage"));
    auto webRoot = std::filesystem::path(envString("CLOUD_DISK_WEB_ROOT", "./web"));
    RedisSessionStore sessions(envString("CLOUD_DISK_REDIS_HOST", "127.0.0.1"),
                               envInt("CLOUD_DISK_REDIS_PORT", 6379),
                               envInt("CLOUD_DISK_SESSION_TTL", 86400));

    MySqlDatabase db(envString("CLOUD_DISK_MYSQL_HOST", "127.0.0.1"),
                     envInt("CLOUD_DISK_MYSQL_PORT", 3306),
                     envString("CLOUD_DISK_MYSQL_DATABASE", "cloud_disk"),
                     envString("CLOUD_DISK_MYSQL_USER", "cloud_disk"),
                     envString("CLOUD_DISK_MYSQL_PASSWORD", "cloud_disk_password"));
    registerRoutes(db, sessions, storageRoot, webRoot);

    drogon::app().addListener("0.0.0.0", envInt("CLOUD_DISK_PORT", 8080));
    drogon::app().run();
}
