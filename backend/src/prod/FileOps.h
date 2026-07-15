#pragma once

#include "prod/MySqlDatabase.h"
#include "prod/Types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cloud_disk {

struct CreatedFile {
    FileRow file;
    std::string sha256;
};

// 同目录下是否已有未删除的同名项。
bool nameTaken(const MySqlDatabase& db,
               std::int64_t userId,
               std::int64_t parentId,
               const std::string& name);

// 按内容哈希查找已有文件对象。
std::optional<DbRow> findObjectBySha(const MySqlDatabase& db, const std::string& sha);

// 把内容写入对象存储（已存在则复用并增加引用），返回 object_id。
std::int64_t storeContentObject(const MySqlDatabase& db,
                                const std::filesystem::path& storageRoot,
                                const std::string& sha,
                                const std::string& content);

// 复用已有对象：增加引用并挂一条用户文件记录。
CreatedFile attachExistingObject(const MySqlDatabase& db,
                                 std::int64_t userId,
                                 std::int64_t parentId,
                                 const std::string& name,
                                 std::int64_t objectId,
                                 std::uint64_t sizeBytes,
                                 const std::string& sha);

// 写入内容并创建用户文件记录（上传/分片完成共用）。
CreatedFile createFileFromContent(const MySqlDatabase& db,
                                  const std::filesystem::path& storageRoot,
                                  std::int64_t userId,
                                  std::int64_t parentId,
                                  const std::string& name,
                                  const std::string& content);

// 生成 upload_sessions 用的 UUID 风格 id。
std::string newUploadId();

// 分片临时目录。
std::filesystem::path uploadTempDir(const std::filesystem::path& storageRoot,
                                    const std::string& uploadId);

// 软删除文件或文件夹（文件夹会一并软删未删除的子孙）。
void softDeleteTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId);

// 从回收站恢复（文件夹会恢复仍挂在其下的已删子孙）。
void restoreTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId);

// 永久删除（文件夹会删掉整棵已删子树），并处理对象引用与容量。
void permanentDeleteTree(const MySqlDatabase& db, std::int64_t userId, std::int64_t fileId);

// 列出回收站中的条目。
std::vector<FileRow> listRecycle(const MySqlDatabase& db, std::int64_t userId);

// 查询用户自己的已删除文件。
std::optional<FileRow> findDeletedFile(const MySqlDatabase& db,
                                       std::int64_t userId,
                                       std::int64_t fileId);

} // namespace cloud_disk
