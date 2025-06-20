cmake_minimum_required(VERSION 3.28)
project(KUT_WEB_SOCKET C)

set(CMAKE_C_STANDARD 11)

# ─── Threads (pthread) ───
find_package(Threads REQUIRED)

# ─── MySQL client ───
find_path(MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql)
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient)
if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIB)
    message(FATAL_ERROR "MySQL client library not found. sudo apt install libmysqlclient-dev")
endif()

# ─── OpenSSL ───
find_package(OpenSSL REQUIRED)

# ─── cJSON library ───
find_path(CJSON_INCLUDE_DIR NAMES cJSON.h PATH_SUFFIXES cjson)
find_library(CJSON_LIB NAMES cjson)
if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIB)
    message(FATAL_ERROR "cJSON not found. Install lib-cjson-dev or equivalent.")
endif()

# ─── Source files ───
file(GLOB WS_SOURCES
        ws_handshake.c
        ws_frame.c
        ws_util.c
        ws_base64.c
        ws_server.c
        db.c
        session_repository.c
        chat_repository.c
)

add_executable(KUT_WEB_SOCKET ${WS_SOURCES})

# ─── Include directories ───
target_include_directories(KUT_WEB_SOCKET PRIVATE
        ${OPENSSL_INCLUDE_DIR}
        ${MYSQL_INCLUDE_DIR}
        ${CJSON_INCLUDE_DIR}
)

# ─── Link libraries ───
target_link_libraries(KUT_WEB_SOCKET PRIVATE
        OpenSSL::Crypto
        ${MYSQL_CLIENT_LIB}
        ${CJSON_LIB}
        Threads::Threads
)

# ─── Compile options (warnings) ───
target_compile_options(KUT_WEB_SOCKET PRIVATE
        -Wall -Wextra -Wpedantic
)

# ─── Installation (optional) ───
install(TARGETS KUT_WEB_SOCKET DESTINATION bin)
