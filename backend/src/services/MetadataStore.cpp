#include "services/MetadataStore.h"

#include "utils/Path.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cloud_disk {
namespace {

std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

} // namespace

MetadataStore::MetadataStore(std::filesystem::path storageRoot)
    : storageRoot_(std::move(storageRoot)), metadataPath_(storageRoot_ / "metadata.tsv") {
    ensureDirectory(storageRoot_);
    ensureDirectory(storageRoot_ / "objects");
    ensureDirectory(storageRoot_ / "tmp");
    load();
}

std::optional<User> MetadataStore::findUserByUsername(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user : users_) {
        if (user.username == username) {
            return user;
        }
    }
    return std::nullopt;
}

std::optional<User> MetadataStore::findUserById(std::int64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user : users_) {
        if (user.id == id) {
            return user;
        }
    }
    return std::nullopt;
}

User MetadataStore::createUser(const std::string& username,
                               const std::string& passwordHash,
                               const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user : users_) {
        if (user.username == username) {
            throw std::runtime_error("username already exists");
        }
    }

    User user;
    user.id = nextUserId_++;
    user.username = username;
    user.passwordHash = passwordHash;
    user.displayName = displayName;
    users_.push_back(user);
    saveLocked();
    return user;
}

std::optional<FileEntry> MetadataStore::findFile(std::int64_t userId, std::int64_t fileId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& file : files_) {
        if (file.userId == userId && file.id == fileId && !file.isDeleted) {
            return file;
        }
    }
    return std::nullopt;
}

std::optional<FileEntry> MetadataStore::findFileIncludingDeleted(std::int64_t userId,
                                                                 std::int64_t fileId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& file : files_) {
        if (file.userId == userId && file.id == fileId) {
            return file;
        }
    }
    return std::nullopt;
}

std::optional<FileEntry> MetadataStore::findActiveChild(std::int64_t userId,
                                                        std::int64_t parentId,
                                                        const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& file : files_) {
        if (file.userId == userId && file.parentId == parentId && file.name == name
            && !file.isDeleted) {
            return file;
        }
    }
    return std::nullopt;
}

std::vector<FileEntry> MetadataStore::listFiles(std::int64_t userId, std::int64_t parentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileEntry> result;
    for (const auto& file : files_) {
        if (file.userId == userId && file.parentId == parentId && !file.isDeleted) {
            result.push_back(file);
        }
    }
    return result;
}

std::vector<FileEntry> MetadataStore::listRecycleBin(std::int64_t userId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileEntry> result;
    for (const auto& file : files_) {
        if (file.userId == userId && file.isDeleted) {
            result.push_back(file);
        }
    }
    return result;
}

FileEntry MetadataStore::createFolder(std::int64_t userId,
                                      std::int64_t parentId,
                                      const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parentExistsLocked(userId, parentId)) {
        throw std::runtime_error("parent folder not found");
    }
    for (const auto& file : files_) {
        if (file.userId == userId && file.parentId == parentId && file.name == name
            && !file.isDeleted) {
            throw std::runtime_error("file name already exists");
        }
    }

    FileEntry entry;
    entry.id = nextFileId_++;
    entry.userId = userId;
    entry.parentId = parentId;
    entry.name = name;
    entry.isDir = true;
    files_.push_back(entry);
    saveLocked();
    return entry;
}

FileEntry MetadataStore::createFile(std::int64_t userId,
                                    std::int64_t parentId,
                                    const std::string& name,
                                    std::int64_t objectId,
                                    std::uint64_t sizeBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parentExistsLocked(userId, parentId)) {
        throw std::runtime_error("parent folder not found");
    }
    for (const auto& file : files_) {
        if (file.userId == userId && file.parentId == parentId && file.name == name
            && !file.isDeleted) {
            throw std::runtime_error("file name already exists");
        }
    }

    FileEntry entry;
    entry.id = nextFileId_++;
    entry.userId = userId;
    entry.parentId = parentId;
    entry.objectId = objectId;
    entry.name = name;
    entry.sizeBytes = sizeBytes;
    entry.isDir = false;
    files_.push_back(entry);
    for (auto& user : users_) {
        if (user.id == userId) {
            user.storageUsed += sizeBytes;
            break;
        }
    }
    saveLocked();
    return entry;
}

bool MetadataStore::softDeleteFile(std::int64_t userId, std::int64_t fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    std::vector<std::int64_t> deletedIds;
    for (auto& file : files_) {
        if (file.userId == userId && file.id == fileId && !file.isDeleted) {
            file.isDeleted = true;
            deletedIds.push_back(file.id);
            changed = true;
        }
    }
    for (std::size_t i = 0; i < deletedIds.size(); ++i) {
        for (auto& file : files_) {
            if (file.userId == userId && file.parentId == deletedIds[i] && !file.isDeleted) {
                file.isDeleted = true;
                deletedIds.push_back(file.id);
                changed = true;
            }
        }
    }
    if (changed) {
        saveLocked();
    }
    return changed;
}

bool MetadataStore::restoreFile(std::int64_t userId, std::int64_t fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    std::vector<std::int64_t> restoredIds;
    for (auto& file : files_) {
        if (file.userId == userId && file.id == fileId && file.isDeleted) {
            file.isDeleted = false;
            restoredIds.push_back(file.id);
            changed = true;
        }
    }
    for (std::size_t i = 0; i < restoredIds.size(); ++i) {
        for (auto& file : files_) {
            if (file.userId == userId && file.parentId == restoredIds[i] && file.isDeleted) {
                file.isDeleted = false;
                restoredIds.push_back(file.id);
                changed = true;
            }
        }
    }
    if (changed) {
        saveLocked();
    }
    return changed;
}

std::optional<FileEntry> MetadataStore::permanentDeleteFile(std::int64_t userId,
                                                            std::int64_t fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(files_.begin(), files_.end(), [&](const FileEntry& file) {
        return file.userId == userId && file.id == fileId && file.isDeleted;
    });
    if (it == files_.end()) {
        return std::nullopt;
    }
    FileEntry removed = *it;
    files_.erase(it);
    for (auto& user : users_) {
        if (user.id == userId) {
            user.storageUsed = user.storageUsed > removed.sizeBytes ? user.storageUsed - removed.sizeBytes : 0;
            break;
        }
    }
    saveLocked();
    return removed;
}

std::optional<FileObject> MetadataStore::findObjectByHash(const std::string& sha256) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& object : objects_) {
        if (object.sha256 == sha256) {
            return object;
        }
    }
    return std::nullopt;
}

std::optional<FileObject> MetadataStore::findObjectById(std::int64_t objectId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& object : objects_) {
        if (object.id == objectId) {
            return object;
        }
    }
    return std::nullopt;
}

FileObject MetadataStore::createObject(const std::string& sha256,
                                       std::uint64_t sizeBytes,
                                       const std::string& storagePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    FileObject object;
    object.id = nextObjectId_++;
    object.sha256 = sha256;
    object.sizeBytes = sizeBytes;
    object.storagePath = storagePath;
    object.refCount = 1;
    objects_.push_back(object);
    saveLocked();
    return object;
}

void MetadataStore::incrementObjectRef(std::int64_t objectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& object : objects_) {
        if (object.id == objectId) {
            ++object.refCount;
            saveLocked();
            return;
        }
    }
    throw std::runtime_error("object not found");
}

std::optional<FileObject> MetadataStore::decrementObjectRef(std::int64_t objectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = objects_.begin(); it != objects_.end(); ++it) {
        if (it->id == objectId) {
            if (it->refCount > 0) {
                --it->refCount;
            }
            FileObject object = *it;
            if (it->refCount == 0) {
                objects_.erase(it);
            }
            saveLocked();
            return object;
        }
    }
    return std::nullopt;
}

UploadSession MetadataStore::createUploadSession(std::int64_t userId,
                                                 std::int64_t parentId,
                                                 const std::string& filename,
                                                 const std::string& sha256,
                                                 std::uint64_t sizeBytes,
                                                 std::uint64_t chunkSize,
                                                 int totalChunks) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parentExistsLocked(userId, parentId)) {
        throw std::runtime_error("parent folder not found");
    }
    UploadSession session;
    session.uploadId = std::to_string(userId) + "-" + std::to_string(nextFileId_) + "-"
                       + std::to_string(uploadSessions_.size() + 1);
    session.userId = userId;
    session.parentId = parentId;
    session.filename = filename;
    session.sha256 = sha256;
    session.sizeBytes = sizeBytes;
    session.chunkSize = chunkSize;
    session.totalChunks = totalChunks;
    uploadSessions_.push_back(session);
    saveLocked();
    return session;
}

std::optional<UploadSession> MetadataStore::findUploadSession(std::int64_t userId,
                                                              const std::string& uploadId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : uploadSessions_) {
        if (session.userId == userId && session.uploadId == uploadId) {
            return session;
        }
    }
    return std::nullopt;
}

void MetadataStore::markChunkUploaded(const std::string& uploadId,
                                      int chunkIndex,
                                      std::uint64_t sizeBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& chunk : chunks_) {
        if (chunk.uploadId == uploadId && chunk.chunkIndex == chunkIndex) {
            chunk.sizeBytes = sizeBytes;
            saveLocked();
            return;
        }
    }
    chunks_.push_back(UploadedChunk {uploadId, chunkIndex, sizeBytes});
    saveLocked();
}

std::vector<UploadedChunk> MetadataStore::listChunks(const std::string& uploadId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UploadedChunk> result;
    for (const auto& chunk : chunks_) {
        if (chunk.uploadId == uploadId) {
            result.push_back(chunk);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.chunkIndex < b.chunkIndex;
    });
    return result;
}

void MetadataStore::completeUploadSession(const std::string& uploadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& session : uploadSessions_) {
        if (session.uploadId == uploadId) {
            session.status = "completed";
            saveLocked();
            return;
        }
    }
}

ShareLink MetadataStore::createShare(std::int64_t userId,
                                     std::int64_t fileId,
                                     const std::string& token,
                                     const std::string& accessCode,
                                     bool allowDownload) {
    std::lock_guard<std::mutex> lock(mutex_);
    ShareLink share;
    share.token = token;
    share.userId = userId;
    share.fileId = fileId;
    share.accessCode = accessCode;
    share.allowDownload = allowDownload;
    shares_.push_back(share);
    saveLocked();
    return share;
}

std::optional<ShareLink> MetadataStore::findShare(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& share : shares_) {
        if (share.token == token && share.isActive) {
            return share;
        }
    }
    return std::nullopt;
}

void MetadataStore::increaseShareView(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& share : shares_) {
        if (share.token == token) {
            ++share.viewCount;
            saveLocked();
            return;
        }
    }
}

void MetadataStore::increaseShareDownload(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& share : shares_) {
        if (share.token == token) {
            ++share.downloadCount;
            saveLocked();
            return;
        }
    }
}

void MetadataStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(metadataPath_);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto f = splitTab(line);
        if (f.empty()) {
            continue;
        }
        if (f[0] == "user" && f.size() == 7) {
            User user;
            user.id = std::stoll(f[1]);
            user.username = f[2];
            user.passwordHash = f[3];
            user.displayName = f[4];
            user.storageUsed = static_cast<std::uint64_t>(std::stoull(f[5]));
            user.storageLimit = static_cast<std::uint64_t>(std::stoull(f[6]));
            nextUserId_ = std::max(nextUserId_, user.id + 1);
            users_.push_back(user);
        } else if (f[0] == "file" && f.size() == 9) {
            FileEntry file;
            file.id = std::stoll(f[1]);
            file.userId = std::stoll(f[2]);
            file.parentId = std::stoll(f[3]);
            file.objectId = std::stoll(f[4]);
            file.name = f[5];
            file.sizeBytes = static_cast<std::uint64_t>(std::stoull(f[6]));
            file.isDir = f[7] == "1";
            file.isDeleted = f[8] == "1";
            nextFileId_ = std::max(nextFileId_, file.id + 1);
            files_.push_back(file);
        } else if (f[0] == "object" && f.size() == 6) {
            FileObject object;
            object.id = std::stoll(f[1]);
            object.sha256 = f[2];
            object.sizeBytes = static_cast<std::uint64_t>(std::stoull(f[3]));
            object.storagePath = f[4];
            object.refCount = static_cast<std::uint64_t>(std::stoull(f[5]));
            nextObjectId_ = std::max(nextObjectId_, object.id + 1);
            objects_.push_back(object);
        } else if (f[0] == "session" && f.size() == 9) {
            UploadSession session;
            session.uploadId = f[1];
            session.userId = std::stoll(f[2]);
            session.parentId = std::stoll(f[3]);
            session.filename = f[4];
            session.sha256 = f[5];
            session.sizeBytes = static_cast<std::uint64_t>(std::stoull(f[6]));
            session.chunkSize = static_cast<std::uint64_t>(std::stoull(f[7]));
            session.totalChunks = std::stoi(f[8]);
            uploadSessions_.push_back(session);
        } else if (f[0] == "chunk" && f.size() == 4) {
            chunks_.push_back(UploadedChunk {f[1], std::stoi(f[2]),
                                             static_cast<std::uint64_t>(std::stoull(f[3]))});
        } else if (f[0] == "share" && f.size() == 10) {
            ShareLink share;
            share.token = f[1];
            share.userId = std::stoll(f[2]);
            share.fileId = std::stoll(f[3]);
            share.accessCode = f[4];
            share.allowDownload = f[5] == "1";
            share.isActive = f[6] == "1";
            share.viewCount = static_cast<std::uint64_t>(std::stoull(f[7]));
            share.downloadCount = static_cast<std::uint64_t>(std::stoull(f[8]));
            shares_.push_back(share);
        }
    }
}

void MetadataStore::saveLocked() const {
    std::filesystem::path tmp = metadataPath_;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write metadata");
    }

    for (const auto& user : users_) {
        out << "user\t" << user.id << '\t' << user.username << '\t' << user.passwordHash << '\t'
            << user.displayName << '\t' << user.storageUsed << '\t' << user.storageLimit << '\n';
    }
    for (const auto& file : files_) {
        out << "file\t" << file.id << '\t' << file.userId << '\t' << file.parentId << '\t'
            << file.objectId << '\t' << file.name << '\t' << file.sizeBytes << '\t'
            << (file.isDir ? "1" : "0") << '\t' << (file.isDeleted ? "1" : "0") << '\n';
    }
    for (const auto& object : objects_) {
        out << "object\t" << object.id << '\t' << object.sha256 << '\t' << object.sizeBytes << '\t'
            << object.storagePath << '\t' << object.refCount << '\n';
    }
    for (const auto& session : uploadSessions_) {
        out << "session\t" << session.uploadId << '\t' << session.userId << '\t'
            << session.parentId << '\t' << session.filename << '\t' << session.sha256 << '\t'
            << session.sizeBytes << '\t' << session.chunkSize << '\t' << session.totalChunks << '\n';
    }
    for (const auto& chunk : chunks_) {
        out << "chunk\t" << chunk.uploadId << '\t' << chunk.chunkIndex << '\t' << chunk.sizeBytes
            << '\n';
    }
    for (const auto& share : shares_) {
        out << "share\t" << share.token << '\t' << share.userId << '\t' << share.fileId << '\t'
            << share.accessCode << '\t' << (share.allowDownload ? "1" : "0") << '\t'
            << (share.isActive ? "1" : "0") << '\t' << share.viewCount << '\t'
            << share.downloadCount << "\t0\n";
    }
    out.close();
    std::filesystem::rename(tmp, metadataPath_);
}

bool MetadataStore::parentExistsLocked(std::int64_t userId, std::int64_t parentId) const {
    if (parentId == 0) {
        return true;
    }
    for (const auto& file : files_) {
        if (file.userId == userId && file.id == parentId && file.isDir && !file.isDeleted) {
            return true;
        }
    }
    return false;
}

} // namespace cloud_disk

