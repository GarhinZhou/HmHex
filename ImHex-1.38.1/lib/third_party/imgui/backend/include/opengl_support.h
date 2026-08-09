#pragma once

#if defined(OS_WINDOWS)
    #include <Windows.h>
    #include <GL/GL.h>
#endif

#if defined(OS_WEB) || defined(__OHOS__) || defined(__OHOS_FAMILY__)
    #define GLFW_INCLUDE_ES3
    #include <GLES3/gl3.h>
#else
    #include <imgui_impl_opengl3_loader.h>
#endif