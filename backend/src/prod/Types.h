#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace cloud_disk {

using DbRow = std::map<std::string, std::string>;

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

}
