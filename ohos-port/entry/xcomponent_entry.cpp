// ImHex OHOS entry — XComponent integration
// ==========================================
// Exports the fixed OH_NativeXComponent_* symbols that the ArkUI XComponent
// (with libraryname="entry") calls automatically:
//   - OnSurfaceCreated:   attach the native window to the GLFW layer, start ImHex
//   - OnSurfaceChanged:   propagate size changes
//   - OnSurfaceDestroyed: detach
//   - OnDispatchTouchEvent / mouse / key callbacks: forward input to GLFW layer

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <native_window/external_window.h>
#include <multimodalinput/oh_key_code.h>

#include <hex/api/task_manager.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/imhex_api/system.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/events/events_lifecycle.hpp>
#include <hex/api/events/requests_gui.hpp>
#include <hex/api/theme_manager.hpp>
#include <hex/api/content_registry/settings.hpp>

#include <xdg.hpp>

#include <hilog/log.h>
#include <napi/native_api.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

#include <window.hpp>

#define OHOS_LOG_TAG "ImHexNative"
// NOTE: LOG_INFO from the app domain is filtered out by hilog on this system;
// use LOG_ERROR so diagnostic messages are actually visible.
#define OHOS_LOG(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, OHOS_LOG_TAG, __VA_ARGS__)
#define OHOS_LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, OHOS_LOG_TAG, __VA_ARGS__)

// Deferred file opens: files requested before ImHex finished initializing
// (e.g. "Open with HmHex" on a cold start) are queued here and flushed on
// EventImHexStartupFinished, when the RequestOpenFile subscribers exist.
static bool sImHexStarted = false;
static std::mutex sPendingOpenMutex;
static std::vector<std::fs::path> sPendingOpens;

// GLFW layer (glfw_ohos.cpp)
extern "C" {
    void glfwOhosAttachNativeWindow(void *window, void *nativeWindow);
    void glfwOhosDetachNativeWindow(void *window);
    void glfwOhosDispatchKey(int key, int scancode, int action, int mods);
    void glfwOhosDispatchChar(unsigned int codepoint);
    void glfwOhosDispatchMouseButton(int button, int action, int mods);
    void glfwOhosDispatchCursorPos(double x, double y);
    void glfwOhosDispatchScroll(double xOffset, double yOffset);
    void glfwOhosDispatchDrop(int count, const char **paths);
    void glfwOhosSetPendingNativeWindow(void *nativeWindow);
    void *glfwOhosGetWindow();
    void glfwOhosSetWindowSize(int width, int height);
    void glfwOhosSetScreenDpi(int dpi);
    int glfwOhosGetScreenDpi();
    void glfwOhosSetTitleButtonWidth(float width);
    float glfwOhosGetTitleButtonWidth();
    void glfwOhosResetKeys();
    int ohosKeyToGlfwKey(int32_t keyCode);
}

// GLFW key constants used by the NAPI input forwarding
#include <GLFW/glfw3.h>

// ImHex GUI (compiled from ImHex main/gui)
namespace hex::init { int runImHex(); }
namespace hex::messaging { void setupMessaging(); }
namespace hex { class Window; }

namespace hex::crash { void setupCrashHandlers(); }
namespace hex::trace { void enableExceptionCaptureForCurrentThread(); }

// Implemented in libimhex (helpers/fs.cpp, OHOS branch): delivers the path of
// a picked-and-imported file to ImHex's openFileBrowser() callback.
extern "C" void fileBrowserCallback(const char *path);

// Implemented in glfw_ohos.cpp: updates the in-process clipboard cache from
// the system pasteboard (ArkTS side calls this after a pasteboard change).
extern "C" void glfwOhosSetClipboardCache(const char *text);

namespace {

    // Runs when the system dlopens libentry.so (before any XComponent callback).
    // Used to verify the library is actually loaded (signature/linker checks).
    __attribute__((constructor))
    static void libentry_loaded() {
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD002D00, "ImHexNative",
                     "libentry.so loaded by dlopen (constructor)");
    }

    thread_local bool g_dispatchActive = false;
    void startImHexThreadOnce();
    // Notifies the ArkTS side that ImHex shut down and the ability should
    // terminate (defined further below in this translation unit).
    void ohosNotifyExitApp();

    // Points ImHex' data paths (~/.local/share/imhex/...) at the app sandbox
    // files directory and makes sure the plugin folder exists. The ArkTS side
    // passes its context.filesDir explicitly (most reliable); fall back to
    // environment probing for robustness.
    void setupSandboxPaths(const char *explicitFilesDir) {
        std::filesystem::path filesDir;

        if (explicitFilesDir != nullptr && std::filesystem::is_directory(explicitFilesDir)) {
            filesDir = explicitFilesDir;
        } else {
            const char *home = getenv("HOME");
            // In the app sandbox HOME already points at the files directory
            // (e.g. /data/storage/el2/base/haps/entry/files); detect it by
            // name since the explicit filesDir may arrive only later via NAPI.
            if (home != nullptr && std::filesystem::is_directory(home)) {
                std::string homeStr = home;
                if (homeStr.size() >= 6 && homeStr.compare(homeStr.size() - 6, 6, "/files") == 0) {
                    filesDir = home;
                } else if (std::filesystem::is_directory(std::filesystem::path(home) / "files")) {
                    filesDir = std::filesystem::path(home) / "files";
                }
            }
            if (filesDir.empty() && std::filesystem::exists("/data/app/el2/100/base/net.werwolv.imhex/haps/entry/files")) {
                filesDir = "/data/app/el2/100/base/net.werwolv.imhex/haps/entry/files";
            }
        }

        if (!filesDir.empty()) {
            setenv("HOME", filesDir.c_str(), 1);
            std::filesystem::create_directories(filesDir / ".local/share/imhex/plugins");
            std::filesystem::create_directories(filesDir / ".local/share/imhex/libraries");
            // The sandbox linker namespace only allows dlopen from the HAP's
            // native library directory; point ImHex' plugin scanner there.
            // filesDir = <base>/haps/entry/files -> plugins live in <base>/haps/entry/libs/arm64-v8a
            auto libsDir = filesDir.parent_path() / "libs" / "arm64-v8a";
            setenv("IMHEX_OHOS_PLUGIN_DIR", libsDir.c_str(), 1);
            OHOS_LOGE("plugin dir set to %{public}s", libsDir.c_str());
            // xdgpp caches HOME/XDG_* in a static singleton that may already
            // have been initialized with the pre-sandbox HOME; rebuild it so
            // ImHex resolves its data/plugin paths inside the sandbox.
            xdg::BaseDirectories::Reset();
        }
    }

    // Forward a single touch point as a mouse event
    void forwardTouchAsMouse(const OH_NativeXComponent_TouchEvent *touchEvent) {
        if (touchEvent == nullptr || touchEvent->numPoints == 0)
            return;

        const auto &point = touchEvent->touchPoints[0];
        OHOS_LOGE("forwardTouchAsMouse: type=%d n=%d x=%f y=%f", touchEvent->type, touchEvent->numPoints, point.x, point.y);

        switch (touchEvent->type) {
            case OH_NATIVEXCOMPONENT_DOWN:
                glfwOhosDispatchCursorPos(point.x, point.y);
                glfwOhosDispatchMouseButton(0, 1, 0);
                break;
            case OH_NATIVEXCOMPONENT_MOVE:
                glfwOhosDispatchCursorPos(point.x, point.y);
                break;
            case OH_NATIVEXCOMPONENT_UP:
                glfwOhosDispatchCursorPos(point.x, point.y);
                glfwOhosDispatchMouseButton(0, 0, 0);
                break;
            case OH_NATIVEXCOMPONENT_CANCEL:
                glfwOhosDispatchMouseButton(0, 0, 0);
                break;
            default:
                break;
        }
    }

    void dispatchMouseEvent(OH_NativeXComponent *component, void *window) {
        if (g_dispatchActive)
            return;
        g_dispatchActive = true;

        OHOS_LOGE("dispatchMouseEvent: component=%p window=%p", (void*)component, window);

        OH_NativeXComponent_MouseEvent mouseEvent {};
        if (OH_NativeXComponent_GetMouseEvent(component, window, &mouseEvent) == 0) {
            OHOS_LOGE("dispatchMouseEvent: action=%d button=%d x=%f y=%f",
                      mouseEvent.action, mouseEvent.button, mouseEvent.x, mouseEvent.y);
            // Map OHOS mouse action -> GLFW action
            int action = -1;
            switch (mouseEvent.action) {
                case OH_NATIVEXCOMPONENT_MOUSE_PRESS:   action = 1; break;
                case OH_NATIVEXCOMPONENT_MOUSE_RELEASE: action = 0; break;
                case OH_NATIVEXCOMPONENT_MOUSE_MOVE:    action = 2; break; // motion, not used as button
                default: break;
            }

            // Map button
            int button = 0;
            switch (mouseEvent.button) {
                case OH_NATIVEXCOMPONENT_LEFT_BUTTON:   button = 0; break;
                case OH_NATIVEXCOMPONENT_RIGHT_BUTTON:  button = 1; break;
                case OH_NATIVEXCOMPONENT_MIDDLE_BUTTON: button = 2; break;
                default: break;
            }

            glfwOhosDispatchCursorPos(mouseEvent.x, mouseEvent.y);
            if (action == 1 || action == 0)
                glfwOhosDispatchMouseButton(button, action, 0);
        }

        g_dispatchActive = false;
    }

    void dispatchKeyEvent(OH_NativeXComponent *component, void *window) {
        if (component == nullptr)
            return;

        OH_NativeXComponent_KeyEvent *keyEvent = nullptr;
        if (OH_NativeXComponent_GetKeyEvent(component, &keyEvent) != 0 || keyEvent == nullptr)
            return;

        OHOS_LOGE("dispatchKeyEvent: keyEvent=%p", (void*)keyEvent);

        OH_NativeXComponent_KeyAction action {};
        OH_NativeXComponent_KeyCode code {};
        OH_NativeXComponent_EventSourceType sourceType {};

        OH_NativeXComponent_GetKeyEventAction(keyEvent, &action);
        OH_NativeXComponent_GetKeyEventCode(keyEvent, &code);
        OH_NativeXComponent_GetKeyEventSourceType(keyEvent, &sourceType);

        if (sourceType == OH_NATIVEXCOMPONENT_SOURCE_TYPE_KEYBOARD) {
            // Map OHOS keycode -> GLFW keycode (implemented in the GLFW layer)
            int glfwKey = ohosKeyToGlfwKey(static_cast<int32_t>(code));

            int glfwAction = (action == OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN) ? 1 : 0;
            glfwOhosDispatchKey(glfwKey, 0, glfwAction, 0);

            // Convert to a character for text input (best effort via keycode)
            if (glfwAction == 1 && glfwKey >= 'A' && glfwKey <= 'Z')
                glfwOhosDispatchChar(static_cast<unsigned int>(glfwKey));
        }
    }

    // --- XComponent auto-called entry points (fixed symbol names) ---

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) {
        OHOS_LOG("OnSurfaceCreated: window=%p component=%p", window, (void*)component);

        // Native window wrapper
        OHNativeWindow *nativeWindow = OH_NativeWindow_CreateNativeWindow(window);
        if (nativeWindow == nullptr) {
            OHOS_LOGE("OnSurfaceCreated: OH_NativeWindow_CreateNativeWindow failed");
            return;
        }
        OHOS_LOG("OnSurfaceCreated: nativeWindow=%p", (void*)nativeWindow);

        // Register mouse & key callbacks once we have the component handle
        OH_NativeXComponent_MouseEvent_Callback mouseCallback {
            .DispatchMouseEvent = [](OH_NativeXComponent *comp, void *win) {
                dispatchMouseEvent(comp, win);
            }
        };
        OH_NativeXComponent_RegisterMouseEventCallback(component, &mouseCallback);

        OH_NativeXComponent_RegisterKeyEventCallback(component, [](OH_NativeXComponent *comp, void *win) {
            dispatchKeyEvent(comp, win);
        });

        // The GLFW window may not exist yet (ImHex starts lazily). Hand the
        // native window to the GLFW layer; it attaches once the window is created.
        glfwOhosSetPendingNativeWindow(nativeWindow);

        // Start ImHex on a render thread (idempotent)
        startImHexThreadOnce();
    }

    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) {
        (void)component;
        (void)window;
        uint64_t width = 0, height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) == 0) {
            OHOS_LOGE("OnSurfaceChanged: %llu x %llu", (unsigned long long)width, (unsigned long long)height);
            glfwOhosSetWindowSize(static_cast<int>(width), static_cast<int>(height));
        }
    }

    void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) {
        (void)component;
        (void)window;
        if (void *glfwWindow = glfwOhosGetWindow(); glfwWindow != nullptr)
            glfwOhosDetachNativeWindow(glfwWindow);
    }

    void OnDispatchTouchEvent(OH_NativeXComponent *component, void *window) {
        OHOS_LOGE("OnDispatchTouchEvent: component=%p window=%p", (void*)component, window);
        OH_NativeXComponent_TouchEvent touchEvent {};
        if (OH_NativeXComponent_GetTouchEvent(component, window, &touchEvent) == 0)
            forwardTouchAsMouse(&touchEvent);
    }

}

// ---------------------------------------------------------------------------
// NAPI entry (API 12 standard path: surfaceId passed from ArkTS)
// ---------------------------------------------------------------------------

namespace {

    // Starts the ImHex render thread exactly once.
    void startImHexThreadOnce() {
        static std::once_flag started;
        std::call_once(started, [] {
            std::thread([] {
                OHOS_LOG("ImHex thread started");
                try {
                    // Redirect ImHex data paths into the app sandbox so the
                    // plugins deployed by the ArkTS side are found. The NAPI
                    // entry already passes the filesDir when available; this
                    // call covers the XComponent-callback path.
                    setupSandboxPaths(nullptr);
                    OHOS_LOG("step: sandbox paths configured, HOME=%{public}s",
                             getenv("HOME") != nullptr ? getenv("HOME") : "(null)");
                    // Mirrors the initialization steps of the desktop main()
                    using namespace hex;
                    OHOS_LOG("step: setMainThreadId");
                    TaskManager::setMainThreadId(std::this_thread::get_id());
                    TaskManager::setCurrentThreadName("ImHex");
                    OHOS_LOG("step: setupCrashHandlers");
                    crash::setupCrashHandlers();
                    OHOS_LOG("step: enableExceptionCaptureForCurrentThread");
                    trace::enableExceptionCaptureForCurrentThread();
                    OHOS_LOG("step: Window::initNative");
                    Window::initNative();
                    OHOS_LOG("step: messaging::setupMessaging");
                    messaging::setupMessaging();
                    OHOS_LOG("step: calling runImHex()");
                    init::runImHex();
                    OHOS_LOG("runImHex() returned");
                    // ImHex shut down (File -> Exit ImHex): the render thread
                    // is done, but the HAP process/UIAbility would stay alive
                    // with a frozen surface. Ask the ArkTS side to terminate
                    // the ability so the app actually exits.
                    ohosNotifyExitApp();
                } catch (const std::exception &e) {
                    OHOS_LOGE("ImHex thread exception: %{public}s", e.what());
                } catch (...) {
                    OHOS_LOGE("ImHex thread unknown exception");
                }
            }).detach();
        });
    }

    void startImHexWithSurface(uint64_t surfaceId) {
        OHNativeWindow *nativeWindow = nullptr;
        int32_t ret = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId, &nativeWindow);
        if (ret != 0 || nativeWindow == nullptr) {
            OHOS_LOGE("startImHex: CreateNativeWindowFromSurfaceId(%llu) failed ret=%d", (unsigned long long)surfaceId, ret);
            return;
        }
        OHOS_LOG("startImHex: surfaceId=%llu nativeWindow=%p", (unsigned long long)surfaceId, (void*)nativeWindow);

        glfwOhosSetPendingNativeWindow(nativeWindow);
        startImHexThreadOnce();
    }

    napi_value NapiStartImHex(napi_env env, napi_callback_info info) {
        // Deferred file opens: RequestOpenFile subscribers (the builtin
        // plugin) only exist after ImHex finished initializing; a file opened
        // via "Open with HmHex" during a cold start would otherwise be
        // dropped. Queue it here and flush on EventImHexStartupFinished.
        {
            static bool subscribed = false;
            if (!subscribed) {
                subscribed = true;
                hex::EventImHexStartupFinished::subscribe([] {
                    std::lock_guard lock(sPendingOpenMutex);
                    sImHexStarted = true;
                    for (auto &path : sPendingOpens)
                        hex::RequestOpenFile::post(path);
                    sPendingOpens.clear();
                });
            }
        }

        size_t argc = 3;
        napi_value args[3] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        if (argc < 1) {
            OHOS_LOGE("startImHex: missing surfaceId argument");
            return nullptr;
        }

        // Optional second argument: the ArkTS filesDir (sandbox root). When
        // present, ImHex data paths are redirected there so the plugins
        // deployed by the ArkTS side are found.
        if (argc >= 2) {
            napi_valuetype type;
            napi_typeof(env, args[1], &type);
            if (type == napi_string) {
                char buf[512] = { 0 };
                size_t len = 0;
                napi_get_value_string_utf8(env, args[1], buf, sizeof(buf), &len);
                setupSandboxPaths(buf);
                OHOS_LOG("startImHex: filesDir=%{public}s", buf);
            }
        }

        // Optional third argument: screen densityDPI from ArkUI, used for the
        // native UI scale (ImHex's fonts/layout scale with it).
        if (argc >= 3) {
            napi_valuetype type;
            napi_typeof(env, args[2], &type);
            if (type == napi_number) {
                double dpi = 0;
                napi_get_value_double(env, args[2], &dpi);
                glfwOhosSetScreenDpi(static_cast<int>(dpi));
                OHOS_LOG("startImHex: densityDPI=%{public}d", static_cast<int>(dpi));
            }
        }

        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_string) {
            char buf[64] = { 0 };
            size_t len = 0;
            napi_get_value_string_utf8(env, args[0], buf, sizeof(buf), &len);
            startImHexWithSurface(static_cast<uint64_t>(strtoull(buf, nullptr, 10)));
        } else if (type == napi_number) {
            double value = 0;
            napi_get_value_double(env, args[0], &value);
            startImHexWithSurface(static_cast<uint64_t>(value));
        } else {
            OHOS_LOGE("startImHex: unsupported argument type");
        }

        return nullptr;
    }

    napi_value NapiOnTouch(napi_env env, napi_callback_info info) {
        size_t argc = 3;
        napi_value args[3] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 3)
            return nullptr;

        double x = 0, y = 0, action = 0;
        napi_get_value_double(env, args[0], &x);
        napi_get_value_double(env, args[1], &y);
        napi_get_value_double(env, args[2], &action);

        // ArkUI event coordinates are in vp (logical pixels); ImGui renders in
        // physical pixels, so scale by the screen density.
        const double coordScale = glfwOhosGetScreenDpi() / 160.0;
        glfwOhosDispatchCursorPos(x * coordScale, y * coordScale);
        // ArkUI TouchType: Down=0, Up=1, Move=2, Cancel=3
        if (action == 0)
            glfwOhosDispatchMouseButton(0, 1, 0);
        else if (action == 1 || action == 3)
            glfwOhosDispatchMouseButton(0, 0, 0);

        return nullptr;
    }

    // Modifier key state, so ImGui receives correct mods for shortcuts and
    // text input gets the shifted character (uppercase vs lowercase etc.).
    static bool sShiftDown = false;
    static bool sCtrlDown = false;
    static bool sAltDown = false;
    static bool sMetaDown = false;
    static bool sCapsLock = false;

    // Last time a modifier key (shift/ctrl/alt/meta) produced any event.
    // Used to recover from a lost modifier release: the OS keeps sending key
    // repeats (~every 53 ms) while a key is physically held, so a modifier
    // that has been silent for >800 ms is no longer held down.
    static std::chrono::steady_clock::time_point sLastModifierEvent =
        std::chrono::steady_clock::now();

    static unsigned int shiftCharForKey(int glfwKey) {
        switch (glfwKey) {
            case GLFW_KEY_COMMA:         return '<';
            case GLFW_KEY_PERIOD:        return '>';
            case GLFW_KEY_SLASH:         return '?';
            case GLFW_KEY_SEMICOLON:     return ':';
            case GLFW_KEY_APOSTROPHE:    return '"';
            case GLFW_KEY_LEFT_BRACKET:  return '{';
            case GLFW_KEY_RIGHT_BRACKET: return '}';
            case GLFW_KEY_BACKSLASH:     return '|';
            case GLFW_KEY_MINUS:         return '_';
            case GLFW_KEY_EQUAL:         return '+';
            case GLFW_KEY_GRAVE_ACCENT:  return '~';
            default:                     return 0;
        }
    }

    napi_value NapiOnKey(napi_env env, napi_callback_info info) {
        size_t argc = 2;
        napi_value args[2] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 2)
            return nullptr;

        int32_t keyCode = 0;
        double keyAction = 0;
        napi_get_value_int32(env, args[0], &keyCode);
        napi_get_value_double(env, args[1], &keyAction);
        const bool pressed = keyAction == 0;   // ArkUI KeyType: Down=0, Up=1

        // TEMP: diagnose key events (press AND release) to verify modifier
        // key state tracking after clipboard operations.
        OHOS_LOGE("NapiOnKey: keyCode=%{public}d action=%{public}d", (int)keyCode, (int)keyAction);

        const auto now = std::chrono::steady_clock::now();

        // Guard against a lost modifier release: if a modifier is tracked as
        // held but produced no event for >800 ms (the OS repeats the press
        // every ~53 ms while held), it is no longer physically down. Without
        // this, a single lost Ctrl release makes every later key press act as
        // a Ctrl+key shortcut ("keyboard seems dead").
        if (pressed && (sShiftDown || sCtrlDown || sAltDown || sMetaDown) &&
            (now - sLastModifierEvent) > std::chrono::milliseconds(800)) {
            OHOS_LOGE("NapiOnKey: stale modifier state detected, resetting");
            sShiftDown = sCtrlDown = sAltDown = sMetaDown = false;
        }

        // Track modifier key state so ImGui gets correct mods for shortcuts.
        switch (keyCode) {
            case KEYCODE_SHIFT_LEFT: case KEYCODE_SHIFT_RIGHT: sShiftDown = pressed; sLastModifierEvent = now; break;
            case KEYCODE_CTRL_LEFT:  case KEYCODE_CTRL_RIGHT:  sCtrlDown = pressed; sLastModifierEvent = now; break;
            case KEYCODE_ALT_LEFT:   case KEYCODE_ALT_RIGHT:   sAltDown = pressed; sLastModifierEvent = now; break;
            case KEYCODE_META_LEFT:  case KEYCODE_META_RIGHT:  sMetaDown = pressed; sLastModifierEvent = now; break;
            case KEYCODE_CAPS_LOCK:
                if (pressed)
                    sCapsLock = !sCapsLock;
                break;
            default: break;
        }

        const int mods = (sShiftDown ? GLFW_MOD_SHIFT : 0) |
                         (sCtrlDown  ? GLFW_MOD_CONTROL : 0) |
                         (sAltDown   ? GLFW_MOD_ALT : 0) |
                         (sMetaDown  ? GLFW_MOD_SUPER : 0) |
                         (sCapsLock  ? GLFW_MOD_CAPS_LOCK : 0);

        int glfwKey = ohosKeyToGlfwKey(keyCode);
        glfwOhosDispatchKey(glfwKey, 0, pressed ? 1 : 0, mods);

        // Text input: derive the actual character from the key + shift state.
        // Shortcuts must not produce characters: while Ctrl/Alt/Meta are held,
        // the key is a hotkey, not text.
        if (pressed && !sCtrlDown && !sAltDown && !sMetaDown) {
            unsigned int ch = 0;
            if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z) {
                // CapsLock flips the case of letters only.
                const bool upper = sShiftDown ^ sCapsLock;
                ch = upper ? static_cast<unsigned int>(glfwKey) : static_cast<unsigned int>(glfwKey) + 32;
            } else if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9) {
                static const char shiftedDigits[] = ")!@#$%^&*(";
                ch = sShiftDown ? static_cast<unsigned int>(shiftedDigits[glfwKey - GLFW_KEY_0]) : static_cast<unsigned int>(glfwKey);
            } else if (glfwKey == GLFW_KEY_SPACE) {
                ch = 32;
            } else if (glfwKey == GLFW_KEY_COMMA || glfwKey == GLFW_KEY_PERIOD || glfwKey == GLFW_KEY_SLASH ||
                       glfwKey == GLFW_KEY_SEMICOLON || glfwKey == GLFW_KEY_APOSTROPHE || glfwKey == GLFW_KEY_LEFT_BRACKET ||
                       glfwKey == GLFW_KEY_RIGHT_BRACKET || glfwKey == GLFW_KEY_BACKSLASH || glfwKey == GLFW_KEY_MINUS ||
                       glfwKey == GLFW_KEY_EQUAL || glfwKey == GLFW_KEY_GRAVE_ACCENT) {
                ch = sShiftDown ? shiftCharForKey(glfwKey) : static_cast<unsigned int>(glfwKey);
            }
            if (ch != 0)
                glfwOhosDispatchChar(ch);
        }

        return nullptr;
    }

    napi_value NapiOnScroll(napi_env env, napi_callback_info info) {
        size_t argc = 2;
        napi_value args[2] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 2)
            return nullptr;

        double x = 0, y = 0;
        napi_get_value_double(env, args[0], &x);
        napi_get_value_double(env, args[1], &y);
        // ArkUI axis values are fine-grained and sign-inverted relative to
        // GLFW: scale down, flip the vertical direction and clamp so one
        // wheel notch scrolls at most ~1.5 lines.
        auto clampNotch = [](double v) { return std::clamp(v, -1.5, 1.5); };
        glfwOhosDispatchScroll(clampNotch(x / 50.0), clampNotch(-y / 50.0));
        return nullptr;
    }

    napi_value NapiOnMouse(napi_env env, napi_callback_info info) {
        size_t argc = 4;
        napi_value args[4] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 3)
            return nullptr;

        double x = 0, y = 0, action = 0, button = 0;
        napi_get_value_double(env, args[0], &x);
        napi_get_value_double(env, args[1], &y);
        napi_get_value_double(env, args[2], &action);
        if (argc >= 4)
            napi_get_value_double(env, args[3], &button);

        // ArkUI event coordinates are in vp (logical pixels); ImGui renders in
        // physical pixels, so scale by the screen density.
        const double coordScale = glfwOhosGetScreenDpi() / 160.0;
        glfwOhosDispatchCursorPos(x * coordScale, y * coordScale);

        // ArkUI runtime values (verified on device, differ from the enum
        // declaration order): MouseAction Press=1, Release=2, Move=3.
        // MouseButton follows Android: Left=1, Right=2, Middle=3.
        // GLFW: LEFT=0, RIGHT=1, MIDDLE=2.
        auto glfwButtonForArkUi = [](int b) {
            switch (b) {
                case 1:  return 0;   // Left
                case 2:  return 1;   // Right
                case 3:  return 2;   // Middle
                default: return b;
            }
        };
        if (action == 1)  // Press
            glfwOhosDispatchMouseButton(glfwButtonForArkUi((int)button), 1, 0);
        else if (action == 2)  // Release
            glfwOhosDispatchMouseButton(glfwButtonForArkUi((int)button), 0, 0);

        return nullptr;
    }

    // --- File open bridge (DocumentViewPicker) -----------------------------
    // ImHex's openFileBrowser() (fs.cpp) runs on the render thread and needs
    // the ArkTS side to open the system picker. We use a threadsafe function
    // so the notification can be made from any thread; the ArkTS callback then
    // copies the picked file into the sandbox and calls back openFile().

    static napi_threadsafe_function sFileOpenTsfn = nullptr;
    static bool sFileOpenPendingMultiple = false;

    napi_value NapiSetFileOpenCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1) {
            OHOS_LOGE("setFileOpenCallback: missing argument");
            return nullptr;
        }

        if (sFileOpenTsfn != nullptr) {
            napi_release_threadsafe_function(sFileOpenTsfn, napi_tsfn_release);
            sFileOpenTsfn = nullptr;
        }

        // async_resource_name must be a valid string; passing nullptr makes
        // napi_create_threadsafe_function fail and leaves the result null.
        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "fileOpenCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *data) {
                bool multiple = *static_cast<bool *>(data);
                OHOS_LOGE("fileOpenTsfn: calling JS callback, multiple=%d", multiple ? 1 : 0);
                napi_value arg = nullptr;
                napi_get_boolean(env, multiple, &arg);
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 1, &arg, &result);
            },
            &sFileOpenTsfn);
        // NOTE: a failed creation leaves *result untouched; check status first.
        if (status == napi_ok) {
            OHOS_LOGE("setFileOpenCallback: OK tsfn=0x%{public}lx",
                      (unsigned long)(uintptr_t)sFileOpenTsfn);
        } else {
            OHOS_LOGE("setFileOpenCallback: FAILED status=%d", (int)status);
        }
        return nullptr;
    }

    extern "C" void ohosNotifyOpenFileBrowser(bool multiple) {
        OHOS_LOGE("ohosNotifyOpenFileBrowser: multiple=%d tsfn=0x%{public}lx", multiple ? 1 : 0,
                  (unsigned long)(uintptr_t)sFileOpenTsfn);
        if (sFileOpenTsfn != nullptr) {
            sFileOpenPendingMultiple = multiple;
            napi_status status = napi_call_threadsafe_function(sFileOpenTsfn, &sFileOpenPendingMultiple, napi_tsfn_blocking);
            OHOS_LOGE("ohosNotifyOpenFileBrowser: call status=%d", (int)status);
        } else {
            OHOS_LOGE("ohosNotifyOpenFileBrowser: tsfn is NULL!");
        }
    }

    napi_value NapiOpenFile(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_string) {
            char buf[1024] = { 0 };
            size_t len = 0;
            napi_get_value_string_utf8(env, args[0], buf, sizeof(buf), &len);
            OHOS_LOG("openFile: %{public}s", buf);
            fileBrowserCallback(buf);
        }
        return nullptr;
    }

    // Opens a file that arrived from outside ImHex (e.g. a file dropped onto
    // the window). Unlike openFile this does NOT go through the pending file
    // dialog callback: a stale/cancelled dialog would otherwise swallow the
    // request and the file never opens. RequestOpenFile is the same event the
    // CLI and desktop drag & drop use.
    napi_value NapiOpenFileDropped(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_string) {
            char buf[1024] = { 0 };
            size_t len = 0;
            napi_get_value_string_utf8(env, args[0], buf, sizeof(buf), &len);
            OHOS_LOGE("openFileDropped: %{public}s", buf);
            std::lock_guard lock(sPendingOpenMutex);
            if (sImHexStarted)
                hex::RequestOpenFile::post(std::fs::path(buf));
            else
                sPendingOpens.emplace_back(buf);
        }
        return nullptr;
    }

    // --- File save bridge ------------------------------------------------
    // ImHex's save dialog (fs.cpp, DialogMode::Save) notifies the ArkTS side,
    // which opens the system save picker (DocumentViewPicker.save), opens the
    // picked URI and passes its fd back. NapiSaveToFd then writes the current
    // provider data into that fd on the render thread (provider data must be
    // accessed there) and closes it.

    static napi_threadsafe_function sSaveTsfn = nullptr;

    napi_value NapiSetSaveFileCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sSaveTsfn != nullptr) {
            napi_release_threadsafe_function(sSaveTsfn, napi_tsfn_release);
            sSaveTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "saveFileCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *) {
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 0, nullptr, &result);
            },
            &sSaveTsfn);
        OHOS_LOGE("setSaveFileCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifySaveFileBrowser() {
        OHOS_LOGE("ohosNotifySaveFileBrowser: tsfn=0x%{public}lx",
                  (unsigned long)(uintptr_t)sSaveTsfn);
        if (sSaveTsfn != nullptr)
            napi_call_threadsafe_function(sSaveTsfn, nullptr, napi_tsfn_nonblocking);
        else
            OHOS_LOGE("ohosNotifySaveFileBrowser: tsfn is NULL!");
    }

    napi_value NapiSaveToFd(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        int32_t fd = -1;
        napi_get_value_int32(env, args[0], &fd);
        OHOS_LOGE("saveToFd: fd=%{public}d", (int)fd);

        // Write the provider data on the render thread; the provider must not
        // be accessed from the JS thread. TaskManager::doLater runs on it.
        hex::TaskManager::doLater([fd] {
            auto *provider = hex::ImHexApi::Provider::get();
            if (provider == nullptr) {
                OHOS_LOGE("saveToFd: no provider, closing fd");
                ::close(fd);
                return;
            }

            const auto size = provider->getSize();
            std::vector<std::uint8_t> buffer(static_cast<size_t>(size));
            provider->readRaw(0, buffer.data(), buffer.size());

            ssize_t written = ::write(fd, buffer.data(), buffer.size());
            if (written < 0)
                OHOS_LOGE("saveToFd: write failed errno=%{public}d", errno);
            else
                OHOS_LOGE("saveToFd: wrote %{public}zd of %{public}zu bytes", written, buffer.size());
            ::close(fd);

            // The data was written out; clear the dirty flag so ImHex stops
            // asking to save and the watchdog is never triggered again.
            hex::ImHexApi::Provider::resetDirty();
        });

        return nullptr;
    }

    // --- Clipboard bridge (system pasteboard) ------------------------------
    // glfwSetClipboardString runs on the render thread and needs the ArkTS
    // side to write into the system pasteboard; ArkTS observes pasteboard
    // changes and mirrors the content back into the native clipboard cache.

    static napi_threadsafe_function sClipboardTsfn = nullptr;
    static char sClipboardPending[4096] = { 0 };

    napi_value NapiSetClipboardCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sClipboardTsfn != nullptr) {
            napi_release_threadsafe_function(sClipboardTsfn, napi_tsfn_release);
            sClipboardTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "clipboardCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *data) {
                const char *text = static_cast<const char *>(data);
                napi_value arg = nullptr;
                napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &arg);
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 1, &arg, &result);
            },
            &sClipboardTsfn);
        OHOS_LOGE("setClipboardCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifyClipboardSet(const char *text) {
        if (sClipboardTsfn == nullptr || text == nullptr)
            return;
        std::strncpy(sClipboardPending, text, sizeof(sClipboardPending) - 1);
        sClipboardPending[sizeof(sClipboardPending) - 1] = '\0';
        // Non-blocking: the render thread must never stall on the JS thread
        // (e.g. while a key event is being dispatched), that would freeze
        // ImHex's main loop and make input appear dead.
        napi_call_threadsafe_function(sClipboardTsfn, sClipboardPending, napi_tsfn_nonblocking);
    }

    // --- Clipboard read-through (READ_PASTEBOARD) --------------------------
    // glfwGetClipboardString asks the ArkTS side for the current system
    // pasteboard content; ArkTS reads it asynchronously and mirrors it back
    // via updateClipboardCache -> glfwOhosSetClipboardCache.

    static napi_threadsafe_function sClipboardReadTsfn = nullptr;

    napi_value NapiSetClipboardReadCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sClipboardReadTsfn != nullptr) {
            napi_release_threadsafe_function(sClipboardReadTsfn, napi_tsfn_release);
            sClipboardReadTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "clipboardReadCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *) {
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 0, nullptr, &result);
            },
            &sClipboardReadTsfn);
        OHOS_LOGE("setClipboardReadCallback: status=%d", (int)status);
        return nullptr;
    }

    // Called from glfwGetClipboardString (render thread): kick off an async
    // pasteboard read on the ArkTS side. The caller waits on its own side.
    extern "C" void ohosRequestClipboardRead() {
        if (sClipboardReadTsfn == nullptr)
            return;
        napi_call_threadsafe_function(sClipboardReadTsfn, nullptr, napi_tsfn_nonblocking);
    }

    napi_value NapiUpdateClipboardCache(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_string) {
            char buf[4096] = { 0 };
            size_t len = 0;
            napi_get_value_string_utf8(env, args[0], buf, sizeof(buf), &len);
            glfwOhosSetClipboardCache(buf);
        }
        return nullptr;
    }

    // Resets all modifier key state and the GLFW key/mouse arrays. Called
    // from ArkTS when the window loses focus: a modifier stuck as "down"
    // (e.g. its release event was lost while switching apps) would otherwise
    // make every later key press act as a shortcut.
    napi_value NapiResetKeys(napi_env env, napi_callback_info info) {
        sShiftDown = sCtrlDown = sAltDown = sMetaDown = false;
        glfwOhosResetKeys();
        OHOS_LOGE("resetKeys: modifiers cleared");
        return nullptr;
    }

    // Width of the system caption button area (immersive mode); ImHex shifts
    // its own title bar buttons left by this amount.
    napi_value NapiSetTitleButtonWidth(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        double width = 0;
        napi_get_value_double(env, args[0], &width);
        glfwOhosSetTitleButtonWidth(static_cast<float>(width));
        OHOS_LOGE("setTitleButtonWidth: %{public}f", width);
        return nullptr;
    }

    // The ArkTS side calls this while the window is being dragged or resized.
    // During a system window drag the mouse events are consumed by the window
    // manager, so ImHex's idle-frame-rate logic never sees interaction and
    // drops to ~5 FPS, which makes the drag look frozen until release.
    napi_value NapiUnlockFrameRate(napi_env env, napi_callback_info info) {
        hex::ImHexApi::System::unlockFrameRate();
        return nullptr;
    }

    // --- System theme bridge ---------------------------------------------
    // The ArkTS side forwards system dark/light mode changes (UIAbility
    // onConfigurationUpdate); when the "Native" theme is selected, ImHex
    // follows the system theme like the Windows plugin does.
    static bool sLastSystemDark = false;
    static bool sLastSystemDarkValid = false;

    // Called from the builtin plugin when the theme setting switches back to
    // "Native": re-apply the last known system dark/light state, since no
    // system configuration change fires at that point.
    extern "C" bool ohosGetLastSystemTheme(bool &dark) {
        if (!sLastSystemDarkValid)
            return false;
        dark = sLastSystemDark;
        return true;
    }

    napi_value NapiNotifySystemTheme(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        // Accept both a JS boolean and a number (0/1) — ArkTS callers pass a
        // number, and napi_get_value_bool would fail on it, leaving dark=false.
        bool dark = false;
        napi_valuetype type;
        if (napi_typeof(env, args[0], &type) == napi_ok && type == napi_boolean) {
            napi_get_value_bool(env, args[0], &dark);
        } else {
            int32_t value = 0;
            if (napi_get_value_int32(env, args[0], &value) == napi_ok)
                dark = value != 0;
        }
        sLastSystemDark = dark;
        sLastSystemDarkValid = true;

        // RequestChangeTheme synchronously invokes its subscribers, including
        // welcome_screen's updateTextures which creates GL textures. This NAPI
        // runs on the JS thread which has no current GL context, so hop over
        // to the render thread before posting.
        hex::TaskManager::doLater([dark] {
            auto theme = hex::ContentRegistry::Settings::read<std::string>(
                "hex.builtin.setting.interface", "hex.builtin.setting.interface.color",
                hex::ThemeManager::NativeTheme);
            if (theme == hex::ThemeManager::NativeTheme)
                hex::RequestChangeTheme::post(dark ? "Dark" : "Light");
        });

        return nullptr;
    }

    // --- Open webpage bridge -------------------------------------------
    // ImHex's openWebpage() (utils.cpp) notifies the ArkTS side, which opens
    // the URL with the system browser / app picker (openLink).

    static napi_threadsafe_function sWebpageTsfn = nullptr;

    napi_value NapiSetOpenWebpageCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sWebpageTsfn != nullptr) {
            napi_release_threadsafe_function(sWebpageTsfn, napi_tsfn_release);
            sWebpageTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "openWebpageCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *data) {
                const char *url = static_cast<const char *>(data);
                napi_value arg = nullptr;
                napi_create_string_utf8(env, url, NAPI_AUTO_LENGTH, &arg);
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 1, &arg, &result);
            },
            &sWebpageTsfn);
        OHOS_LOGE("setOpenWebpageCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifyOpenWebpage(const char *url) {
        OHOS_LOGE("ohosNotifyOpenWebpage: url=%{public}s", url != nullptr ? url : "(null)");
        static char sWebpageUrl[2048] = { 0 };
        if (url != nullptr) {
            std::strncpy(sWebpageUrl, url, sizeof(sWebpageUrl) - 1);
            sWebpageUrl[sizeof(sWebpageUrl) - 1] = '\0';
        }
        if (sWebpageTsfn != nullptr)
            napi_call_threadsafe_function(sWebpageTsfn, sWebpageUrl, napi_tsfn_nonblocking);
        else
            OHOS_LOGE("ohosNotifyOpenWebpage: tsfn is NULL!");
    }

    // --- Cursor bridge ---------------------------------------------------
    // ImGui changes the mouse cursor shape (text selection, resize, ...);
    // the ArkTS side applies the matching system pointer style.

    static napi_threadsafe_function sCursorTsfn = nullptr;

    napi_value NapiSetCursorCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sCursorTsfn != nullptr) {
            napi_release_threadsafe_function(sCursorTsfn, napi_tsfn_release);
            sCursorTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "cursorCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *data) {
                int shape = *static_cast<int *>(data);
                napi_value arg = nullptr;
                napi_create_int32(env, shape, &arg);
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 1, &arg, &result);
            },
            &sCursorTsfn);
        OHOS_LOGE("setCursorCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifySetCursor(int shape) {
        static int sPendingShape = 0;
        sPendingShape = shape;
        if (sCursorTsfn != nullptr)
            napi_call_threadsafe_function(sCursorTsfn, &sPendingShape, napi_tsfn_nonblocking);
        else
            OHOS_LOGE("ohosNotifySetCursor: tsfn is NULL!");
    }

    // --- App exit bridge ------------------------------------------------
    // ImHex shut down on the render thread; the ArkTS side terminates the
    // UIAbility so the whole app actually exits (otherwise the HAP process
    // stays alive with a frozen XComponent surface).

    static napi_threadsafe_function sExitTsfn = nullptr;

    napi_value NapiSetExitAppCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sExitTsfn != nullptr) {
            napi_release_threadsafe_function(sExitTsfn, napi_tsfn_release);
            sExitTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "exitAppCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *) {
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 0, nullptr, &result);
            },
            &sExitTsfn);
        OHOS_LOGE("setExitAppCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifyExitApp() {
        OHOS_LOGE("ohosNotifyExitApp: tsfn=0x%{public}lx", (unsigned long)(uintptr_t)sExitTsfn);
        if (sExitTsfn != nullptr)
            napi_call_threadsafe_function(sExitTsfn, nullptr, napi_tsfn_blocking);
        else
            OHOS_LOGE("ohosNotifyExitApp: tsfn is NULL!");
    }

    // --- Always-on-top bridge ---------------------------------------------
    // The ImHex settings toggle (settings_entries.cpp) notifies the ArkTS
    // side, which applies it via the system window API (setWindowTopmost).

    static napi_threadsafe_function sTopmostTsfn = nullptr;

    napi_value NapiSetTopmostCallback(napi_env env, napi_callback_info info) {
        size_t argc = 1;
        napi_value args[1] = { nullptr };
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        if (argc < 1)
            return nullptr;

        if (sTopmostTsfn != nullptr) {
            napi_release_threadsafe_function(sTopmostTsfn, napi_tsfn_release);
            sTopmostTsfn = nullptr;
        }

        napi_value resourceName = nullptr;
        napi_create_string_utf8(env, "topmostCallback", NAPI_AUTO_LENGTH, &resourceName);
        napi_status status = napi_create_threadsafe_function(
            env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value jsCallback, void *, void *data) {
                bool topmost = *static_cast<bool *>(data);
                napi_value arg = nullptr;
                napi_get_boolean(env, topmost, &arg);
                napi_value global = nullptr;
                napi_get_global(env, &global);
                napi_value result = nullptr;
                napi_call_function(env, global, jsCallback, 1, &arg, &result);
            },
            &sTopmostTsfn);
        OHOS_LOGE("setTopmostCallback: status=%d", (int)status);
        return nullptr;
    }

    extern "C" void ohosNotifySetWindowTopmost(bool topmost) {
        OHOS_LOGE("ohosNotifySetWindowTopmost: topmost=%d tsfn=0x%{public}lx", topmost ? 1 : 0,
                  (unsigned long)(uintptr_t)sTopmostTsfn);
        static bool sPendingTopmost = false;
        sPendingTopmost = topmost;
        if (sTopmostTsfn != nullptr)
            napi_call_threadsafe_function(sTopmostTsfn, &sPendingTopmost, napi_tsfn_nonblocking);
        else
            OHOS_LOGE("ohosNotifySetWindowTopmost: tsfn is NULL!");
    }

    napi_value NapiModuleInit(napi_env env, napi_value exports) {
        napi_property_descriptor desc[] = {
            { "startImHex", nullptr, NapiStartImHex, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "onTouch",    nullptr, NapiOnTouch,    nullptr, nullptr, nullptr, napi_default, nullptr },
            { "onKey",      nullptr, NapiOnKey,      nullptr, nullptr, nullptr, napi_default, nullptr },
            { "onScroll",   nullptr, NapiOnScroll,   nullptr, nullptr, nullptr, napi_default, nullptr },
            { "onMouse",    nullptr, NapiOnMouse,    nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setFileOpenCallback", nullptr, NapiSetFileOpenCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "openFile",   nullptr, NapiOpenFile,   nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setSaveFileCallback", nullptr, NapiSetSaveFileCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "saveToFd",   nullptr, NapiSaveToFd,   nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setClipboardCallback", nullptr, NapiSetClipboardCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setClipboardReadCallback", nullptr, NapiSetClipboardReadCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "updateClipboardCache", nullptr, NapiUpdateClipboardCache, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "resetKeys",  nullptr, NapiResetKeys,  nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setTopmostCallback", nullptr, NapiSetTopmostCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setTitleButtonWidth", nullptr, NapiSetTitleButtonWidth, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setExitAppCallback", nullptr, NapiSetExitAppCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setOpenWebpageCallback", nullptr, NapiSetOpenWebpageCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setCursorCallback", nullptr, NapiSetCursorCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "unlockFrameRate", nullptr, NapiUnlockFrameRate, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "notifySystemTheme", nullptr, NapiNotifySystemTheme, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "openFileDropped", nullptr, NapiOpenFileDropped, nullptr, nullptr, nullptr, napi_default, nullptr },
        };
        napi_define_properties(env, exports, 21, desc);
        return exports;
    }

}

// ---------------------------------------------------------------------------
// Exported XComponent symbols (kept for compatibility with the libraryname
// mechanism; the NAPI path above is the primary one on API 12+)
// ---------------------------------------------------------------------------

extern "C" {

    void OH_NativeXComponent_OnSurfaceCreated(OH_NativeXComponent *component, void *window) {
        OnSurfaceCreated(component, window);
    }

    void OH_NativeXComponent_OnSurfaceChanged(OH_NativeXComponent *component, void *window) {
        OnSurfaceChanged(component, window);
    }

    void OH_NativeXComponent_OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) {
        OnSurfaceDestroyed(component, window);
    }

    void OH_NativeXComponent_OnDispatchTouchEvent(OH_NativeXComponent *component, void *window) {
        OnDispatchTouchEvent(component, window);
    }

}

static napi_module g_imhexModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = NapiModuleInit,
    .nm_modname = "libentry",
    .nm_priv = nullptr,
    .reserved = { 0 },
};

// Some runtimes match the imported module by the .so basename without the
// "lib" prefix ("entry"), others by the full module name; register both.
static napi_module g_imhexModuleAlt = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = NapiModuleInit,
    .nm_modname = "entry",
    .nm_priv = nullptr,
    .reserved = { 0 },
};

__attribute__((constructor))
static void register_napi_module() {
    napi_module_register(&g_imhexModule);
    napi_module_register(&g_imhexModuleAlt);
}
