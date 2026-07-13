#include "services/AuthService.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cloud_disk {

AuthService::AuthService(MetadataStore& store)
    : store_(store),
      rng_(static_cast<std::mt19937_64::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

User AuthService::registerUser(const std::string& username, const std::string& password) {
    if (!isValidUsername(username)) {
        throw std::runtime_error("username must be 3-64 characters and use letters, numbers, _, -");
    }
    if (!isValidPassword(password)) {
        throw std::runtime_error("password must be at least 6 characters");
    }
    return store_.createUser(username, hashPassword(username, password), username);
}

std::string AuthService::login(const std::string& username, const std::string& password) {
    auto user = store_.findUserByUsername(username);
    if (!user || user->passwordHash != hashPassword(username, password)) {
        throw std::runtime_error("invalid username or password");
    }

    std::string token = newToken();
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = user->id;
    return token;
}

std::optional<User> AuthService::authenticate(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return store_.findUserById(it->second);
}

bool AuthService::isValidUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 64) {
        return false;
    }
    for (char ch : username) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                  || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool AuthService::isValidPassword(const std::string& password) {
    return password.size() >= 6 && password.size() <= 128;
}

std::string AuthService::hashPassword(const std::string& username, const std::string& password) {
    // MVP-only hash to avoid external dependencies; replace with Argon2/bcrypt in production.
    std::hash<std::string> hasher;
    std::size_t value = hasher("cloud-disk:" + username + ":" + password);
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

std::string AuthService::newToken() {
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) {
        out << std::hex << std::setw(16) << std::setfill('0') << rng_();
    }
    return out.str();
}

} // namespace cloud_disk

