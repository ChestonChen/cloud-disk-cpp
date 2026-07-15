#include "prod/AuthRoutes.h"

#include "prod/RouteBind.h"
#include "utils/Json.h"

namespace cloud_disk {

void registerAuthRoutes(const MySqlDatabase& db, const RedisSessionStore& sessions) {
    addPost("/api/user/register", [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
        auto body = parseFlatJsonObject(std::string(req->body()));
        const auto& username = body["username"];
        const auto& password = body["password"];

        if (!validUsername(username) || password.size() < 6 || password.size() > 128) {
            reply(errorResponse(drogon::k400BadRequest, 400, "invalid username or password"));
            return;
        }

        db.execute("INSERT INTO users(username, password_hash, display_name) VALUES("
                   + db.quote(username) + ", "
                   + db.quote(hashPassword(username, password)) + ", "
                   + db.quote(username) + ")");

        auto rows = db.query("SELECT id, username FROM users WHERE username = " + db.quote(username));
        reply(jsonResponse(drogon::k201Created,
                           okJson(jsonObject({{"id", rows[0]["id"]}, {"username", username}}))));
    });

    addPost(
        "/api/user/login",
        [&db, &sessions](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            auto body = parseFlatJsonObject(std::string(req->body()));
            const auto& username = body["username"];

            auto rows = db.query("SELECT id, password_hash FROM users WHERE username = "
                                 + db.quote(username));
            if (rows.empty() || rows[0]["password_hash"] != hashPassword(username, body["password"])) {
                reply(errorResponse(drogon::k401Unauthorized, 401, "invalid username or password"));
                return;
            }

            auto token = newToken();
            sessions.save(token, asInt64(rows[0], "id"));
            reply(jsonResponse(drogon::k200OK, okJson(jsonObject({{"token", token}}))));
        },
        drogon::k401Unauthorized);

    addAuthGet(
        "/api/user/me",
        db,
        sessions,
        [](const drogon::HttpRequestPtr&, HttpCallback& reply, const UserContext& user) {
            reply(jsonResponse(drogon::k200OK,
                               okJson(jsonObject({{"id", std::to_string(user.id)},
                                                  {"username", user.username},
                                                  {"storage_used", std::to_string(user.storageUsed)},
                                                  {"storage_limit", std::to_string(user.storageLimit)}}))));
        },
        drogon::k401Unauthorized);
}

}
