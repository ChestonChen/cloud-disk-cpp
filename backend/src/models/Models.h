#pragma once

#include <cstdint>
#include <string>

namespace cloud_disk {

struct User {
    std::int64_t id = 0;
    std::string username;
    std::string passwordHash;
    std::string displayName;
    std::uint64_t storageUsed = 0;
    std::uint64_t storageLimit = 1024ULL * 1024ULL * 1024ULL;
};

struct FileEntry {
    std::int64_t id = 0;
    std::int64_t userId = 0;
    std::int64_t parentId = 0;
    std::int64_t objectId = 0;
    std::string name;
    std::uint64_t sizeBytes = 0;
    bool isDir = false;
    bool isDeleted = false;
};

struct FileObject {
    std::int64_t id = 0;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::string storagePath;
    std::uint64_t refCount = 0;
};

struct UploadSession {
    std::string uploadId;
    std::int64_t userId = 0;
    std::int64_t parentId = 0;
    std::string filename;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::uint64_t chunkSize = 0;
    int totalChunks = 0;
    std::string status = "uploading";
};

struct UploadedChunk {
    std::string uploadId;
    int chunkIndex = 0;
    std::uint64_t sizeBytes = 0;
};

struct ShareLink {
    std::string token;
    std::int64_t userId = 0;
    std::int64_t fileId = 0;
    std::string accessCode;
    bool allowDownload = true;
    bool isActive = true;
    std::uint64_t viewCount = 0;
    std::uint64_t downloadCount = 0;
};

} // namespace cloud_disk

