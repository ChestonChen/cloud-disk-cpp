#pragma once

#include "models/Models.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace cloud_disk {

class MetadataStore {
public:
    explicit MetadataStore(std::filesystem::path storageRoot);

    std::optional<User> findUserByUsername(const std::string& username) const;
    std::optional<User> findUserById(std::int64_t id) const;
    User createUser(const std::string& username,
                    const std::string& passwordHash,
                    const std::string& displayName);

    std::optional<FileEntry> findFile(std::int64_t userId, std::int64_t fileId) const;
    std::optional<FileEntry> findFileIncludingDeleted(std::int64_t userId, std::int64_t fileId) const;
    std::optional<FileEntry> findActiveChild(std::int64_t userId,
                                             std::int64_t parentId,
                                             const std::string& name) const;
    std::vector<FileEntry> listFiles(std::int64_t userId, std::int64_t parentId) const;
    std::vector<FileEntry> listRecycleBin(std::int64_t userId) const;
    FileEntry createFolder(std::int64_t userId, std::int64_t parentId, const std::string& name);
    FileEntry createFile(std::int64_t userId,
                         std::int64_t parentId,
                         const std::string& name,
                         std::int64_t objectId,
                         std::uint64_t sizeBytes);
    bool softDeleteFile(std::int64_t userId, std::int64_t fileId);
    bool restoreFile(std::int64_t userId, std::int64_t fileId);
    std::optional<FileEntry> permanentDeleteFile(std::int64_t userId, std::int64_t fileId);

    std::optional<FileObject> findObjectByHash(const std::string& sha256) const;
    std::optional<FileObject> findObjectById(std::int64_t objectId) const;
    FileObject createObject(const std::string& sha256,
                            std::uint64_t sizeBytes,
                            const std::string& storagePath);
    void incrementObjectRef(std::int64_t objectId);
    std::optional<FileObject> decrementObjectRef(std::int64_t objectId);

    UploadSession createUploadSession(std::int64_t userId,
                                      std::int64_t parentId,
                                      const std::string& filename,
                                      const std::string& sha256,
                                      std::uint64_t sizeBytes,
                                      std::uint64_t chunkSize,
                                      int totalChunks);
    std::optional<UploadSession> findUploadSession(std::int64_t userId,
                                                   const std::string& uploadId) const;
    void markChunkUploaded(const std::string& uploadId, int chunkIndex, std::uint64_t sizeBytes);
    std::vector<UploadedChunk> listChunks(const std::string& uploadId) const;
    void completeUploadSession(const std::string& uploadId);

    ShareLink createShare(std::int64_t userId,
                          std::int64_t fileId,
                          const std::string& token,
                          const std::string& accessCode,
                          bool allowDownload);
    std::optional<ShareLink> findShare(const std::string& token) const;
    void increaseShareView(const std::string& token);
    void increaseShareDownload(const std::string& token);

private:
    void load();
    void saveLocked() const;
    bool parentExistsLocked(std::int64_t userId, std::int64_t parentId) const;

    std::filesystem::path storageRoot_;
    std::filesystem::path metadataPath_;
    mutable std::mutex mutex_;
    std::vector<User> users_;
    std::vector<FileEntry> files_;
    std::vector<FileObject> objects_;
    std::vector<UploadSession> uploadSessions_;
    std::vector<UploadedChunk> chunks_;
    std::vector<ShareLink> shares_;
    std::int64_t nextUserId_ = 1;
    std::int64_t nextFileId_ = 1;
    std::int64_t nextObjectId_ = 1;
};

} // namespace cloud_disk

