#include "prod/StaticRoutes.h"

#include "prod/RouteBind.h"
#include "utils/Json.h"

namespace cloud_disk {

void registerHealthRoute() {
    addGet("/health", [](const drogon::HttpRequestPtr&, HttpCallback& reply) {
        reply(jsonResponse(drogon::k200OK,
                           okJson(jsonObject({{"service", "cloud-disk-prod"},
                                              {"status", "healthy"},
                                              {"stack", "drogon-mysql-redis"}}))));
    });
}

void registerStaticRoutes(const std::filesystem::path& webRoot) {
    addGet("/", [webRoot](const drogon::HttpRequestPtr&, HttpCallback& reply) {
        reply(drogon::HttpResponse::newFileResponse((webRoot / "index.html").string()));
    });

    addGet("/styles.css", [webRoot](const drogon::HttpRequestPtr&, HttpCallback& reply) {
        reply(drogon::HttpResponse::newFileResponse((webRoot / "styles.css").string()));
    });

    addGet("/app.js", [webRoot](const drogon::HttpRequestPtr&, HttpCallback& reply) {
        reply(drogon::HttpResponse::newFileResponse((webRoot / "app.js").string()));
    });
}

}
