#pragma once

#include "models/Models.h"
#include "services/MetadataStore.h"

#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

namespace cloud_disk {

class AuthService {
public:
    explicit AuthService(MetadataStore& store);

    User registerUser(const std::string& username, const std::string& password);
    std::string login(const std::string& username, const std::string& password);
    std::optional<User> authenticate(const std::string& token) const;

private:
    static bool isValidUsername(const std::string& username);
    static bool isValidPassword(const std::string& password);
    static std::string hashPassword(const std::string& username, const std::string& password);
    std::string newToken();

    MetadataStore& store_;
    mutable std::mutex mutex_;
    std::mt19937_64 rng_;
    std::unordered_map<std::string, std::int64_t> sessions_;
};

} // namespace cloud_disk

