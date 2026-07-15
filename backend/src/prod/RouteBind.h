#pragma once

#include "prod/RouteUtils.h"

#include <drogon/drogon.h>

#include <functional>

namespace cloud_disk {

// Drogon 异步回调别名，路由里反复写太长。
using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

using OpenHandler = std::function<void(const drogon::HttpRequestPtr&, HttpCallback&)>;
using AuthHandler =
    std::function<void(const drogon::HttpRequestPtr&, HttpCallback&, const UserContext&)>;

// 注册公开接口。errorStatus 是未捕获异常时的默认 HTTP 状态码。
inline void addRoute(const char* path,
                     drogon::HttpMethod method,
                     OpenHandler handler,
                     drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    drogon::app().registerHandler(
        path,
        [handler, errorStatus](const drogon::HttpRequestPtr& req, HttpCallback&& callback) {
            try {
                handler(req, callback);
            } catch (const std::exception& ex) {
                callback(errorResponse(errorStatus, static_cast<int>(errorStatus), ex.what()));
            }
        },
        {method});
}

inline void addGet(const char* path,
                   OpenHandler handler,
                   drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addRoute(path, drogon::Get, std::move(handler), errorStatus);
}

inline void addPost(const char* path,
                    OpenHandler handler,
                    drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addRoute(path, drogon::Post, std::move(handler), errorStatus);
}

inline void addDelete(const char* path,
                      OpenHandler handler,
                      drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addRoute(path, drogon::Delete, std::move(handler), errorStatus);
}

// 注册需要登录的接口，内部先走 requireUser。
inline void addAuthRoute(const char* path,
                         drogon::HttpMethod method,
                         const MySqlDatabase& db,
                         const RedisSessionStore& sessions,
                         AuthHandler handler,
                         drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    drogon::app().registerHandler(
        path,
        [&db, &sessions, handler, errorStatus](const drogon::HttpRequestPtr& req,
                                               HttpCallback&& callback) {
            try {
                auto user = requireUser(req, db, sessions);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, 401, "missing or invalid token"));
                    return;
                }
                handler(req, callback, *user);
            } catch (const std::exception& ex) {
                callback(errorResponse(errorStatus, static_cast<int>(errorStatus), ex.what()));
            }
        },
        {method});
}

inline void addAuthGet(const char* path,
                       const MySqlDatabase& db,
                       const RedisSessionStore& sessions,
                       AuthHandler handler,
                       drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addAuthRoute(path, drogon::Get, db, sessions, std::move(handler), errorStatus);
}

inline void addAuthPost(const char* path,
                        const MySqlDatabase& db,
                        const RedisSessionStore& sessions,
                        AuthHandler handler,
                        drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addAuthRoute(path, drogon::Post, db, sessions, std::move(handler), errorStatus);
}

inline void addAuthDelete(const char* path,
                          const MySqlDatabase& db,
                          const RedisSessionStore& sessions,
                          AuthHandler handler,
                          drogon::HttpStatusCode errorStatus = drogon::k400BadRequest) {
    addAuthRoute(path, drogon::Delete, db, sessions, std::move(handler), errorStatus);
}

} // namespace cloud_disk
