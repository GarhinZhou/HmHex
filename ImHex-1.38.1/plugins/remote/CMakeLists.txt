cmake_minimum_required(VERSION 3.16)

include(ImHexPlugin)

if (NOT WIN32 AND NOT IMHEX_OHOS_PORT)
    find_package(OpenSSL REQUIRED)
endif()
find_package(libssh2 REQUIRED)

add_imhex_plugin(
    NAME
        remote
    SOURCES
        source/plugin_remote.cpp
        source/content/helpers/sftp_client.cpp
        source/content/providers/ssh_provider.cpp

    INCLUDES
        include
        ${LIBSSH2_INCLUDE_DIR}

    LIBRARIES
        ui
        fonts
        ${LIBSSH2_LIBRARY}
)

if (NOT WIN32)
    if (IMHEX_OHOS_PORT)
        # libssh2 is built from source with the in-tree mbedTLS crypto backend;
        # link the mbedTLS libraries explicitly to satisfy its symbols.
        target_link_libraries(remote PRIVATE mbedtls mbedx509 mbedcrypto)
    else()
        target_link_libraries(remote PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()
