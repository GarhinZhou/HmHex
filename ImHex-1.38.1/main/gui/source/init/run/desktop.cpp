#if !defined(OS_WEB)

    #include <hex/api/events/requests_lifecycle.hpp>
    #include <hex/api/imhex_api/system.hpp>
    #include <wolv/utils/guards.hpp>

    #include <init/run.hpp>
    #include <window.hpp>

    #include <GLFW/glfw3.h>

    #if defined(IMHEX_OHOS_PORT)
        #include <hilog/log.h>
    #endif

    namespace hex::init {

        int runImHex() {
            #if defined(IMHEX_OHOS_PORT)
            #define OHOS_RUN_LOG(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative", __VA_ARGS__)
            #else
            #define OHOS_RUN_LOG(...)
            #endif

            OHOS_RUN_LOG("runImHex: glfwInit");
            // Initialize GLFW
            if (!glfwInit()) {
                log::fatal("Failed to initialize GLFW!");
                std::abort();
            }
            ON_SCOPE_EXIT { glfwTerminate(); };

            bool shouldRestart = false;
            do {
                // Register an event handler that will make ImHex restart when requested
                shouldRestart = false;
                RequestRestartImHex::subscribe([&] {
                    shouldRestart = true;
                });

                // Splash window
                {
                    OHOS_RUN_LOG("runImHex: initializeImHex start");
                    auto splashWindow = initializeImHex();
                    OHOS_RUN_LOG("runImHex: initializeImHex done");
                    // Draw the splash window while tasks are running

                    int splashFrames = 0;
                    while (true) {
                        const auto result = splashWindow->loop();
                        if (++splashFrames % 600 == 0)
                            OHOS_RUN_LOG("runImHex: splash loop alive (%d frames)", splashFrames);
                        if (result.has_value()) {
                            OHOS_RUN_LOG("runImHex: splash loop finished (%d frames)", splashFrames);
                            if (result.value() == false) {
                                ImHexApi::System::impl::addInitArgument("tasks-failed");
                            }

                            break;
                        }
                    }

                    handleFileOpenRequest();
                }

                // Main window
                {
                    OHOS_RUN_LOG("runImHex: main window init");
                    Window window;
                    initializationFinished();
                    OHOS_RUN_LOG("runImHex: main window loop start");

                    int mainFrames = 0;
                    window.loop();
                    OHOS_RUN_LOG("runImHex: main window loop ended");
                }

                deinitializeImHex();
            } while (shouldRestart);

            return EXIT_SUCCESS;
        }

    }

#endif