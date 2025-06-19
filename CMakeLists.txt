cmake_minimum_required(VERSION 3.28)
project(KUT_WEB_SOCKET C)

set(CMAKE_C_STANDARD 11)

add_executable(KUT_WEB_SOCKET
        ws_handshake.c
        ws_handshake.h
        ws_frame.c
        ws_frame.h
        ws_server.c
        ws_util.c
        ws_util.h
        ws_base64.c
        ws_base64.h)

find_package(OpenSSL REQUIRED)
target_include_directories(KUT_WEB_SOCKET PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(KUT_WEB_SOCKET PRIVATE OpenSSL::Crypto)