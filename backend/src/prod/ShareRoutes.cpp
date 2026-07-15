#include "prod/ShareRoutes.h"

#include "prod/RouteBind.h"
#include "utils/Json.h"

#include <optional>
#include <sstream>

namespace cloud_disk {
namespace {

// 按分享 token 查出有效记录；访问码不对或未找到时返回空。
std::optional<DbRow> loadActiveShare(const MySqlDatabase& db,
                                     const std::string& token,
                                     const std::string& accessCode) {
    auto rows = db.query(
        "SELECT s.share_token, s.access_code, s.allow_download, f.id file_id, f.name, f.size_bytes, "
        "f.object_id "
        "FROM shares s JOIN files f ON s.file_id = f.id "
        "WHERE s.share_token = "
        + db.quote(token) + " AND s.is_active = TRUE AND f.is_deleted = FALSE");
    if (rows.empty()) {
        return std::nullopt;
    }
    if (!rows[0]["access_code"].empty() && rows[0]["access_code"] != accessCode) {
        return std::nullopt;
    }
    return rows[0];
}

// 用户操作：登录后给某个文件创建分享链接。
void createShare(const MySqlDatabase& db,
                 const drogon::HttpRequestPtr& req,
                 HttpCallback& reply,
                 const UserContext& user) {
    auto body = parseFlatJsonObject(std::string(req->body()));
    auto fileId = std::stoll(body["file_id"]);
    auto file = findActiveFile(db, user.id, fileId);
    if (!file || file->isDir) {
        reply(errorResponse(drogon::k404NotFound, 404, "file not found"));
        return;
    }

    auto token = newShareToken();
    bool allowDownload = body["allow_download"] != "false";
    db.execute("INSERT INTO shares(share_token, access_code, user_id, file_id, allow_download) VALUES("
               + db.quote(token) + ", " + db.quote(body["access_code"]) + ", " + sqlNumber(user.id) + ", "
               + sqlNumber(fileId) + ", " + (allowDownload ? "TRUE" : "FALSE") + ")");

    auto root = baseUrl(req);
    reply(jsonResponse(drogon::k201Created,
                       okJson(jsonObject({{"token", token},
                                          {"url", root + "/share?token=" + token},
                                          {"api_url", root + "/api/public/share?token=" + token},
                                          {"download_url", root + "/api/public/download?token=" + token},
                                          {"allow_download", allowDownload ? "true" : "false"}}))));
}

// 用户操作：打开分享页或查询分享信息（HTML / JSON）。
void viewPublicShare(const MySqlDatabase& db,
                     const drogon::HttpRequestPtr& req,
                     HttpCallback& reply,
                     bool asHtml) {
    auto share = loadActiveShare(db, req->getParameter("token"), req->getParameter("code"));
    if (!share) {
        reply(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
        return;
    }

    db.execute("UPDATE shares SET view_count = view_count + 1 WHERE share_token = "
               + db.quote(req->getParameter("token")));

    if (!asHtml) {
        reply(jsonResponse(drogon::k200OK,
                           okJson(jsonObject({{"token", share->at("share_token")},
                                              {"file_id", share->at("file_id")},
                                              {"name", share->at("name")},
                                              {"size_bytes", share->at("size_bytes")},
                                              {"allow_download",
                                               asBool(*share, "allow_download") ? "true" : "false"}}))));
        return;
    }

    std::ostringstream page;
    page << "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
         << "<title>Cloud Disk 分享</title><body style=\"font-family:sans-serif;max-width:680px;"
         << "margin:64px auto;line-height:1.7\"><h1>Cloud Disk 文件分享</h1>"
         << "<p>文件名：<strong>" << htmlEscape(share->at("name")) << "</strong></p>"
         << "<p>大小：" << share->at("size_bytes") << " bytes</p>";
    if (asBool(*share, "allow_download")) {
        page << "<p><a href=\"/api/public/download?token=" << htmlEscape(req->getParameter("token"));
        if (!req->getParameter("code").empty()) {
            page << "&code=" << htmlEscape(req->getParameter("code"));
        }
        page << "\">下载文件</a></p>";
    }
    page << "</body></html>";
    reply(textResponse(drogon::k200OK, page.str(), "text/html; charset=utf-8"));
}

// 用户操作：通过分享链接下载文件。
void downloadPublicShare(const MySqlDatabase& db,
                         const drogon::HttpRequestPtr& req,
                         HttpCallback& reply) {
    auto share = loadActiveShare(db, req->getParameter("token"), req->getParameter("code"));
    if (!share || !asBool(*share, "allow_download")) {
        reply(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
        return;
    }

    auto path = objectPath(db, asInt64(*share, "object_id"));
    if (!path) {
        reply(errorResponse(drogon::k404NotFound, 404, "object not found"));
        return;
    }

    db.execute("UPDATE shares SET download_count = download_count + 1 WHERE share_token = "
               + db.quote(req->getParameter("token")));
    reply(fileDownloadResponse(*path, share->at("name"), req->getHeader("range")));
}

}

void registerShareRoutes(const MySqlDatabase& db, const RedisSessionStore& sessions) {
    // 创建分享链接
    addAuthPost("/api/shares", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    createShare(db, req, reply, user);
                });

    // 浏览器打开分享页
    addGet(
        "/share",
        [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            viewPublicShare(db, req, reply, true);
        },
        drogon::k404NotFound);

    // 查询分享信息（JSON）
    addGet(
        "/api/public/share",
        [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            viewPublicShare(db, req, reply, false);
        },
        drogon::k404NotFound);

    // 公开下载
    addGet(
        "/api/public/download",
        [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            downloadPublicShare(db, req, reply);
        },
        drogon::k404NotFound);
}

}
