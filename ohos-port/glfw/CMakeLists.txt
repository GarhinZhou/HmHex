# GLFW compatibility layer for OpenHarmony (ImHex OHOS port)

add_library(imhex_glfw_ohos STATIC glfw_ohos.cpp)

set_target_properties(imhex_glfw_ohos PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

target_include_directories(imhex_glfw_ohos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(imhex_glfw_ohos PUBLIC
    EGL
    GLESv3
    native_window
    ${IMHEX_OHOS_SYSROOT}/usr/lib/aarch64-linux-ohos/libace_ndk.z.so
    pthread
)

target_compile_definitions(imhex_glfw_ohos PRIVATE GLFW_INCLUDE_ES3)
