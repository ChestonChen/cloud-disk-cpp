#include "services/AuthService.h"
#include "services/FileService.h"
#include "services/MetadataStore.h"
#include "utils/HttpServer.h"
#include "utils/Json.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

using namespace cloud_disk;

namespace {

HttpResponse jsonResponse(int status, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    return response;
}

std::int64_t parseId(const std::string& value, std::int64_t fallback = 0) {
    if (value.empty()) {
        return fallback;
    }
    return std::stoll(value);
}

std::optional<User> requireUser(const HttpRequest& request, const AuthService& auth) {
    std::string header = getHeader(request, "Authorization");
    const std::string prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    return auth.authenticate(header.substr(prefix.size()));
}

HttpResponse unauthorized() {
    return jsonResponse(401, errorJson(401, "missing or invalid token"));
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("file not found: " + path.string());
    }
    std::ostringstream body;
    body << in.rdbuf();
    return body.str();
}

HttpResponse staticFileResponse(const std::filesystem::path& path, const std::string& contentType) {
    HttpResponse response;
    response.status = 200;
    response.contentType = contentType;
    response.body = readTextFile(path);
    return response;
}

std::string fileEntryWithObjectJson(const FileEntry& entry, const FileService& files) {
    auto fields = JsonObject {
        {"id", std::to_string(entry.id)},
        {"parent_id", std::to_string(entry.parentId)},
        {"object_id", std::to_string(entry.objectId)},
        {"name", entry.name},
        {"is_dir", entry.isDir ? "true" : "false"},
        {"is_deleted", entry.isDeleted ? "true" : "false"},
        {"size_bytes", std::to_string(entry.sizeBytes)},
    };
    if (!entry.isDir && entry.objectId != 0) {
        auto object = files.getObject(entry.objectId);
        fields["sha256"] = object.sha256;
        fields["ref_count"] = std::to_string(object.refCount);
    }
    return jsonObject(fields);
}

std::string newShareToken() {
    static std::mt19937_64 rng(std::random_device {}());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << rng();
    return out.str();
}

bool shareCodeMatches(const ShareLink& share, const HttpRequest& request) {
    return share.accessCode.empty() || getQuery(request, "code") == share.accessCode;
}

} // namespace

int main() {
    std::filesystem::path storageRoot = std::getenv("CLOUD_DISK_STORAGE")
                                            ? std::getenv("CLOUD_DISK_STORAGE")
                                            : "./storage";
    int port = std::getenv("CLOUD_DISK_PORT") ? std::atoi(std::getenv("CLOUD_DISK_PORT")) : 8080;

    MetadataStore store(storageRoot);
    AuthService auth(store);
    FileService files(store, storageRoot);
    HttpServer server("0.0.0.0", port);
    std::filesystem::path webRoot = std::getenv("CLOUD_DISK_WEB_ROOT")
                                        ? std::getenv("CLOUD_DISK_WEB_ROOT")
                                        : "./web";

    server.route("GET", "/", [&](const HttpRequest&) {
        return staticFileResponse(webRoot / "index.html", "text/html; charset=utf-8");
    });

    server.route("GET", "/styles.css", [&](const HttpRequest&) {
        return staticFileResponse(webRoot / "styles.css", "text/css; charset=utf-8");
    });

    server.route("GET", "/app.js", [&](const HttpRequest&) {
        return staticFileResponse(webRoot / "app.js", "application/javascript; charset=utf-8");
    });

    server.route("GET", "/health", [](const HttpRequest&) {
        return jsonResponse(200, okJson(jsonObject({
                                     {"service", "cloud-disk"},
                                     {"status", "healthy"},
                                     {"version", "0.1.0"},
                                 })));
    });

    server.route("POST", "/api/user/register", [&](const HttpRequest& request) {
        try {
            auto body = parseFlatJsonObject(request.body);
            auto user = auth.registerUser(body["username"], body["password"]);
            return jsonResponse(201, okJson(jsonObject({
                                         {"id", std::to_string(user.id)},
                                         {"username", user.username},
                                     })));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("POST", "/api/user/login", [&](const HttpRequest& request) {
        try {
            auto body = parseFlatJsonObject(request.body);
            std::string token = auth.login(body["username"], body["password"]);
            return jsonResponse(200, okJson(jsonObject({{"token", token}})));
        } catch (const std::exception& ex) {
            return jsonResponse(401, errorJson(401, ex.what()));
        }
    });

    server.route("GET", "/api/user/me", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        return jsonResponse(200, okJson(jsonObject({
                                     {"id", std::to_string(user->id)},
                                     {"username", user->username},
                                     {"storage_used", std::to_string(user->storageUsed)},
                                     {"storage_limit", std::to_string(user->storageLimit)},
                                 })));
    });

    server.route("POST", "/api/folders", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto body = parseFlatJsonObject(request.body);
            auto entry = files.createFolder(user->id, parseId(body["parent_id"]), body["name"]);
            return jsonResponse(201, okJson(fileEntryJson(entry)));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("GET", "/api/files", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        std::vector<std::string> rows;
        for (const auto& file : files.listFiles(user->id, parseId(getQuery(request, "parent_id")))) {
            rows.push_back(fileEntryJson(file));
        }
        return jsonResponse(200, okJson(jsonArray(rows)));
    });

    server.route("POST", "/api/files/upload", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto entry = files.uploadFile(user->id,
                                         parseId(getQuery(request, "parent_id")),
                                         getQuery(request, "name"),
                                         request.body);
            return jsonResponse(201, okJson(fileEntryWithObjectJson(entry, files)));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("POST", "/api/files/instant", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto body = parseFlatJsonObject(request.body);
            auto entry = files.instantUpload(user->id,
                                            parseId(body["parent_id"]),
                                            body["name"],
                                            body["sha256"],
                                            static_cast<std::uint64_t>(std::stoull(body["size_bytes"])));
            return jsonResponse(201, okJson(fileEntryWithObjectJson(entry, files)));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("POST", "/api/uploads/init", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto body = parseFlatJsonObject(request.body);
            auto session = files.initChunkedUpload(user->id,
                                                  parseId(body["parent_id"]),
                                                  body["name"],
                                                  body["sha256"],
                                                  static_cast<std::uint64_t>(std::stoull(body["size_bytes"])),
                                                  static_cast<std::uint64_t>(std::stoull(body["chunk_size"])),
                                                  std::stoi(body["total_chunks"]));
            return jsonResponse(201, okJson(jsonObject({
                                         {"upload_id", session.uploadId},
                                         {"status", session.status},
                                         {"total_chunks", std::to_string(session.totalChunks)},
                                     })));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("POST", "/api/uploads/chunk", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            files.uploadChunk(user->id,
                              getQuery(request, "upload_id"),
                              static_cast<int>(parseId(getQuery(request, "chunk_index"))),
                              request.body);
            return jsonResponse(200, okJson(jsonObject({{"uploaded", "true"}})));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("GET", "/api/uploads/progress", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            std::vector<std::string> rows;
            for (const auto& chunk : files.uploadProgress(user->id, getQuery(request, "upload_id"))) {
                rows.push_back(chunkJson(chunk));
            }
            return jsonResponse(200, okJson(jsonArray(rows)));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("POST", "/api/uploads/complete", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto entry = files.completeChunkedUpload(user->id, getQuery(request, "upload_id"));
            return jsonResponse(201, okJson(fileEntryWithObjectJson(entry, files)));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("GET", "/api/files/download", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto file = files.getDownloadFile(user->id, parseId(getQuery(request, "id")));
            auto object = files.getObject(file.objectId);
            std::ifstream in(object.storagePath, std::ios::binary);
            std::ostringstream body;
            body << in.rdbuf();
            HttpResponse response;
            response.status = 200;
            response.contentType = "application/octet-stream";
            response.headers["Content-Disposition"] = "attachment; filename=\"" + file.name + "\"";
            response.body = body.str();
            return response;
        } catch (const std::exception& ex) {
            return jsonResponse(404, errorJson(404, ex.what()));
        }
    });

    server.route("DELETE", "/api/files", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        bool deleted = files.deleteFile(user->id, parseId(getQuery(request, "id")));
        if (!deleted) {
            return jsonResponse(404, errorJson(404, "file not found"));
        }
        return jsonResponse(200, okJson(jsonObject({{"deleted", "true"}})));
    });

    server.route("GET", "/api/recycle", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        std::vector<std::string> rows;
        for (const auto& file : files.listRecycleBin(user->id)) {
            rows.push_back(fileEntryJson(file));
        }
        return jsonResponse(200, okJson(jsonArray(rows)));
    });

    server.route("POST", "/api/recycle/restore", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        bool restored = files.restoreFile(user->id, parseId(getQuery(request, "id")));
        if (!restored) {
            return jsonResponse(404, errorJson(404, "file not found in recycle bin"));
        }
        return jsonResponse(200, okJson(jsonObject({{"restored", "true"}})));
    });

    server.route("DELETE", "/api/recycle/permanent", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        bool removed = files.permanentDeleteFile(user->id, parseId(getQuery(request, "id")));
        if (!removed) {
            return jsonResponse(404, errorJson(404, "file not found in recycle bin"));
        }
        return jsonResponse(200, okJson(jsonObject({{"permanently_deleted", "true"}})));
    });

    server.route("POST", "/api/shares", [&](const HttpRequest& request) {
        auto user = requireUser(request, auth);
        if (!user) {
            return unauthorized();
        }
        try {
            auto body = parseFlatJsonObject(request.body);
            auto fileId = parseId(body["file_id"]);
            if (!store.findFile(user->id, fileId)) {
                return jsonResponse(404, errorJson(404, "file not found"));
            }
            auto share = store.createShare(user->id,
                                           fileId,
                                           newShareToken(),
                                           body["access_code"],
                                           body["allow_download"] != "false");
            return jsonResponse(201, okJson(jsonObject({
                                         {"token", share.token},
                                         {"url", "/api/public/share?token=" + share.token},
                                         {"allow_download", share.allowDownload ? "true" : "false"},
                                     })));
        } catch (const std::exception& ex) {
            return jsonResponse(400, errorJson(400, ex.what()));
        }
    });

    server.route("GET", "/api/public/share", [&](const HttpRequest& request) {
        auto share = store.findShare(getQuery(request, "token"));
        if (!share || !shareCodeMatches(*share, request)) {
            return jsonResponse(404, errorJson(404, "share not found or access code invalid"));
        }
        auto file = store.findFile(share->userId, share->fileId);
        if (!file) {
            return jsonResponse(404, errorJson(404, "shared file not found"));
        }
        store.increaseShareView(share->token);
        return jsonResponse(200, okJson(jsonObject({
                                     {"token", share->token},
                                     {"file_id", std::to_string(file->id)},
                                     {"name", file->name},
                                     {"size_bytes", std::to_string(file->sizeBytes)},
                                     {"allow_download", share->allowDownload ? "true" : "false"},
                                 })));
    });

    server.route("GET", "/api/public/download", [&](const HttpRequest& request) {
        auto share = store.findShare(getQuery(request, "token"));
        if (!share || !shareCodeMatches(*share, request) || !share->allowDownload) {
            return jsonResponse(404, errorJson(404, "share not found or access code invalid"));
        }
        try {
            auto file = files.getDownloadFile(share->userId, share->fileId);
            auto object = files.getObject(file.objectId);
            std::ifstream in(object.storagePath, std::ios::binary);
            std::ostringstream body;
            body << in.rdbuf();
            store.increaseShareDownload(share->token);
            HttpResponse response;
            response.status = 200;
            response.contentType = "application/octet-stream";
            response.headers["Content-Disposition"] = "attachment; filename=\"" + file.name + "\"";
            response.body = body.str();
            return response;
        } catch (const std::exception& ex) {
            return jsonResponse(404, errorJson(404, ex.what()));
        }
    });

    server.run();
}

