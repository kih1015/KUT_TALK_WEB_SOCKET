cmake_minimum_required(VERSION 3.28)
project(KUT_WEB_SOCKET C)

set(CMAKE_C_STANDARD 11)

# 2) MySQL client
find_path(MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql)
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient)
if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIB)
    message(FATAL_ERROR "MySQL client library not found. sudo apt install libmysqlclient-dev")
endif ()

add_executable(KUT_WEB_SOCKET
        ws_handshake.c
        ws_handshake.h
        ws_frame.c
        ws_frame.h
        ws_server.c
        ws_util.c
        ws_util.h
        ws_base64.c
        ws_base64.h
        db.c
        db.h
        session_repository.c
        session_repository.h)

find_package(OpenSSL REQUIRED)
target_include_directories(
        KUT_WEB_SOCKET PRIVATE ${OPENSSL_INCLUDE_DIR}
        ${MYSQL_INCLUDE_DIR}
)
target_link_libraries(
        KUT_WEB_SOCKET PRIVATE
        OpenSSL::Crypto
        ${MYSQL_CLIENT_LIB}      # ← mysqlclient 라이브러리
        pthread                  # ← 스레드 init/end 함수용 (mysql_thread_*)
)
