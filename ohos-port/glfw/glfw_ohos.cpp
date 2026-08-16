// GLFW-compatible layer for OpenHarmony (ImHex OHOS port)
// =======================================================
// Implements the GLFW API surface used by ImHex (main/gui + imgui_impl_glfw)
// on top of OpenHarmony's native graphics stack:
//   - EGL + GLES3 for rendering
//   - OH_NativeWindow (from an ArkUI XComponent surface) as the window target
//   - XComponent input callbacks (mouse / touch / key) feeding the GLFW callbacks
//
// The ImHex main window is single-windowed; this implementation supports one
// window and one EGL context, which is all ImHex needs.

#include <GLFW/glfw3.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <hilog/log.h>

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <native_window/external_window.h>
#include <multimodalinput/oh_key_code.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

// GLFW 3.4 constant not present in the 3.3.9 header we ship
#ifndef GLFW_MOUSE_TRANSPARENT
    #define GLFW_MOUSE_TRANSPARENT 0x0002000C
#endif

// Implemented in the OHOS entry layer (xcomponent_entry.cpp): forwards the
// always-on-top state to the ArkTS side, which applies it via the system
// window API.
extern "C" void ohosNotifySetWindowTopmost(bool);

// Implemented in the OHOS entry layer (xcomponent_entry.cpp): forwards the
// GLFW cursor shape to the ArkTS side, which applies the matching system
// pointer style.
extern "C" void ohosNotifySetCursor(int shape);

// GLFW 3.4 platform enum (glfwGetPlatform)
#ifndef GLFW_PLATFORM_WAYLAND
    typedef enum {
        GLFW_PLATFORM_WAYLAND = 0x00060001,
        GLFW_PLATFORM_X11     = 0x00060002,
        GLFW_PLATFORM_ANY     = 0x00060000
    } GLFWplatformenum;
#endif

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

namespace {

    double nowSeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // OHOS keycode -> GLFW keycode
    int ohosKeyToGlfw(int32_t keyCode) {
        // Printable keys: OHOS uses Android-compatible keycodes starting at 2017 for 'A'
        if (keyCode >= KEYCODE_A && keyCode <= KEYCODE_Z)           // 2017..2042
            return GLFW_KEY_A + (keyCode - KEYCODE_A);
        if (keyCode >= KEYCODE_0 && keyCode <= KEYCODE_9)           // 2062..2071
            return GLFW_KEY_0 + (keyCode - KEYCODE_0);

        switch (keyCode) {
            case KEYCODE_ENTER:            return GLFW_KEY_ENTER;
            case KEYCODE_DPAD_LEFT:        return GLFW_KEY_LEFT;
            case KEYCODE_DPAD_RIGHT:       return GLFW_KEY_RIGHT;
            case KEYCODE_DPAD_UP:          return GLFW_KEY_UP;
            case KEYCODE_DPAD_DOWN:        return GLFW_KEY_DOWN;
            case KEYCODE_DEL:              return GLFW_KEY_BACKSPACE;
            case KEYCODE_FORWARD_DEL:      return GLFW_KEY_DELETE;
            case KEYCODE_TAB:              return GLFW_KEY_TAB;
            case KEYCODE_SPACE:            return GLFW_KEY_SPACE;
            case KEYCODE_ESCAPE:           return GLFW_KEY_ESCAPE;
            case KEYCODE_CAPS_LOCK:        return GLFW_KEY_CAPS_LOCK;
            case KEYCODE_SHIFT_LEFT:       return GLFW_KEY_LEFT_SHIFT;
            case KEYCODE_SHIFT_RIGHT:      return GLFW_KEY_RIGHT_SHIFT;
            case KEYCODE_CTRL_LEFT:        return GLFW_KEY_LEFT_CONTROL;
            case KEYCODE_CTRL_RIGHT:       return GLFW_KEY_RIGHT_CONTROL;
            case KEYCODE_ALT_LEFT:         return GLFW_KEY_LEFT_ALT;
            case KEYCODE_ALT_RIGHT:        return GLFW_KEY_RIGHT_ALT;
            case KEYCODE_MENU:             return GLFW_KEY_LEFT_SUPER;
            case KEYCODE_COMMA:            return GLFW_KEY_COMMA;
            case KEYCODE_PERIOD:           return GLFW_KEY_PERIOD;
            case KEYCODE_SLASH:            return GLFW_KEY_SLASH;
            case KEYCODE_SEMICOLON:        return GLFW_KEY_SEMICOLON;
            case KEYCODE_APOSTROPHE:       return GLFW_KEY_APOSTROPHE;
            case KEYCODE_LEFT_BRACKET:     return GLFW_KEY_LEFT_BRACKET;
            case KEYCODE_RIGHT_BRACKET:    return GLFW_KEY_RIGHT_BRACKET;
            case KEYCODE_BACKSLASH:        return GLFW_KEY_BACKSLASH;
            case KEYCODE_MINUS:            return GLFW_KEY_MINUS;
            case KEYCODE_EQUALS:           return GLFW_KEY_EQUAL;
            case KEYCODE_GRAVE:            return GLFW_KEY_GRAVE_ACCENT;
            case KEYCODE_HOME:             return GLFW_KEY_HOME;
            case KEYCODE_MOVE_END:              return GLFW_KEY_END;
            case KEYCODE_PAGE_UP:          return GLFW_KEY_PAGE_UP;
            case KEYCODE_PAGE_DOWN:        return GLFW_KEY_PAGE_DOWN;
            case KEYCODE_INSERT:           return GLFW_KEY_INSERT;
            case KEYCODE_NUM_LOCK:         return GLFW_KEY_NUM_LOCK;
            case KEYCODE_SCROLL_LOCK:      return GLFW_KEY_SCROLL_LOCK;
            case KEYCODE_BREAK:            return GLFW_KEY_PAUSE;
            default:
                if (keyCode >= KEYCODE_F1 && keyCode <= KEYCODE_F12)
                    return GLFW_KEY_F1 + (keyCode - KEYCODE_F1);
                if (keyCode >= KEYCODE_NUMPAD_0 && keyCode <= KEYCODE_NUMPAD_9)
                    return GLFW_KEY_KP_0 + (keyCode - KEYCODE_NUMPAD_0);
                return GLFW_KEY_UNKNOWN;
        }
    }

}

// ---------------------------------------------------------------------------
// GLFW types
// ---------------------------------------------------------------------------

struct GLFWmonitor {
    int x = 0, y = 0;
    int widthMM = 0, heightMM = 0;
    GLFWvidmode mode { 1920, 1080, 60, 32, 32, 32 };
    float contentScaleX = 1.0f, contentScaleY = 1.0f;
    const char *name = "Display";
    void *userPointer = nullptr;
};

struct GLFWwindow {
    // Window state
    bool shouldClose = false;
    bool visible = true;
    bool iconified = false;
    bool focused = true;
    bool decorated = true;
    bool resizable = true;
    bool mouseTransparent = false;
    bool maximized = false;
    float opacity = 1.0f;
    std::string title = "HmHex";
    int posX = 0, posY = 0;
    int width = 1280, height = 800;
    int fbWidth = 1280, fbHeight = 800;
    void *userPointer = nullptr;

    // Monitor the window is fullscreen on (nullptr = windowed). The OHOS port
    // has a single logical monitor and no real fullscreen toggle; this flag
    // drives the UI state (fullscreen menu checkbox, custom titlebar buttons).
    GLFWmonitor *monitor = nullptr;

    // Native/EGL state
    OHNativeWindow *nativeWindow = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    bool contextCurrent = false;

    // Input state
    double cursorX = 0.0, cursorY = 0.0;
    std::array<char, GLFW_KEY_LAST + 1> keys {};
    std::array<char, GLFW_MOUSE_BUTTON_LAST + 1> mouseButtons {};
    int inputModeCursor = GLFW_CURSOR_NORMAL;
    bool floating = false;
    // Clipboard as a plain C buffer: the std::string member crashed in
    // glfwSetClipboardString on the OHOS libc++ (ABI mismatch with the
    // bundled libc++_shared), so avoid std::string here entirely.
    char clipboard[4096] = { 0 };

    // Callbacks (as registered by ImHex / imgui_impl_glfw)
    GLFWerrorfun        errorCallback        = nullptr;
    GLFWwindowposfun    windowPosCallback    = nullptr;
    GLFWwindowsizefun   windowSizeCallback   = nullptr;
    GLFWwindowclosefun  windowCloseCallback  = nullptr;
    GLFWwindowrefreshfun windowRefreshCallback = nullptr;
    GLFWwindowfocusfun  windowFocusCallback  = nullptr;
    GLFWwindowiconifyfun windowIconifyCallback = nullptr;
    GLFWwindowmaximizefun windowMaximizeCallback = nullptr;
    GLFWkeyfun          keyCallback          = nullptr;
    GLFWcharfun         charCallback         = nullptr;
    GLFWcharmodsfun     charModsCallback     = nullptr;
    GLFWmousebuttonfun  mouseButtonCallback  = nullptr;
    GLFWcursorposfun    cursorPosCallback    = nullptr;
    GLFWcursorenterfun  cursorEnterCallback  = nullptr;
    GLFWscrollfun       scrollCallback       = nullptr;
    GLFWdropfun         dropCallback         = nullptr;
    GLFWmonitorfun      monitorCallback      = nullptr;
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

namespace {

    struct GlobalState {
        bool initialized = false;
        bool eglInitialized = false;
        int screenDpi = 160;
        float titleButtonWidth = 0.0F;
        GLFWerrorfun errorCallback = nullptr;
        GLFWmonitor monitor;
        std::vector<GLFWmonitor*> monitors;
        GLFWwindow *window = nullptr;
        GLFWwindow *currentContext = nullptr;
        void *pendingNativeWindow = nullptr;
        void *lastNativeWindow = nullptr;
        std::mutex mutex;
        std::condition_variable eventCond;
        bool eventPending = false;

        // Input queue (filled by XComponent callbacks on the UI thread,
        // drained by glfwPollEvents / glfwWaitEvents on the render thread)
        enum class EventType { Key, Char, MouseButton, CursorPos, CursorEnter, Scroll, Drop, Focus, Close, SurfaceChanged, SurfaceDestroyed, Reattach };
        struct InputEvent {
            EventType type;
            int a = 0, b = 0, c = 0, d = 0;
            double x = 0.0, y = 0.0;
            std::string text;
            void *ptr = nullptr;
        };
        std::vector<InputEvent> inputQueue;

        void queueEvent(InputEvent event) {
            std::lock_guard lock(mutex);
            inputQueue.push_back(std::move(event));
            eventPending = true;
            eventCond.notify_all();
        }
    };

    GlobalState &g() {
        static GlobalState state;
        return state;
    }

    void setError(int code, const char *format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                     "GLFW/EGL error %d: %{public}s", code, buffer);

        if (g().errorCallback != nullptr)
            g().errorCallback(code, buffer);
    }

}

// ---------------------------------------------------------------------------
// Public API: native window attachment & input dispatch (called by entry layer)
// ---------------------------------------------------------------------------

extern "C" {

    // Attaches a native window (from an ArkUI XComponent surface) to the GLFW window
    // and initializes EGL. Called by the NAPI entry once the surface is available.
    void glfwOhosAttachNativeWindow(GLFWwindow *window, void *nativeWindow) {
        if (window == nullptr || nativeWindow == nullptr) {
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "attach: null window/nativeWindow");
            return;
        }

        // Replacing a previously attached native window: release the old
        // reference (each CreateNativeWindow/CreateNativeWindowFromSurfaceId
        // must be paired with exactly one DestroyNativeWindow). Detach it from
        // lastNativeWindow first so a later release can't double-free it.
        // Also tear down the old EGL surface/context — they still reference
        // the replaced native window (ordering safety: a Reattach can arrive
        // before the matching SurfaceDestroyed).
        // Safe without a lock: this runs on the render thread, while
        // setPendingNativeWindow (UI thread) never writes lastNativeWindow
        // once a window is attached.
        if (window->nativeWindow != nullptr && window->nativeWindow != nativeWindow) {
            if (g().lastNativeWindow == window->nativeWindow)
                g().lastNativeWindow = nullptr;
            OH_NativeWindow_DestroyNativeWindow(window->nativeWindow);
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "attach: released replaced nativeWindow");

            if (window->display != EGL_NO_DISPLAY) {
                if (window->surface != EGL_NO_SURFACE) {
                    eglDestroySurface(window->display, window->surface);
                    window->surface = EGL_NO_SURFACE;
                }
                if (window->context != EGL_NO_CONTEXT) {
                    eglMakeCurrent(window->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(window->display, window->context);
                    window->context = EGL_NO_CONTEXT;
                    window->contextCurrent = false;
                }
            }
        }

        window->nativeWindow = static_cast<OHNativeWindow *>(nativeWindow);
        // Track the reference for the surface-destroyed release path. This
        // runs on the render thread; setPendingNativeWindow (UI thread, under
        // the global mutex) only writes lastNativeWindow while no window is
        // attached, so the two never race.
        g().lastNativeWindow = window->nativeWindow;
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                     "attach: nativeWindow=%p", (void*)nativeWindow);

        if (!g().eglInitialized) {
            window->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (window->display == EGL_NO_DISPLAY) {
                setError(GLFW_PLATFORM_ERROR, "EGL: eglGetDisplay failed");
                return;
            }
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", "attach: display=%p", (void*)window->display);

            EGLint major = 0, minor = 0;
            if (!eglInitialize(window->display, &major, &minor)) {
                setError(GLFW_PLATFORM_ERROR, "EGL: eglInitialize failed");
                return;
            }
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "attach: eglInitialize %d.%d", major, minor);
            g().eglInitialized = true;
        } else {
            window->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        }

        // Choose config: RGBA8 + ES3 + window surface
        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint numConfigs = 0;
        if (!eglChooseConfig(window->display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
            setError(GLFW_PLATFORM_ERROR, "EGL: no matching config");
            return;
        }
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", "attach: config ok");

        window->surface = eglCreateWindowSurface(window->display, config,
            reinterpret_cast<EGLNativeWindowType>(window->nativeWindow), nullptr);
        if (window->surface == EGL_NO_SURFACE) {
            setError(GLFW_PLATFORM_ERROR, "EGL: eglCreateWindowSurface failed");
            return;
        }
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", "attach: surface=%p", (void*)window->surface);

        const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        window->context = eglCreateContext(window->display, config, EGL_NO_CONTEXT, contextAttribs);
        if (window->context == EGL_NO_CONTEXT) {
            setError(GLFW_PLATFORM_ERROR, "EGL: eglCreateContext failed");
            return;
        }
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", "attach: context=%p", (void*)window->context);

        if (!eglMakeCurrent(window->display, window->surface, window->surface, window->context)) {
            setError(GLFW_PLATFORM_ERROR, "EGL: eglMakeCurrent failed");
            return;
        }
        window->contextCurrent = true;
        g().currentContext = window;
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", "attach: current, GL_VERSION=%{public}s",
                     (const char*)glGetString(GL_VERSION));

        // Query the actual surface size
        EGLint w = 0, h = 0;
        eglQuerySurface(window->display, window->surface, EGL_WIDTH, &w);
        eglQuerySurface(window->display, window->surface, EGL_HEIGHT, &h);
        if (w > 0) window->width = w;
        if (h > 0) window->height = h;
        window->fbWidth = window->width;
        window->fbHeight = window->height;

        if (window->windowSizeCallback != nullptr)
            window->windowSizeCallback(window, window->width, window->height);
    }

    void glfwOhosDetachNativeWindow(GLFWwindow *window) {
        if (window == nullptr)
            return;

        if (window->context != EGL_NO_CONTEXT) {
            eglMakeCurrent(window->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(window->display, window->context);
            window->context = EGL_NO_CONTEXT;
        }
        if (window->surface != EGL_NO_SURFACE) {
            eglDestroySurface(window->display, window->surface);
            window->surface = EGL_NO_SURFACE;
        }
        window->nativeWindow = nullptr;
        window->contextCurrent = false;
        g().currentContext = nullptr;
    }

    // Input dispatch helpers (called from the XComponent callbacks)
    void glfwOhosDispatchKey(int key, int scancode, int action, int mods) {
        g().queueEvent({ GlobalState::EventType::Key, key, scancode, action, mods, 0.0, 0.0, {} });
    }
    void glfwOhosDispatchChar(unsigned int codepoint) {
        g().queueEvent({ GlobalState::EventType::Char, static_cast<int>(codepoint), 0, 0, 0, 0.0, 0.0, {} });
    }
    void glfwOhosDispatchMouseButton(int button, int action, int mods) {
        g().queueEvent({ GlobalState::EventType::MouseButton, button, action, mods, 0, 0.0, 0.0, {} });
    }
    void glfwOhosDispatchCursorPos(double x, double y) {
        g().queueEvent({ GlobalState::EventType::CursorPos, 0, 0, 0, 0, x, y, {} });
    }
    void glfwOhosDispatchScroll(double xOffset, double yOffset) {
        g().queueEvent({ GlobalState::EventType::Scroll, 0, 0, 0, 0, xOffset, yOffset, {} });
    }
    void glfwOhosDispatchDrop(int count, const char **paths) {
        std::string joined;
        for (int i = 0; i < count; i++) {
            joined += paths[i];
            joined += '\n';
        }
        g().queueEvent({ GlobalState::EventType::Drop, count, 0, 0, 0, 0.0, 0.0, std::move(joined) });
    }

    // Sets a native window that will be attached when the GLFW window is created.
    // Called by the XComponent surface callback before ImHex starts. Owns the
    // OHNativeWindow reference: every created reference is released exactly once
    // (on replacement, on surface destruction or at process exit).
    void glfwOhosSetPendingNativeWindow(void *nativeWindow) {
        std::lock_guard lock(g().mutex);
        if (nativeWindow == nullptr) {
            g().pendingNativeWindow = nullptr;
            return;
        }

        // Surface recreation while ImHex is already rendering: the current EGL
        // surface still uses the old native window, so drop the new reference
        // instead of orphaning it.
        if (g().window != nullptr && g().window->nativeWindow != nullptr) {
            OH_NativeWindow_DestroyNativeWindow(static_cast<OHNativeWindow *>(nativeWindow));
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "setPending: window already attached, released new nativeWindow");
            return;
        }

        // Overwriting a pending native window that was never attached: release
        // the old reference.
        if (g().pendingNativeWindow != nullptr && g().pendingNativeWindow != nativeWindow) {
            OH_NativeWindow_DestroyNativeWindow(static_cast<OHNativeWindow *>(g().pendingNativeWindow));
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "setPending: released replaced pending nativeWindow");
        }

        g().pendingNativeWindow = nativeWindow;
        // Remember the native window so later windows (e.g. the main window
        // after the splash window) can re-attach to the same XComponent surface.
        g().lastNativeWindow = nativeWindow;
    }

    // Releases the remembered native window (called on the render thread once
    // the XComponent surface was destroyed; detach must have run before).
    void glfwOhosReleaseLastNativeWindow() {
        std::lock_guard lock(g().mutex);
        if (g().lastNativeWindow != nullptr) {
            OH_NativeWindow_DestroyNativeWindow(static_cast<OHNativeWindow *>(g().lastNativeWindow));
            g().lastNativeWindow = nullptr;
        }
    }

    // Requests the render thread to re-attach to a new native window. Used
    // when the XComponent surface is recreated while ImHex is already
    // rendering (page rebuild, 2in1 surface recreation). The event is queued
    // behind any pending SurfaceDestroyed, so the old surface is detached
    // first, then the new one is attached.
    void glfwOhosNotifyReattach(void *nativeWindow) {
        g().queueEvent({ GlobalState::EventType::Reattach, 0, 0, 0, 0, 0.0, 0.0, {}, nativeWindow });
    }

    void *glfwOhosGetWindow() {
        return g().window;
    }

    void glfwOhosSetWindowSize(int width, int height) {
        auto *window = g().window;
        if (window == nullptr)
            return;

        if (width > 0) window->width = width;
        if (height > 0) window->height = height;
        if (window->contextCurrent && window->display != EGL_NO_DISPLAY && window->surface != EGL_NO_SURFACE) {
            EGLint w = 0, h = 0;
            eglQuerySurface(window->display, window->surface, EGL_WIDTH, &w);
            eglQuerySurface(window->display, window->surface, EGL_HEIGHT, &h);
            if (w > 0) window->fbWidth = w;
            if (h > 0) window->fbHeight = h;
        } else {
            window->fbWidth = window->width;
            window->fbHeight = window->height;
        }

        if (window->windowSizeCallback != nullptr)
            window->windowSizeCallback(window, window->width, window->height);
    }

    // Surface size change / destruction arrive on the UI thread (XComponent
    // callbacks). The actual EGL/ImGui state must only be touched on the
    // render thread, so both are forwarded through the input queue and
    // applied inside glfwPollEvents.
    void glfwOhosNotifySurfaceChanged(int width, int height) {
        g().queueEvent({ GlobalState::EventType::SurfaceChanged, width, height, 0, 0, 0.0, 0.0, {} });
    }

    void glfwOhosNotifySurfaceDestroyed() {
        g().queueEvent({ GlobalState::EventType::SurfaceDestroyed, 0, 0, 0, 0, 0.0, 0.0, {} });
    }

    // OHOS keycode -> GLFW keycode (exported for the entry layer)
    int ohosKeyToGlfwKey(int32_t keyCode) {
        return ohosKeyToGlfw(keyCode);
    }

}

// ---------------------------------------------------------------------------
// GLFW API implementation
// ---------------------------------------------------------------------------

extern "C" {

    // --- Version / errors ---

    GLFWAPI GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback) {
        auto old = g().errorCallback;
        g().errorCallback = callback;
        if (g().window != nullptr)
            g().window->errorCallback = callback;
        return old;
    }

    GLFWAPI const char *glfwGetVersionString(void) {
        return "3.3.9 OHOS (ImHex port)";
    }

    GLFWAPI int glfwGetError(const char **description) {
        if (description != nullptr)
            *description = "OHOS port: no GLFW error state";
        return GLFW_NO_ERROR;
    }

    // --- Init / terminate ---

    GLFWAPI int glfwInit(void) {
        std::lock_guard lock(g().mutex);
        if (g().initialized)
            return GLFW_TRUE;

        g().monitor = GLFWmonitor();
        g().monitors.push_back(&g().monitor);
        g().initialized = true;
        return GLFW_TRUE;
    }

    GLFWAPI void glfwTerminate(void) {
        std::lock_guard lock(g().mutex);
        if (g().window != nullptr) {
            glfwOhosDetachNativeWindow(g().window);
            delete g().window;
            g().window = nullptr;
        }
        g().initialized = false;
    }

    // --- Time ---

    GLFWAPI double glfwGetTime(void) {
        return nowSeconds();
    }

    GLFWAPI void glfwSetTime(double) {
    }

    // --- Window creation ---

    GLFWAPI GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *, GLFWwindow *) {
        std::lock_guard lock(g().mutex);
        if (g().window != nullptr) {
            setError(GLFW_PLATFORM_ERROR, "OHOS port: only a single window is supported");
            return nullptr;
        }

        auto *window = new GLFWwindow();
        window->width = width > 0 ? width : 1280;
        window->height = height > 0 ? height : 800;
        window->fbWidth = window->width;
        window->fbHeight = window->height;
        window->title = title != nullptr ? title : "";
        g().window = window;

        // If a native window (from the XComponent surface) is already waiting,
        // attach it immediately so the EGL context is ready before the first frame.
        // Otherwise reuse the last native window (splash -> main window) so the
        // main window gets a working EGL context too.
        if (g().pendingNativeWindow != nullptr) {
            glfwOhosAttachNativeWindow(window, g().pendingNativeWindow);
            g().pendingNativeWindow = nullptr;
        } else if (g().lastNativeWindow != nullptr) {
            glfwOhosAttachNativeWindow(window, g().lastNativeWindow);
        }

        return window;
    }

    GLFWAPI void glfwDestroyWindow(GLFWwindow *window) {
        if (window == nullptr)
            return;

        glfwOhosDetachNativeWindow(window);
        if (g().window == window)
            g().window = nullptr;
        delete window;
    }

    GLFWAPI void glfwDefaultWindowHints(void) {
    }

    GLFWAPI void glfwWindowHint(int, int) {
    }

    GLFWAPI void glfwWindowHintString(int, const char *) {
    }

    GLFWAPI void glfwShowWindow(GLFWwindow *window) {
        window->visible = true;
    }

    GLFWAPI void glfwHideWindow(GLFWwindow *window) {
        window->visible = false;
    }

    GLFWAPI void glfwFocusWindow(GLFWwindow *) {
    }

    GLFWAPI int glfwWindowShouldClose(GLFWwindow *window) {
        return window->shouldClose ? GLFW_TRUE : GLFW_FALSE;
    }

    GLFWAPI void glfwSetWindowShouldClose(GLFWwindow *window, int value) {
        window->shouldClose = value != GLFW_FALSE;
        g().queueEvent({ GlobalState::EventType::Close });
    }

    GLFWAPI void glfwSetWindowTitle(GLFWwindow *window, const char *title) {
        window->title = title != nullptr ? title : "";
    }

    GLFWAPI void glfwGetWindowPos(GLFWwindow *window, int *xpos, int *ypos) {
        if (xpos != nullptr) *xpos = window->posX;
        if (ypos != nullptr) *ypos = window->posY;
    }

    GLFWAPI void glfwSetWindowPos(GLFWwindow *window, int xpos, int ypos) {
        window->posX = xpos;
        window->posY = ypos;
        if (window->windowPosCallback != nullptr)
            window->windowPosCallback(window, xpos, ypos);
    }

    GLFWAPI void glfwGetWindowSize(GLFWwindow *window, int *width, int *height) {
        // Always refresh from the EGL surface: the XComponent surface size can
        // change (window resize / layout) without a glfwSetWindowSize call.
        if (window->contextCurrent && window->surface != EGL_NO_SURFACE && window->display != EGL_NO_DISPLAY) {
            EGLint w = 0, h = 0;
            eglQuerySurface(window->display, window->surface, EGL_WIDTH, &w);
            eglQuerySurface(window->display, window->surface, EGL_HEIGHT, &h);
            if (w > 0) window->width = w;
            if (h > 0) window->height = h;
            window->fbWidth = window->width;
            window->fbHeight = window->height;
        }
        if (width != nullptr) *width = window->width;
        if (height != nullptr) *height = window->height;
    }

    GLFWAPI void glfwSetWindowSize(GLFWwindow *window, int width, int height) {
        window->width = width;
        window->height = height;
        window->fbWidth = width;
        window->fbHeight = height;
        if (window->windowSizeCallback != nullptr)
            window->windowSizeCallback(window, width, height);
    }

    GLFWAPI void glfwSetWindowSizeLimits(GLFWwindow *, int, int, int, int) {
    }

    GLFWAPI void glfwSetWindowAspectRatio(GLFWwindow *, int, int) {
    }

    GLFWAPI void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height) {
        if (window->contextCurrent && window->surface != EGL_NO_SURFACE && window->display != EGL_NO_DISPLAY) {
            EGLint w = 0, h = 0;
            eglQuerySurface(window->display, window->surface, EGL_WIDTH, &w);
            eglQuerySurface(window->display, window->surface, EGL_HEIGHT, &h);
            if (w > 0) window->fbWidth = w;
            if (h > 0) window->fbHeight = h;
        }
        if (width != nullptr) *width = window->fbWidth;
        if (height != nullptr) *height = window->fbHeight;
    }

    GLFWAPI void glfwGetWindowContentScale(GLFWwindow *window, float *xscale, float *yscale) {
        const float scale = g().screenDpi / 160.0f;
        if (xscale != nullptr) *xscale = scale;
        if (yscale != nullptr) *yscale = scale;
    }

    GLFWAPI void glfwGetMonitorContentScale(GLFWmonitor *, float *xscale, float *yscale) {
        const float scale = g().screenDpi / 160.0f;
        if (xscale != nullptr) *xscale = scale;
        if (yscale != nullptr) *yscale = scale;
    }

    // Sets the screen density (densityDPI) reported by ArkUI. ImHex derives
    // its native UI scale from glfwGetWindowContentScale(); without a real
    // value everything renders at 1:1 physical pixels and looks tiny on
    // 2in1 screens.
    void glfwOhosSetScreenDpi(int dpi) {
        if (dpi > 0)
            g().screenDpi = dpi;
    }

    int glfwOhosGetScreenDpi() {
        return g().screenDpi;
    }

    // Width of the system caption button area (minimize/maximize/close)
    // floating over the top-right corner in immersive mode. Reported by the
    // ArkTS side from the windowTitleButtonRectChange event; ImHex uses it to
    // shift its own title bar buttons out of the way.
    void glfwOhosSetTitleButtonWidth(float width) {
        g().titleButtonWidth = width;
    }

    float glfwOhosGetTitleButtonWidth() {
        return g().titleButtonWidth;
    }

    GLFWAPI void glfwSetWindowOpacity(GLFWwindow *window, float opacity) {
        window->opacity = opacity;
    }

    GLFWAPI float glfwGetWindowOpacity(GLFWwindow *window) {
        return window->opacity;
    }

    GLFWAPI void glfwSetWindowUserPointer(GLFWwindow *window, void *pointer) {
        window->userPointer = pointer;
    }

    GLFWAPI void *glfwGetWindowUserPointer(GLFWwindow *window) {
        return window->userPointer;
    }

    GLFWAPI GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow *window, GLFWwindowposfun callback) {
        auto old = window->windowPosCallback;
        window->windowPosCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow *window, GLFWwindowsizefun callback) {
        auto old = window->windowSizeCallback;
        window->windowSizeCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow *window, GLFWwindowclosefun callback) {
        auto old = window->windowCloseCallback;
        window->windowCloseCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow *window, GLFWwindowrefreshfun callback) {
        auto old = window->windowRefreshCallback;
        window->windowRefreshCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun callback) {
        auto old = window->windowFocusCallback;
        window->windowFocusCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow *window, GLFWwindowiconifyfun callback) {
        auto old = window->windowIconifyCallback;
        window->windowIconifyCallback = callback;
        return old;
    }

    GLFWAPI GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow *window, GLFWwindowmaximizefun callback) {
        auto old = window->windowMaximizeCallback;
        window->windowMaximizeCallback = callback;
        return old;
    }

    GLFWAPI void glfwSetWindowAttrib(GLFWwindow *window, int attrib, int value) {
        switch (attrib) {
            case GLFW_DECORATED: window->decorated = value != GLFW_FALSE; break;
            case GLFW_RESIZABLE: window->resizable = value != GLFW_FALSE; break;
            case GLFW_MOUSE_TRANSPARENT: window->mouseTransparent = value != GLFW_FALSE; break;
            case GLFW_FLOATING: {
                window->floating = value != GLFW_FALSE;
                // Forward to the ArkTS layer (system window API).
                ohosNotifySetWindowTopmost(window->floating);
                break;
            }
            default: break;
        }
    }

    GLFWAPI int glfwGetWindowAttrib(GLFWwindow *window, int attrib) {
        switch (attrib) {
            case GLFW_FOCUSED:    return window->focused ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_ICONIFIED:  return window->iconified ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_MAXIMIZED:  return window->maximized ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_VISIBLE:    return window->visible ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_RESIZABLE:  return window->resizable ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_DECORATED:  return window->decorated ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_CONTEXT_VERSION_MAJOR: return 3;
            case GLFW_CONTEXT_VERSION_MINOR: return 0;
            case GLFW_OPENGL_PROFILE:        return GLFW_OPENGL_ANY_PROFILE;
            case GLFW_CLIENT_API:            return GLFW_OPENGL_ES_API;
            case GLFW_MOUSE_TRANSPARENT:     return window->mouseTransparent ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_FLOATING:              return window->floating ? GLFW_TRUE : GLFW_FALSE;
            default: return 0;
        }
    }

    // --- Window state (iconify / maximize / restore / fullscreen) ---
    // The ArkUI XComponent surface is managed by the OS window, so these only
    // track GLFW-visible state; ImHex's menu items and custom titlebar react
    // to it (fullscreen checkbox, maximize button glyph, restore menu).

    GLFWAPI GLFWmonitor *glfwGetWindowMonitor(GLFWwindow *window) {
        return window->monitor;
    }

    GLFWAPI void glfwSetWindowMonitor(GLFWwindow *window, GLFWmonitor *monitor, int xpos, int ypos, int width, int height, int refreshRate) {
        (void)refreshRate;
        if (monitor == nullptr) {
            // Leave fullscreen: restore the windowed geometry passed by the caller
            window->monitor = nullptr;
            window->posX = xpos;
            window->posY = ypos;
            window->width = width;
            window->height = height;
            window->fbWidth = width;
            window->fbHeight = height;
        } else {
            // Enter fullscreen: adopt the video mode geometry
            window->monitor = monitor;
            if (width > 0 && height > 0) {
                window->width = width;
                window->height = height;
                window->fbWidth = width;
                window->fbHeight = height;
            }
        }
        if (window->windowSizeCallback != nullptr)
            window->windowSizeCallback(window, window->width, window->height);
    }

    GLFWAPI void glfwIconifyWindow(GLFWwindow *window) {
        if (window->iconified) return;
        window->iconified = true;
        window->maximized = false;
        if (window->windowIconifyCallback != nullptr)
            window->windowIconifyCallback(window, GLFW_TRUE);
    }

    GLFWAPI void glfwMaximizeWindow(GLFWwindow *window) {
        if (window->maximized) return;
        window->maximized = true;
        window->iconified = false;
        if (window->windowIconifyCallback != nullptr)
            window->windowIconifyCallback(window, GLFW_FALSE);
        if (window->windowMaximizeCallback != nullptr)
            window->windowMaximizeCallback(window, GLFW_TRUE);
    }

    GLFWAPI void glfwRestoreWindow(GLFWwindow *window) {
        bool wasMaximized = window->maximized;
        bool wasIconified = window->iconified;
        window->maximized = false;
        window->iconified = false;
        if (wasIconified && window->windowIconifyCallback != nullptr)
            window->windowIconifyCallback(window, GLFW_FALSE);
        if (wasMaximized && window->windowMaximizeCallback != nullptr)
            window->windowMaximizeCallback(window, GLFW_FALSE);
    }

    GLFWAPI void glfwRequestWindowAttention(GLFWwindow *window) {
        (void)window;
        // No taskbar attention mechanism on the OHOS port; nothing to do.
    }

    // --- Monitors ---

    GLFWAPI GLFWmonitor **glfwGetMonitors(int *count) {
        if (count != nullptr) *count = 1;
        static GLFWmonitor *monitors[2] = { nullptr, nullptr };
        monitors[0] = &g().monitor;
        return monitors;
    }

    GLFWAPI GLFWmonitor *glfwGetPrimaryMonitor(void) {
        return &g().monitor;
    }

    GLFWAPI void glfwGetMonitorPos(GLFWmonitor *monitor, int *xpos, int *ypos) {
        if (xpos != nullptr) *xpos = monitor->x;
        if (ypos != nullptr) *ypos = monitor->y;
    }

    GLFWAPI void glfwGetMonitorWorkarea(GLFWmonitor *monitor, int *xpos, int *ypos, int *width, int *height) {
        if (xpos != nullptr) *xpos = monitor->x;
        if (ypos != nullptr) *ypos = monitor->y;
        if (width != nullptr) *width = monitor->mode.width;
        if (height != nullptr) *height = monitor->mode.height;
    }

    GLFWAPI const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor) {
        return &monitor->mode;
    }

    GLFWAPI const char *glfwGetMonitorName(GLFWmonitor *monitor) {
        return monitor->name;
    }

    GLFWAPI GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun callback) {
        GLFWmonitorfun old = nullptr;
        if (g().window != nullptr) {
            old = g().window->monitorCallback;
            g().window->monitorCallback = callback;
        }
        return old;
    }

    GLFWAPI void glfwSetWindowIcon(GLFWwindow *, int, const GLFWimage *) {
    }

    // --- Context ---

    GLFWAPI void glfwMakeContextCurrent(GLFWwindow *window) {
        if (window == nullptr) {
            if (g().currentContext != nullptr && g().currentContext->contextCurrent) {
                eglMakeCurrent(g().currentContext->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                g().currentContext->contextCurrent = false;
            }
            g().currentContext = nullptr;
            return;
        }

        if (window->display == EGL_NO_DISPLAY || window->context == EGL_NO_CONTEXT)
            return;

        eglMakeCurrent(window->display, window->surface, window->surface, window->context);
        window->contextCurrent = true;
        g().currentContext = window;
    }

    GLFWAPI GLFWwindow *glfwGetCurrentContext(void) {
        return g().currentContext;
    }

    GLFWAPI void glfwSwapBuffers(GLFWwindow *window) {
        if (window->display != EGL_NO_DISPLAY && window->surface != EGL_NO_SURFACE)
            eglSwapBuffers(window->display, window->surface);
    }

    GLFWAPI void glfwSwapInterval(int interval) {
        if (g().currentContext != nullptr && g().currentContext->display != EGL_NO_DISPLAY)
            eglSwapInterval(g().currentContext->display, interval);
    }

    GLFWAPI int glfwExtensionSupported(const char *extension) {
        return GLFW_FALSE;
    }

    GLFWAPI int glfwCreateWindowSurface(GLFWmonitor *, GLFWwindow *, const void *, void **surface) {
        if (surface != nullptr)
            *surface = nullptr;
        setError(GLFW_API_UNAVAILABLE, "OHOS port: Vulkan window surfaces are not supported");
        return GLFW_API_UNAVAILABLE;
    }

    GLFWAPI GLFWglproc glfwGetProcAddress(const char *procname) {
        return reinterpret_cast<GLFWglproc>(eglGetProcAddress(procname));
    }

    // --- Input ---

    GLFWAPI int glfwGetInputMode(GLFWwindow *window, int mode) {
        if (mode == GLFW_CURSOR)
            return window->inputModeCursor;
        return GLFW_FALSE;
    }

    GLFWAPI void glfwSetInputMode(GLFWwindow *window, int mode, int value) {
        if (mode == GLFW_CURSOR)
            window->inputModeCursor = value;
    }

    GLFWAPI int glfwGetKey(GLFWwindow *window, int key) {
        if (key < 0 || key > GLFW_KEY_LAST)
            return GLFW_RELEASE;
        return window->keys[key];
    }

    GLFWAPI int glfwGetMouseButton(GLFWwindow *window, int button) {
        if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST)
            return GLFW_RELEASE;
        return window->mouseButtons[button];
    }

    GLFWAPI void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos) {
        if (xpos != nullptr) *xpos = window->cursorX;
        if (ypos != nullptr) *ypos = window->cursorY;
    }

    GLFWAPI void glfwSetCursorPos(GLFWwindow *window, double xpos, double ypos) {
        window->cursorX = xpos;
        window->cursorY = ypos;
        if (window->cursorPosCallback != nullptr)
            window->cursorPosCallback(window, xpos, ypos);
    }

    GLFWAPI GLFWcursor *glfwCreateCursor(const GLFWimage *, int, int) {
        return reinterpret_cast<GLFWcursor *>(1);
    }

    GLFWAPI GLFWcursor *glfwCreateStandardCursor(int shape) {
        // Encode the requested shape in the returned handle; glfwSetCursor
        // decodes it and forwards it to the ArkTS layer, which applies the
        // matching system pointer style.
        return reinterpret_cast<GLFWcursor *>(static_cast<intptr_t>(shape));
    }

    GLFWAPI void glfwDestroyCursor(GLFWcursor *) {
    }

    GLFWAPI void glfwSetCursor(GLFWwindow *, GLFWcursor *cursor) {
        static int s_lastShape = 0;

        const auto shape = static_cast<int>(reinterpret_cast<intptr_t>(cursor));
        if (shape == s_lastShape)
            return;
        s_lastShape = shape;

        ohosNotifySetCursor(shape);
    }

    GLFWAPI int glfwRawMouseMotionSupported(void) {
        return GLFW_FALSE;
    }

    GLFWAPI const char *glfwGetKeyName(int key, int) {
        static char name[8];
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            name[0] = static_cast<char>('A' + key - GLFW_KEY_A);
            name[1] = '\0';
            return name;
        }
        return nullptr;
    }

    GLFWAPI int glfwGetKeyScancode(int) {
        return 0;
    }

    // --- Callback registration (input) ---

    GLFWAPI GLFWkeyfun glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun callback) {
        auto old = window->keyCallback;
        window->keyCallback = callback;
        return old;
    }

    GLFWAPI GLFWcharfun glfwSetCharCallback(GLFWwindow *window, GLFWcharfun callback) {
        auto old = window->charCallback;
        window->charCallback = callback;
        return old;
    }

    GLFWAPI GLFWcharmodsfun glfwSetCharModsCallback(GLFWwindow *window, GLFWcharmodsfun callback) {
        auto old = window->charModsCallback;
        window->charModsCallback = callback;
        return old;
    }

    GLFWAPI GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun callback) {
        auto old = window->mouseButtonCallback;
        window->mouseButtonCallback = callback;
        return old;
    }

    GLFWAPI GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *window, GLFWcursorposfun callback) {
        auto old = window->cursorPosCallback;
        window->cursorPosCallback = callback;
        return old;
    }

    GLFWAPI GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow *window, GLFWcursorenterfun callback) {
        auto old = window->cursorEnterCallback;
        window->cursorEnterCallback = callback;
        return old;
    }

    GLFWAPI GLFWscrollfun glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun callback) {
        auto old = window->scrollCallback;
        window->scrollCallback = callback;
        return old;
    }

    GLFWAPI GLFWdropfun glfwSetDropCallback(GLFWwindow *window, GLFWdropfun callback) {
        auto old = window->dropCallback;
        window->dropCallback = callback;
        return old;
    }

    // --- Clipboard ---

    // Synchronisation for read-through of the system pasteboard: the render
    // thread requests an async read on the ArkTS side (ohosRequestClipboardRead)
    // and waits briefly for the mirrored content to arrive via
    // glfwOhosSetClipboardCache. The JS thread is never blocked by the wait,
    // so there is no deadlock.
    static std::mutex sClipboardReadMutex;
    static std::condition_variable sClipboardReadCv;
    static bool sClipboardReadPending = false;
    static std::chrono::steady_clock::time_point sClipboardLastReadAt{};

    // Standard GLFW semantics: the clipboard is process-global and the
    // window argument is ignored (imgui_impl_glfw passes nullptr). Dereferencing
    // the passed pointer would return an offset-into-null address that later
    // strlen()/memcpy() calls crash on.
    GLFWAPI const char *glfwGetClipboardString(GLFWwindow *) {
        auto *window = g().window;
        if (window == nullptr)
            return "";

        // Throttle the system-pasteboard refresh to once per second so
        // repeated queries during a single paste don't stall the render loop.
        const auto now = std::chrono::steady_clock::now();
        bool refresh = false;
        {
            std::lock_guard lock(sClipboardReadMutex);
            refresh = now - sClipboardLastReadAt > std::chrono::seconds(1);
            if (refresh)
                sClipboardLastReadAt = now;
        }

        if (refresh) {
            extern void ohosRequestClipboardRead();
            std::unique_lock lock(sClipboardReadMutex);
            sClipboardReadPending = true;
            ohosRequestClipboardRead();
            // Wait up to 300 ms for the ArkTS side to mirror the system
            // clipboard back; on timeout the previous cache is returned.
            sClipboardReadCv.wait_for(lock, std::chrono::milliseconds(300), [] { return !sClipboardReadPending; });
        }

        return window->clipboard;
    }

    GLFWAPI void glfwSetClipboardString(GLFWwindow *, const char *string) {
        auto *window = g().window;
        if (window == nullptr)
            return;
        const char *src = string != nullptr ? string : "";
        std::strncpy(window->clipboard, src, sizeof(window->clipboard) - 1);
        window->clipboard[sizeof(window->clipboard) - 1] = '\0';

        // Mirror the text into the system clipboard via the ArkTS pasteboard
        // (implemented in the entry layer).
        extern void ohosNotifyClipboardSet(const char *text);
        ohosNotifyClipboardSet(window->clipboard);
    }

    // Updates the in-process clipboard from the system clipboard. Called by
    // the ArkTS side when it observes a pasteboard change or answers a
    // read-through request.
    void glfwOhosSetClipboardCache(const char *text) {
        auto *window = g().window;
        if (window == nullptr || text == nullptr)
            return;
        std::strncpy(window->clipboard, text, sizeof(window->clipboard) - 1);
        window->clipboard[sizeof(window->clipboard) - 1] = '\0';

        // Wake up a glfwGetClipboardString read-through wait, if any.
        {
            std::lock_guard lock(sClipboardReadMutex);
            sClipboardReadPending = false;
            sClipboardReadCv.notify_all();
        }
    }

    // Clears all key/mouse button state and drops queued key/mouse events.
    // Called when the app loses focus: stale "down" state (e.g. a modifier
    // whose release event was lost) would otherwise stick and turn every
    // later key press into a shortcut combination.
    void glfwOhosResetKeys() {
        std::lock_guard lock(g().mutex);
        if (g().window != nullptr) {
            g().window->keys.fill(0);
            g().window->mouseButtons.fill(0);
        }
        auto &queue = g().inputQueue;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [](const GlobalState::InputEvent &e) {
            return e.type == GlobalState::EventType::Key ||
                   e.type == GlobalState::EventType::MouseButton;
        }), queue.end());
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                     "glfwOhosResetKeys: keys cleared");
    }

    // --- Event processing ---

    GLFWAPI void glfwPollEvents(void) {
        auto *window = g().window;
        if (window == nullptr)
            return;

        std::vector<GlobalState::InputEvent> events;
        {
            std::lock_guard lock(g().mutex);
            events.swap(g().inputQueue);
            g().eventPending = false;
        }

        for (const auto &event : events) {
            switch (event.type) {
                case GlobalState::EventType::Key: {
                    int key = event.a;
                    if (key >= 0 && key <= GLFW_KEY_LAST)
                        window->keys[key] = static_cast<char>(event.c);
                    if (window->keyCallback != nullptr)
                        window->keyCallback(window, key, event.b, event.c, event.d);
                    break;
                }
                case GlobalState::EventType::Char: {
                    if (window->charCallback != nullptr)
                        window->charCallback(window, static_cast<unsigned int>(event.a));
                    if (window->charModsCallback != nullptr)
                        window->charModsCallback(window, static_cast<unsigned int>(event.a), event.b);
                    break;
                }
                case GlobalState::EventType::MouseButton: {
                    if (event.a >= 0 && event.a <= GLFW_MOUSE_BUTTON_LAST)
                        window->mouseButtons[event.a] = static_cast<char>(event.b);
                    if (window->mouseButtonCallback != nullptr)
                        window->mouseButtonCallback(window, event.a, event.b, event.c);
                    break;
                }
                case GlobalState::EventType::CursorPos: {
                    window->cursorX = event.x;
                    window->cursorY = event.y;
                    if (window->cursorPosCallback != nullptr)
                        window->cursorPosCallback(window, event.x, event.y);
                    break;
                }
                case GlobalState::EventType::Scroll: {
                    if (window->scrollCallback != nullptr)
                        window->scrollCallback(window, event.x, event.y);
                    break;
                }
                case GlobalState::EventType::SurfaceChanged: {
                    // Applied on the render thread: resizing touches ImGui IO
                    // and ImHex window state, which must not race with the
                    // UI thread.
                    glfwOhosSetWindowSize(event.a, event.b);
                    break;
                }
                case GlobalState::EventType::SurfaceDestroyed: {
                    // The surface is gone; detach EGL on the render thread so
                    // we never destroy a context another thread is using, then
                    // release the remembered native window reference.
                    glfwOhosDetachNativeWindow(window);
                    glfwOhosReleaseLastNativeWindow();
                    break;
                }
                case GlobalState::EventType::Reattach: {
                    // A new XComponent surface arrived (surface recreation /
                    // ability rebuild while the process lives on). Attach it;
                    // the old reference is released inside attach.
                    if (event.ptr != nullptr)
                        glfwOhosAttachNativeWindow(window, event.ptr);
                    break;
                }
                case GlobalState::EventType::Drop: {
                    if (window->dropCallback != nullptr && !event.text.empty()) {
                        std::vector<const char *> paths;
                        std::string current;
                        for (char c : event.text) {
                            if (c == '\n') {
                                paths.push_back(current.c_str());
                                current.clear();
                            } else {
                                current += c;
                            }
                        }
                        // NOTE: the strings must outlive the callback; store them
                        static std::vector<std::string> storage;
                        storage.clear();
                        // re-split properly
                        std::vector<const char *> ptrs;
                        storage.clear();
                        std::string buf = event.text;
                        size_t pos = 0;
                        while ((pos = buf.find('\n')) != std::string::npos) {
                            storage.push_back(buf.substr(0, pos));
                            buf.erase(0, pos + 1);
                        }
                        if (!buf.empty())
                            storage.push_back(buf);
                        for (auto &s : storage)
                            ptrs.push_back(s.c_str());
                        window->dropCallback(window, static_cast<int>(ptrs.size()), ptrs.data());
                    }
                    break;
                }
                case GlobalState::EventType::Close: {
                    if (window->windowCloseCallback != nullptr)
                        window->windowCloseCallback(window);
                    break;
                }
            }
        }
    }

    GLFWAPI void glfwWaitEvents(void) {
        std::unique_lock lock(g().mutex);
        g().eventCond.wait(lock, [] { return g().eventPending; });
        lock.unlock();
        glfwPollEvents();
    }

    GLFWAPI void glfwWaitEventsTimeout(double timeout) {
        std::unique_lock lock(g().mutex);
        g().eventCond.wait_for(lock, std::chrono::duration<double>(timeout), [] { return g().eventPending; });
        lock.unlock();
        glfwPollEvents();
    }

    GLFWAPI void glfwPostEmptyEvent(void) {
        g().queueEvent({ GlobalState::EventType::Focus });
    }

    // --- Joystick / gamepad (unsupported) ---

    GLFWAPI int glfwJoystickPresent(int) { return GLFW_FALSE; }
    GLFWAPI const float *glfwGetJoystickAxes(int, int *count) { if (count) *count = 0; return nullptr; }
    GLFWAPI const unsigned char *glfwGetJoystickButtons(int, int *count) { if (count) *count = 0; return nullptr; }
    GLFWAPI int glfwGetGamepadState(int, GLFWgamepadstate *) { return GLFW_FALSE; }

    GLFWAPI GLFWplatformenum glfwGetPlatform(void) {
        return GLFW_PLATFORM_WAYLAND;
    }

}

// glfw3native.h extension
extern "C" {
    GLFWAPI void *glfwGetX11Window(GLFWwindow *) { return nullptr; }
    GLFWAPI void *glfwGetWaylandWindow(GLFWwindow *window) { return window->nativeWindow; }
}
