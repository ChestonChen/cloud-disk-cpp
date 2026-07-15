#include "utils/Hash.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace cloud_disk {

// 根据文件内容生成稳定哈希；目前用轻量实现，后续可替换成 OpenSSL SHA-256。
std::string contentHash(const std::string& content) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : content) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        std::uint64_t mixed = hash + 0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(i + 1);
        mixed ^= (mixed >> 30);
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= (mixed >> 27);
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= (mixed >> 31);
        out << std::setw(16) << mixed;
    }
    return out.str();
}

} // namespace cloud_disk

