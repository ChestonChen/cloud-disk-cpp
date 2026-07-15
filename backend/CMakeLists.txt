cmake_minimum_required(VERSION 3.16)

project(cloud_disk_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
find_library(MYSQL_CLIENT_LIBRARY mysqlclient)

if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIBRARY)
    message(FATAL_ERROR "MySQL client development library not found")
endif()

add_executable(cloud-disk
    src/prod/AppConfig.cpp
    src/prod/AuthRoutes.cpp
    src/prod/FileOps.cpp
    src/prod/FileRoutes.cpp
    src/prod/MySqlDatabase.cpp
    src/prod/ProdMain.cpp
    src/prod/RedisSessionStore.cpp
    src/prod/RouteUtils.cpp
    src/prod/ShareRoutes.cpp
    src/prod/StaticRoutes.cpp
    src/utils/Hash.cpp
    src/utils/Json.cpp
    src/utils/Path.cpp
)

target_include_directories(cloud-disk PRIVATE src ${MYSQL_INCLUDE_DIR})
target_link_libraries(cloud-disk PRIVATE Drogon::Drogon ${MYSQL_CLIENT_LIBRARY})

if (MSVC)
    target_compile_options(cloud-disk PRIVATE /W4)
else()
    target_compile_options(cloud-disk PRIVATE -Wall -Wextra -Wpedantic)
endif()
