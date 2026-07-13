cmake_minimum_required(VERSION 3.16)

project(cloud_disk_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOUD_DISK_WITH_DROGON "Build Drogon/MySQL/Redis production backend" OFF)

add_executable(cloud-disk
    src/main.cpp
    src/services/AuthService.cpp
    src/services/FileService.cpp
    src/services/MetadataStore.cpp
    src/utils/Hash.cpp
    src/utils/HttpServer.cpp
    src/utils/Json.cpp
    src/utils/Path.cpp
)

target_include_directories(cloud-disk PRIVATE src)

if (CLOUD_DISK_WITH_DROGON)
    find_package(Drogon CONFIG REQUIRED)
    find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
    find_library(MYSQL_CLIENT_LIBRARY mysqlclient)
    if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIBRARY)
        message(FATAL_ERROR "MySQL client development library not found")
    endif()

    add_executable(cloud-disk-prod
        src/prod/ProdMain.cpp
        src/utils/Hash.cpp
        src/utils/Json.cpp
        src/utils/Path.cpp
    )

    target_include_directories(cloud-disk-prod PRIVATE src)
    target_include_directories(cloud-disk-prod PRIVATE ${MYSQL_INCLUDE_DIR})
    target_link_libraries(cloud-disk-prod PRIVATE Drogon::Drogon ${MYSQL_CLIENT_LIBRARY})
endif()

if (MSVC)
    target_compile_options(cloud-disk PRIVATE /W4)
    if (TARGET cloud-disk-prod)
        target_compile_options(cloud-disk-prod PRIVATE /W4)
    endif()
else()
    target_compile_options(cloud-disk PRIVATE -Wall -Wextra -Wpedantic)
    if (TARGET cloud-disk-prod)
        target_compile_options(cloud-disk-prod PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

