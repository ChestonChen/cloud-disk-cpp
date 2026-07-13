#include "services/FileService.h"

#include "utils/Hash.h"
#include "utils/Json.h"
#include "utils/Path.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cloud_disk {
namespace {

std::vector<FileEntry> collectDeletedSubtree(const std::vector<FileEntry>& recycleBin,
                                             std::int64_t rootId) {
    std::vector<FileEntry> result;
    for (const auto& file : recycleBin) {
        if (file.id == rootId) {
            result.push_back(file);
            break;
        }
    }

    for (std::size_t i = 0; i < result.size(); ++i) {
        for (const auto& file : recycleBin) {
            if (file.parentId == result[i].id) {
                result.push_back(file);
            }
        }
    }
    return result;
}

} // namespace

FileService::FileService(MetadataStore& store, std::filesystem::path storageRoot)
    : store_(store),
      objectRoot_(storageRoot / "objects"),
      tempRoot_(std::move(storageRoot) / "tmp") {
    ensureDirectory(objectRoot_);
    ensureDirectory(tempRoot_);
}

FileEntry FileService::createFolder(std::int64_t userId,
                                    std::int64_t parentId,
                                    const std::string& name) {
    if (!isValidName(name)) {
        throw std::runtime_error("invalid folder name");
    }
    return store_.createFolder(userId, parentId, name);
}

FileEntry FileService::uploadFile(std::int64_t userId,
                                  std::int64_t parentId,
                                  const std::string& name,
                                  const std::string& content) {
    return createFileFromContent(userId, parentId, name, content);
}

FileEntry FileService::instantUpload(std::int64_t userId,
                                     std::int64_t parentId,
                                     const std::string& name,
                                     const std::string& sha256,
                                     std::uint64_t sizeBytes) {
    if (!isValidName(name)) {
        throw std::runtime_error("invalid file name");
    }
    if (store_.findActiveChild(userId, parentId, name)) {
        throw std::runtime_error("file name already exists");
    }
    auto object = store_.findObjectByHash(sha256);
    if (!object || object->sizeBytes != sizeBytes) {
        throw std::runtime_error("object not found for instant upload");
    }
    store_.incrementObjectRef(object->id);
    return store_.createFile(userId, parentId, name, object->id, sizeBytes);
}

UploadSession FileService::initChunkedUpload(std::int64_t userId,
                                             std::int64_t parentId,
                                             const std::string& name,
                                             const std::string& sha256,
                                             std::uint64_t sizeBytes,
                                             std::uint64_t chunkSize,
                                             int totalChunks) {
    if (!isValidName(name)) {
        throw std::runtime_error("invalid file name");
    }
    if (chunkSize == 0 || totalChunks <= 0) {
        throw std::runtime_error("invalid chunk config");
    }
    auto object = store_.findObjectByHash(sha256);
    if (object && object->sizeBytes == sizeBytes) {
        UploadSession session;
        session.uploadId = "instant";
        session.userId = userId;
        session.parentId = parentId;
        session.filename = name;
        session.sha256 = sha256;
        session.sizeBytes = sizeBytes;
        session.chunkSize = chunkSize;
        session.totalChunks = totalChunks;
        session.status = "instant_available";
        return session;
    }
    auto session = store_.createUploadSession(userId, parentId, name, sha256, sizeBytes, chunkSize, totalChunks);
    ensureDirectory(tempRoot_ / session.uploadId);
    return session;
}

void FileService::uploadChunk(std::int64_t userId,
                              const std::string& uploadId,
                              int chunkIndex,
                              const std::string& content) {
    auto session = store_.findUploadSession(userId, uploadId);
    if (!session || session->status != "uploading") {
        throw std::runtime_error("upload session not found");
    }
    if (chunkIndex < 0 || chunkIndex >= session->totalChunks) {
        throw std::runtime_error("chunk index out of range");
    }
    auto dir = ensureDirectory(tempRoot_ / uploadId);
    auto path = dir / (std::to_string(chunkIndex) + ".part");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write chunk");
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    store_.markChunkUploaded(uploadId, chunkIndex, content.size());
}

std::vector<UploadedChunk> FileService::uploadProgress(std::int64_t userId,
                                                       const std::string& uploadId) const {
    auto session = store_.findUploadSession(userId, uploadId);
    if (!session) {
        throw std::runtime_error("upload session not found");
    }
    return store_.listChunks(uploadId);
}

FileEntry FileService::completeChunkedUpload(std::int64_t userId, const std::string& uploadId) {
    auto session = store_.findUploadSession(userId, uploadId);
    if (!session || session->status != "uploading") {
        throw std::runtime_error("upload session not found");
    }
    auto chunks = store_.listChunks(uploadId);
    if (static_cast<int>(chunks.size()) != session->totalChunks) {
        throw std::runtime_error("not all chunks uploaded");
    }

    std::string content;
    for (int i = 0; i < session->totalChunks; ++i) {
        auto path = tempRoot_ / uploadId / (std::to_string(i) + ".part");
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("chunk file missing");
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        content += buffer.str();
    }
    if (content.size() != session->sizeBytes) {
        throw std::runtime_error("merged file size mismatch");
    }
    if (contentHash(content) != session->sha256) {
        throw std::runtime_error("merged file hash mismatch");
    }
    auto entry = createFileFromContent(userId, session->parentId, session->filename, content);
    store_.completeUploadSession(uploadId);
    std::filesystem::remove_all(tempRoot_ / uploadId);
    return entry;
}

std::vector<FileEntry> FileService::listFiles(std::int64_t userId, std::int64_t parentId) const {
    return store_.listFiles(userId, parentId);
}

std::vector<FileEntry> FileService::listRecycleBin(std::int64_t userId) const {
    return store_.listRecycleBin(userId);
}

FileEntry FileService::getDownloadFile(std::int64_t userId, std::int64_t fileId) const {
    auto file = store_.findFile(userId, fileId);
    if (!file) {
        throw std::runtime_error("file not found");
    }
    if (file->isDir) {
        throw std::runtime_error("folder cannot be downloaded directly");
    }
    auto object = store_.findObjectById(file->objectId);
    if (!object || !std::filesystem::exists(object->storagePath)) {
        throw std::runtime_error("physical object missing");
    }
    return *file;
}

FileObject FileService::getObject(std::int64_t objectId) const {
    auto object = store_.findObjectById(objectId);
    if (!object) {
        throw std::runtime_error("object not found");
    }
    return *object;
}

bool FileService::deleteFile(std::int64_t userId, std::int64_t fileId) {
    return store_.softDeleteFile(userId, fileId);
}

bool FileService::restoreFile(std::int64_t userId, std::int64_t fileId) {
    return store_.restoreFile(userId, fileId);
}

bool FileService::permanentDeleteFile(std::int64_t userId, std::int64_t fileId) {
    auto targets = collectDeletedSubtree(store_.listRecycleBin(userId), fileId);
    if (targets.empty()) {
        return false;
    }

    for (const auto& target : targets) {
        auto removed = store_.permanentDeleteFile(userId, target.id);
        if (!removed || removed->isDir || removed->objectId == 0) {
            continue;
        }

        auto object = store_.decrementObjectRef(removed->objectId);
        if (object && object->refCount == 0) {
            std::filesystem::remove(object->storagePath);
        }
    }
    return true;
}

FileEntry FileService::createFileFromContent(std::int64_t userId,
                                             std::int64_t parentId,
                                             const std::string& name,
                                             const std::string& content) {
    if (!isValidName(name)) {
        throw std::runtime_error("invalid file name");
    }
    if (content.empty()) {
        throw std::runtime_error("empty file is not supported");
    }
    if (store_.findActiveChild(userId, parentId, name)) {
        throw std::runtime_error("file name already exists");
    }

    std::string hash = contentHash(content);
    auto object = store_.findObjectByHash(hash);
    if (object) {
        store_.incrementObjectRef(object->id);
        return store_.createFile(userId, parentId, name, object->id, content.size());
    }

    auto objectDir = ensureDirectory(objectRoot_ / hash.substr(0, 2));
    auto objectPath = objectDir / hash;
    {
        std::ofstream out(objectPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to write upload object");
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    auto created = store_.createObject(hash, content.size(), objectPath.string());
    return store_.createFile(userId, parentId, name, created.id, content.size());
}

std::string fileEntryJson(const FileEntry& file) {
    JsonObject fields;
    fields["id"] = std::to_string(file.id);
    fields["parent_id"] = std::to_string(file.parentId);
    fields["object_id"] = std::to_string(file.objectId);
    fields["name"] = file.name;
    fields["is_dir"] = file.isDir ? "true" : "false";
    fields["is_deleted"] = file.isDeleted ? "true" : "false";
    fields["size_bytes"] = std::to_string(file.sizeBytes);
    return jsonObject(fields);
}

std::string chunkJson(const UploadedChunk& chunk) {
    return jsonObject({
        {"chunk_index", std::to_string(chunk.chunkIndex)},
        {"size_bytes", std::to_string(chunk.sizeBytes)},
    });
}

} // namespace cloud_disk

