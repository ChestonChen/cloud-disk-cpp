#include "prod/FileOps.h"

#include "prod/RouteUtils.h"
#include "utils/Hash.h"
#include "utils/Path.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cloud_disk {
namespace {

std::filesystem::path objectFilePath(const std::filesystem::path& storageRoot, const std::string& sha) {
    return ensureDirectory(storageRoot / "objects" / sha.substr(0, 2)) / sha;
}

void writeBytes(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write object file");
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

CreatedFile insertFileRow(const MySqlDatabase& db,
                          std::int64_t userId,
                          std::int64_t parentId,
                          std::int64_t objectId,
                          const std::string& name,
                          std::uint64_t sizeBytes,
                          const std::string& sha) {
    db.execute("INSERT INTO files(user_id, parent_id, object_id, name, size_bytes, is_dir) VALUES("
               + sqlNumber(userId) + ", " + sqlNumber(parentId) + ", " + sqlNumber(objectId) + ", "
               + db.quote(name) + ", " + sqlNumber(sizeBytes) + ", FALSE)");
    db.execute("UPDATE users SET storage_used = storage_used + " + sqlNumber(sizeBytes)
               + " WHERE id = " + sqlNumber(userId));
    auto id = asInt64(db.query("SELECT id FROM files WHERE user_id = " + sqlNumber(userId)
                               + " AND object_id = " + sqlNumber(objectId) + " AND name = " + db.quote(name)
                               + " ORDER BY id DESC LIMIT 1")[0],
                      "id");
    return CreatedFile {FileRow {id, parentId, objectId, name, sizeBytes, false}, sha};
}

void releaseObject(const MySqlDatabase& db, std::int64_t objectId) {
    if (objectId <= 0) {
        return;
    }
    db.execute("UPDATE file_objects SET ref_count = GREATEST(ref_count - 1, 0) WHERE id = "
               + sqlNumber(objectId));
    auto rows = db.query("SELECT ref_count, storage_path FROM file_objects WHERE id = " + sqlNumber(objectId));
    if (rows.empty()) {
        return;
    }
    if (asInt64(rows[0], "ref_count") > 0) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(rows[0]["storage_path"], ec);
    db.execute("DELETE FROM file_objects WHERE id = " + sqlNumber(objectId));
}

std::vector<std::int64_t> collectSubtree(const MySqlDatabase& db,
                                         std::int64_t userId,
                                         std::int64_t rootId,
                                         bool deletedOnly) {
    std::vector<std::int64_t> ids {rootId};
    std::vector<std::int64_t> queue {rootId};
    const char* deletedClause = deletedOnly ? "TRUE" : "FALSE";
    while (!queue.empty()) {
        auto parent = queue.back();
        queue.pop_back();
        auto kids = db.query("SELECT id FROM files WHERE user_id = " + sqlNumber(userId)
                             + " AND parent_id = " + sqlNumber(parent) + " AND is_deleted = "
                             + deletedClause);
        for (const auto& kid : kids) {
            auto id = asInt64(kid, "id");
            ids.push_back(id);
            queue.push_back(id);
        }
    }
    return ids;
}

}

bool nameTaken(const MySqlDatabase& db,
               std::int64_t userId,
               std::int64_t parentId,
               const std::string& name) {
    auto rows = db.query("SELECT id FROM files WHERE user_id = " + sqlNumber(userId)
                         + " AND parent_id = " + sqlNumber(parentId) + " AND name = " + db.quote(name)
                         + " AND is_deleted = FALSE");
    return !rows.empty();
}

std::optional<DbRow> findObjectBySha(const MySqlDatabase& db, const std::string& sha) {
    auto rows = db.query("SELECT id, storage_path, size_bytes, ref_count FROM file_objects WHERE sha256 = "
                         + db.quote(sha));
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows[0];
}

std::int64_t storeContentObject(const MySqlDatabase& db,
                                const std::filesystem::path& storageRoot,
                                const std::string& sha,
                                const std::string& content) {
    auto existing = findObjectBySha(db, sha);
    auto path = objectFilePath(storageRoot, sha);
    auto size = static_cast<std::uint64_t>(content.size());
    if (!existing) {
        writeBytes(path, content);
        db.execute("INSERT INTO file_objects(sha256, size_bytes, storage_path, ref_count) VALUES("
                   + db.quote(sha) + ", " + sqlNumber(size) + ", " + db.quote(path.string()) + ", 1)");
        return asInt64(db.query("SELECT id FROM file_objects WHERE sha256 = " + db.quote(sha))[0], "id");
    }

    auto objectId = asInt64(*existing, "id");
    if (!std::filesystem::exists((*existing)["storage_path"])) {
        writeBytes(path, content);
        db.execute("UPDATE file_objects SET storage_path = " + db.quote(path.string())
                   + ", size_bytes = " + sqlNumber(size) + " WHERE id = " + sqlNumber(objectId));
    }
    db.execute("UPDATE file_objects SET ref_count = ref_count + 1 WHERE id = " + sqlNumber(objectId));
    return objectId;
}

CreatedFile attachExistingObject(const MySqlDatabase& db,
                                 std::int64_t userId,
                                 std::int64_t parentId,
                                 const std::string& name,
                                 std::int64_t objectId,
                                 std::uint64_t sizeBytes,
                                 const std::string& sha) {
    db.execute("UPDATE file_objects SET ref_count = ref_count + 1 WHERE id = " + sqlNumber(objectId));
    return insertFileRow(db, userId, parentId, objectId, name, sizeBytes, sha);
}

CreatedFile createFileFromContent(const MySqlDatabase& db,
                                  const std::filesystem::path& storageRoot,
                                  std::int64_t userId,
                                  std::int64_t parentId,
                                  const std::string& name,
                                  const std::string& content) {
    auto sha = contentHash(content);
    auto objectId = storeContentObject(db, storageRoot, sha, content);
    return insertFileRow(db, userId, parentId, objectId, name,
                         static_cast<std::uint64_t>(content.size()), sha);
}

std::string newUploadId() {
    static std::mt19937_64 rng {std::random_device {}()};
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    out << std::setw(8) << (rng() & 0xffffffffULL) << '-';
    out << std::setw(4) << (rng() & 0xffffULL) << '-';
    out << std::setw(4) << ((rng() & 0x0fffULL) | 0x4000ULL) << '-';
    out << std::setw(4) << ((rng() & 0x3fffULL) | 0x8000ULL) << '-';
    out << std::setw(12) << (rng() & 0xffffffffffffULL);
    return out.str();
}

std::filesystem::path uploadTempDir(const std::filesystem::path& storageRoot, const std::string& uploadId) {
    return ensureDirectory(storageRoot / "uploads" / sanitizeTokenPathPart(uploadId));
}

void softDeleteTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId) {
    if (!findActiveFile(db, userId, fileId)) {
        throw std::runtime_error("file not found");
    }
    for (auto id : collectSubtree(db, userId, fileId, false)) {
        db.execute("UPDATE files SET is_deleted = TRUE, deleted_at = CURRENT_TIMESTAMP WHERE id = "
                   + sqlNumber(id) + " AND user_id = " + sqlNumber(userId));
    }
}

void restoreTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId) {
    auto file = findDeletedFile(db, userId, fileId);
    if (!file) {
        throw std::runtime_error("file not found in recycle");
    }
    if (nameTaken(db, userId, file->parentId, file->name)) {
        throw std::runtime_error("file name already exists");
    }
    for (auto id : collectSubtree(db, userId, fileId, true)) {
        db.execute("UPDATE files SET is_deleted = FALSE, deleted_at = NULL WHERE id = " + sqlNumber(id)
                   + " AND user_id = " + sqlNumber(userId));
    }
}

void permanentDeleteTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId) {
    auto file = findDeletedFile(db, userId, fileId);
    if (!file) {
        throw std::runtime_error("file not found in recycle");
    }
    auto ids = collectSubtree(db, userId, fileId, true);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        auto rows = db.query(
            "SELECT COALESCE(object_id, 0) object_id, size_bytes, is_dir FROM files WHERE id = "
            + sqlNumber(*it) + " AND user_id = " + sqlNumber(userId) + " AND is_deleted = TRUE");
        if (rows.empty()) {
            continue;
        }
        auto objectId = asInt64(rows[0], "object_id");
        auto isDir = asBool(rows[0], "is_dir");
        auto size = asUInt64(rows[0], "size_bytes");
        db.execute("DELETE FROM shares WHERE file_id = " + sqlNumber(*it));
        db.execute("DELETE FROM files WHERE id = " + sqlNumber(*it) + " AND user_id = " + sqlNumber(userId));
        if (!isDir) {
            db.execute("UPDATE users SET storage_used = GREATEST(storage_used - " + sqlNumber(size)
                       + ", 0) WHERE id = " + sqlNumber(userId));
            releaseObject(db, objectId);
        }
    }
}

std::vector<FileRow> listRecycle(const MySqlDatabase& db, std::int64_t userId) {
    auto rows = db.query(
        "SELECT id, parent_id, COALESCE(object_id, 0) object_id, name, size_bytes, is_dir "
        "FROM files WHERE user_id = "
        + sqlNumber(userId) + " AND is_deleted = TRUE ORDER BY deleted_at DESC, id DESC");
    std::vector<FileRow> files;
    files.reserve(rows.size());
    for (const auto& row : rows) {
        files.push_back(FileRow {asInt64(row, "id"),
                                 asInt64(row, "parent_id"),
                                 asInt64(row, "object_id"),
                                 row.at("name"),
                                 asUInt64(row, "size_bytes"),
                                 asBool(row, "is_dir")});
    }
    return files;
}

std::optional<FileRow> findDeletedFile(const MySqlDatabase& db,
                                       std::int64_t userId,
                                       std::int64_t fileId) {
    auto rows = db.query(
        "SELECT id, parent_id, COALESCE(object_id, 0) object_id, name, size_bytes, is_dir "
        "FROM files WHERE user_id = "
        + sqlNumber(userId) + " AND id = " + sqlNumber(fileId) + " AND is_deleted = TRUE");
    if (rows.empty()) {
        return std::nullopt;
    }
    auto row = rows[0];
    return FileRow {asInt64(row, "id"),
                    asInt64(row, "parent_id"),
                    asInt64(row, "object_id"),
                    row["name"],
                    asUInt64(row, "size_bytes"),
                    asBool(row, "is_dir")};
}

}
