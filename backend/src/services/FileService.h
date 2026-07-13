#pragma once

#include "models/Models.h"
#include "services/MetadataStore.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cloud_disk {

class FileService {
public:
    FileService(MetadataStore& store, std::filesystem::path storageRoot);

    FileEntry createFolder(std::int64_t userId, std::int64_t parentId, const std::string& name);
    FileEntry uploadFile(std::int64_t userId,
                         std::int64_t parentId,
                         const std::string& name,
                         const std::string& content);
    FileEntry instantUpload(std::int64_t userId,
                            std::int64_t parentId,
                            const std::string& name,
                            const std::string& sha256,
                            std::uint64_t sizeBytes);
    UploadSession initChunkedUpload(std::int64_t userId,
                                    std::int64_t parentId,
                                    const std::string& name,
                                    const std::string& sha256,
                                    std::uint64_t sizeBytes,
                                    std::uint64_t chunkSize,
                                    int totalChunks);
    void uploadChunk(std::int64_t userId,
                     const std::string& uploadId,
                     int chunkIndex,
                     const std::string& content);
    std::vector<UploadedChunk> uploadProgress(std::int64_t userId, const std::string& uploadId) const;
    FileEntry completeChunkedUpload(std::int64_t userId, const std::string& uploadId);
    std::vector<FileEntry> listFiles(std::int64_t userId, std::int64_t parentId) const;
    std::vector<FileEntry> listRecycleBin(std::int64_t userId) const;
    FileEntry getDownloadFile(std::int64_t userId, std::int64_t fileId) const;
    FileObject getObject(std::int64_t objectId) const;
    bool deleteFile(std::int64_t userId, std::int64_t fileId);
    bool restoreFile(std::int64_t userId, std::int64_t fileId);
    bool permanentDeleteFile(std::int64_t userId, std::int64_t fileId);

private:
    FileEntry createFileFromContent(std::int64_t userId,
                                    std::int64_t parentId,
                                    const std::string& name,
                                    const std::string& content);

    MetadataStore& store_;
    std::filesystem::path objectRoot_;
    std::filesystem::path tempRoot_;
};

std::string fileEntryJson(const FileEntry& file);
std::string chunkJson(const UploadedChunk& chunk);

} // namespace cloud_disk

