#include "prod/FileRoutes.h"

#include "prod/FileOps.h"
#include "prod/RouteBind.h"
#include "utils/Hash.h"
#include "utils/Json.h"
#include "utils/Path.h"

#include <fstream>
#include <sstream>

namespace cloud_disk {
namespace {

std::string createdFileJson(const CreatedFile& created) {
    return jsonObject({{"id", std::to_string(created.file.id)},
                       {"parent_id", std::to_string(created.file.parentId)},
                       {"object_id", std::to_string(created.file.objectId)},
                       {"name", created.file.name},
                       {"is_dir", "false"},
                       {"is_deleted", "false"},
                       {"size_bytes", std::to_string(created.file.sizeBytes)},
                       {"sha256", created.sha256}});
}

void writeChunkFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write chunk");
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string mergeChunks(const std::filesystem::path& dir, int totalChunks) {
    std::ostringstream merged;
    for (int i = 0; i < totalChunks; ++i) {
        std::ifstream in(dir / std::to_string(i), std::ios::binary);
        if (!in) {
            throw std::runtime_error("missing chunk: " + std::to_string(i));
        }
        merged << in.rdbuf();
    }
    return merged.str();
}

} // namespace

void registerFileRoutes(const MySqlDatabase& db,
                        const RedisSessionStore& sessions,
                        const std::filesystem::path& storageRoot) {
    ensureDirectory(storageRoot / "objects");
    ensureDirectory(storageRoot / "uploads");

    addAuthPost("/api/folders", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    auto body = parseFlatJsonObject(std::string(req->body()));
                    const auto& name = body["name"];
                    auto parentId = std::stoll(body["parent_id"]);

                    if (!isValidName(name) || !parentExists(db, user.id, parentId)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "invalid folder request"));
                        return;
                    }
                    if (nameTaken(db, user.id, parentId, name)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                        return;
                    }

                    db.execute("INSERT INTO files(user_id, parent_id, name, is_dir) VALUES("
                               + sqlNumber(user.id) + ", " + sqlNumber(parentId) + ", "
                               + db.quote(name) + ", TRUE)");
                    auto rows = db.query(
                        "SELECT id FROM files WHERE user_id = " + sqlNumber(user.id)
                        + " AND parent_id = " + sqlNumber(parentId) + " AND name = " + db.quote(name)
                        + " AND is_deleted = FALSE ORDER BY id DESC LIMIT 1");

                    FileRow file {asInt64(rows[0], "id"), parentId, 0, name, 0, true};
                    reply(jsonResponse(drogon::k201Created, okJson(fileJson(file))));
                });

    addAuthGet("/api/files", db, sessions,
               [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                   auto parentId = req->getParameter("parent_id").empty()
                                       ? 0
                                       : std::stoll(req->getParameter("parent_id"));
                   auto rows = db.query(
                       "SELECT id, parent_id, COALESCE(object_id, 0) object_id, name, size_bytes, is_dir "
                       "FROM files WHERE user_id = "
                       + sqlNumber(user.id) + " AND parent_id = " + sqlNumber(parentId)
                       + " AND is_deleted = FALSE ORDER BY is_dir DESC, id DESC");

                   std::vector<std::string> items;
                   items.reserve(rows.size());
                   for (const auto& row : rows) {
                       items.push_back(fileJson(FileRow {asInt64(row, "id"),
                                                         asInt64(row, "parent_id"),
                                                         asInt64(row, "object_id"),
                                                         row.at("name"),
                                                         asUInt64(row, "size_bytes"),
                                                         asBool(row, "is_dir")}));
                   }
                   reply(jsonResponse(drogon::k200OK, okJson(jsonArray(items))));
               });

    addAuthDelete("/api/files", db, sessions,
                  [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                      softDeleteTree(db, user.id, std::stoll(req->getParameter("id")));
                      reply(jsonResponse(drogon::k200OK, okJson(jsonObject({{"ok", "true"}}))));
                  },
                  drogon::k404NotFound);

    addAuthPost("/api/files/upload", db, sessions,
                [&db, storageRoot](const drogon::HttpRequestPtr& req, HttpCallback& reply,
                                   const UserContext& user) {
                    auto parentId = req->getParameter("parent_id").empty()
                                        ? 0
                                        : std::stoll(req->getParameter("parent_id"));
                    auto name = req->getParameter("name");
                    std::string content(req->body());

                    if (!isValidName(name) || content.empty() || !parentExists(db, user.id, parentId)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "invalid upload request"));
                        return;
                    }
                    if (nameTaken(db, user.id, parentId, name)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                        return;
                    }

                    auto created = createFileFromContent(db, storageRoot, user.id, parentId, name, content);
                    reply(jsonResponse(drogon::k201Created, okJson(createdFileJson(created))));
                });

    addAuthPost("/api/files/instant", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    auto body = parseFlatJsonObject(std::string(req->body()));
                    const auto& name = body["name"];
                    const auto& sha = body["sha256"];
                    auto parentId = std::stoll(body["parent_id"]);
                    auto sizeBytes = static_cast<std::uint64_t>(std::stoull(body["size_bytes"]));

                    if (!isValidName(name) || sha.empty() || !parentExists(db, user.id, parentId)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "invalid instant upload"));
                        return;
                    }
                    if (nameTaken(db, user.id, parentId, name)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                        return;
                    }

                    auto object = findObjectBySha(db, sha);
                    if (!object) {
                        reply(errorResponse(drogon::k404NotFound, 404, "object not found for hash"));
                        return;
                    }

                    auto created = attachExistingObject(db, user.id, parentId, name, asInt64(*object, "id"),
                                                        sizeBytes, sha);
                    reply(jsonResponse(drogon::k201Created, okJson(createdFileJson(created))));
                });

    addAuthGet("/api/files/download", db, sessions,
               [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                   auto file = findActiveFile(db, user.id, std::stoll(req->getParameter("id")));
                   if (!file || file->isDir) {
                       reply(errorResponse(drogon::k404NotFound, 404, "file not found"));
                       return;
                   }

                   auto path = objectPath(db, file->objectId);
                   if (!path) {
                       reply(errorResponse(drogon::k404NotFound, 404, "object not found"));
                       return;
                   }

                   auto response = drogon::HttpResponse::newFileResponse(*path, file->name);
                   response->addHeader("Access-Control-Expose-Headers", "Content-Disposition");
                   reply(response);
               },
               drogon::k404NotFound);

    addAuthGet("/api/recycle", db, sessions,
               [&db](const drogon::HttpRequestPtr&, HttpCallback& reply, const UserContext& user) {
                   std::vector<std::string> items;
                   for (const auto& file : listRecycle(db, user.id)) {
                       items.push_back(fileJson(file, true));
                   }
                   reply(jsonResponse(drogon::k200OK, okJson(jsonArray(items))));
               });

    addAuthPost("/api/recycle/restore", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    restoreTree(db, user.id, std::stoll(req->getParameter("id")));
                    reply(jsonResponse(drogon::k200OK, okJson(jsonObject({{"ok", "true"}}))));
                });

    addAuthDelete("/api/recycle/permanent", db, sessions,
                  [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                      permanentDeleteTree(db, user.id, std::stoll(req->getParameter("id")));
                      reply(jsonResponse(drogon::k200OK, okJson(jsonObject({{"ok", "true"}}))));
                  },
                  drogon::k404NotFound);

    addAuthPost("/api/uploads/init", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    auto body = parseFlatJsonObject(std::string(req->body()));
                    const auto& name = body["name"];
                    const auto& sha = body["sha256"];
                    auto parentId = std::stoll(body["parent_id"]);
                    auto sizeBytes = static_cast<std::uint64_t>(std::stoull(body["size_bytes"]));
                    auto chunkSize = static_cast<std::uint64_t>(std::stoull(body["chunk_size"]));
                    int totalChunks = std::stoi(body["total_chunks"]);

                    if (!isValidName(name) || sha.empty() || totalChunks <= 0 || chunkSize == 0
                        || !parentExists(db, user.id, parentId)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "invalid upload init"));
                        return;
                    }
                    if (nameTaken(db, user.id, parentId, name)) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                        return;
                    }
                    if (findObjectBySha(db, sha)) {
                        reply(jsonResponse(drogon::k200OK,
                                           okJson(jsonObject({{"status", "instant_available"},
                                                              {"sha256", sha}}))));
                        return;
                    }

                    auto uploadId = newUploadId();
                    db.execute(
                        "INSERT INTO upload_sessions(upload_id, user_id, parent_id, filename, sha256, "
                        "size_bytes, chunk_size, total_chunks, status, expires_at) VALUES("
                        + db.quote(uploadId) + ", " + sqlNumber(user.id) + ", " + sqlNumber(parentId) + ", "
                        + db.quote(name) + ", " + db.quote(sha) + ", " + sqlNumber(sizeBytes) + ", "
                        + sqlNumber(chunkSize) + ", " + sqlNumber(static_cast<std::int64_t>(totalChunks))
                        + ", 'uploading', DATE_ADD(NOW(), INTERVAL 1 DAY))");

                    reply(jsonResponse(drogon::k201Created,
                                       okJson(jsonObject({{"upload_id", uploadId},
                                                          {"status", "uploading"},
                                                          {"total_chunks",
                                                           std::to_string(totalChunks)}}))));
                });

    addAuthPost("/api/uploads/chunk", db, sessions,
                [&db, storageRoot](const drogon::HttpRequestPtr& req, HttpCallback& reply,
                                   const UserContext& user) {
                    auto uploadId = req->getParameter("upload_id");
                    int chunkIndex = std::stoi(req->getParameter("chunk_index"));
                    std::string content(req->body());

                    auto rows = db.query(
                        "SELECT upload_id, total_chunks, status FROM upload_sessions WHERE upload_id = "
                        + db.quote(uploadId) + " AND user_id = " + sqlNumber(user.id));
                    if (rows.empty() || rows[0]["status"] != "uploading") {
                        reply(errorResponse(drogon::k404NotFound, 404, "upload session not found"));
                        return;
                    }

                    int totalChunks = std::stoi(rows[0]["total_chunks"]);
                    if (chunkIndex < 0 || chunkIndex >= totalChunks || content.empty()) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "invalid chunk"));
                        return;
                    }

                    auto dir = uploadTempDir(storageRoot, uploadId);
                    writeChunkFile(dir / std::to_string(chunkIndex), content);
                    db.execute("INSERT INTO upload_chunks(upload_id, chunk_index, size_bytes) VALUES("
                               + db.quote(uploadId) + ", "
                               + sqlNumber(static_cast<std::int64_t>(chunkIndex)) + ", "
                               + sqlNumber(static_cast<std::uint64_t>(content.size()))
                               + ") ON DUPLICATE KEY UPDATE size_bytes = VALUES(size_bytes)");

                    reply(jsonResponse(drogon::k200OK,
                                       okJson(jsonObject({{"upload_id", uploadId},
                                                          {"chunk_index", std::to_string(chunkIndex)}}))));
                });

    addAuthPost("/api/uploads/complete", db, sessions,
                [&db, storageRoot](const drogon::HttpRequestPtr& req, HttpCallback& reply,
                                   const UserContext& user) {
                    auto uploadId = req->getParameter("upload_id");
                    auto rows = db.query(
                        "SELECT parent_id, filename, sha256, size_bytes, total_chunks, status "
                        "FROM upload_sessions WHERE upload_id = "
                        + db.quote(uploadId) + " AND user_id = " + sqlNumber(user.id));
                    if (rows.empty() || rows[0]["status"] != "uploading") {
                        reply(errorResponse(drogon::k404NotFound, 404, "upload session not found"));
                        return;
                    }

                    int totalChunks = std::stoi(rows[0]["total_chunks"]);
                    auto chunkRows = db.query("SELECT COUNT(*) cnt FROM upload_chunks WHERE upload_id = "
                                              + db.quote(uploadId));
                    if (asInt64(chunkRows[0], "cnt") != totalChunks) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "chunks incomplete"));
                        return;
                    }

                    auto dir = uploadTempDir(storageRoot, uploadId);
                    auto content = mergeChunks(dir, totalChunks);
                    if (contentHash(content) != rows[0]["sha256"]
                        || static_cast<std::uint64_t>(content.size()) != asUInt64(rows[0], "size_bytes")) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "content hash or size mismatch"));
                        return;
                    }
                    if (nameTaken(db, user.id, asInt64(rows[0], "parent_id"), rows[0]["filename"])) {
                        reply(errorResponse(drogon::k400BadRequest, 400, "file name already exists"));
                        return;
                    }

                    auto created = createFileFromContent(db, storageRoot, user.id,
                                                         asInt64(rows[0], "parent_id"),
                                                         rows[0]["filename"], content);
                    db.execute("UPDATE upload_sessions SET status = 'completed' WHERE upload_id = "
                               + db.quote(uploadId));

                    std::error_code ec;
                    std::filesystem::remove_all(dir, ec);
                    reply(jsonResponse(drogon::k201Created, okJson(createdFileJson(created))));
                });
}

} // namespace cloud_disk
