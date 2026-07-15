#include "prod/ShareRoutes.h"

#include "prod/RouteBind.h"
#include "utils/Json.h"

#include <sstream>

namespace cloud_disk {

void registerShareRoutes(const MySqlDatabase& db, const RedisSessionStore& sessions) {
    addAuthPost("/api/shares", db, sessions,
                [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, const UserContext& user) {
                    auto body = parseFlatJsonObject(std::string(req->body()));
                    auto fileId = std::stoll(body["file_id"]);
                    auto file = findActiveFile(db, user.id, fileId);
                    if (!file || file->isDir) {
                        reply(errorResponse(drogon::k404NotFound, 404, "file not found"));
                        return;
                    }

                    auto token = newShareToken();
                    bool allowDownload = body["allow_download"] != "false";
                    db.execute(
                        "INSERT INTO shares(share_token, access_code, user_id, file_id, allow_download) "
                        "VALUES("
                        + db.quote(token) + ", " + db.quote(body["access_code"]) + ", "
                        + sqlNumber(user.id) + ", " + sqlNumber(fileId) + ", "
                        + (allowDownload ? "TRUE" : "FALSE") + ")");

                    auto root = baseUrl(req);
                    reply(jsonResponse(
                        drogon::k201Created,
                        okJson(jsonObject({{"token", token},
                                           {"url", root + "/share?token=" + token},
                                           {"api_url", root + "/api/public/share?token=" + token},
                                           {"download_url", root + "/api/public/download?token=" + token},
                                           {"allow_download", allowDownload ? "true" : "false"}}))));
                });

    auto handlePublicShare = [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply, bool asHtml) {
        auto rows = db.query(
            "SELECT s.share_token, s.access_code, s.allow_download, f.id file_id, f.name, f.size_bytes "
            "FROM shares s JOIN files f ON s.file_id = f.id "
            "WHERE s.share_token = "
            + db.quote(req->getParameter("token")) + " AND s.is_active = TRUE AND f.is_deleted = FALSE");

        if (rows.empty()
            || (!rows[0]["access_code"].empty() && rows[0]["access_code"] != req->getParameter("code"))) {
            reply(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
            return;
        }

        db.execute("UPDATE shares SET view_count = view_count + 1 WHERE share_token = "
                   + db.quote(req->getParameter("token")));

        const auto& row = rows[0];
        if (!asHtml) {
            reply(jsonResponse(drogon::k200OK,
                               okJson(jsonObject({{"token", row.at("share_token")},
                                                  {"file_id", row.at("file_id")},
                                                  {"name", row.at("name")},
                                                  {"size_bytes", row.at("size_bytes")},
                                                  {"allow_download",
                                                   asBool(row, "allow_download") ? "true" : "false"}}))));
            return;
        }

        std::ostringstream page;
        page << "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
             << "<title>Cloud Disk 分享</title><body style=\"font-family:sans-serif;max-width:680px;"
             << "margin:64px auto;line-height:1.7\"><h1>Cloud Disk 文件分享</h1>"
             << "<p>文件名：<strong>" << htmlEscape(row.at("name")) << "</strong></p>"
             << "<p>大小：" << row.at("size_bytes") << " bytes</p>";
        if (asBool(row, "allow_download")) {
            page << "<p><a href=\"/api/public/download?token=" << htmlEscape(req->getParameter("token"));
            if (!req->getParameter("code").empty()) {
                page << "&code=" << htmlEscape(req->getParameter("code"));
            }
            page << "\">下载文件</a></p>";
        }
        page << "</body></html>";
        reply(textResponse(drogon::k200OK, page.str(), "text/html; charset=utf-8"));
    };

    addGet(
        "/share",
        [handlePublicShare](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            handlePublicShare(req, reply, true);
        },
        drogon::k404NotFound);

    addGet(
        "/api/public/share",
        [handlePublicShare](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            handlePublicShare(req, reply, false);
        },
        drogon::k404NotFound);

    addGet(
        "/api/public/download",
        [&db](const drogon::HttpRequestPtr& req, HttpCallback& reply) {
            auto rows = db.query(
                "SELECT s.access_code, s.allow_download, f.name, f.object_id "
                "FROM shares s JOIN files f ON s.file_id = f.id "
                "WHERE s.share_token = "
                + db.quote(req->getParameter("token")) + " AND s.is_active = TRUE AND f.is_deleted = FALSE");

            if (rows.empty() || !asBool(rows[0], "allow_download")
                || (!rows[0]["access_code"].empty()
                    && rows[0]["access_code"] != req->getParameter("code"))) {
                reply(errorResponse(drogon::k404NotFound, 404, "share not found or access code invalid"));
                return;
            }

            auto path = objectPath(db, asInt64(rows[0], "object_id"));
            if (!path) {
                reply(errorResponse(drogon::k404NotFound, 404, "object not found"));
                return;
            }

            db.execute("UPDATE shares SET download_count = download_count + 1 WHERE share_token = "
                       + db.quote(req->getParameter("token")));
            reply(drogon::HttpResponse::newFileResponse(*path, rows[0]["name"]));
        },
        drogon::k404NotFound);
}

} // namespace cloud_disk
