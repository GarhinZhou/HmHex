#include <hex/helpers/fs.hpp>

#if defined(IMHEX_OHOS_PORT)
    #include <hilog/log.h>
    #include <hex/api/events/requests_interaction.hpp>
    #include <hex/api/task_manager.hpp>
    #include <wolv/utils/guards.hpp>
    #include <array>
    #include <chrono>
    #include <fstream>
    #include <limits>
    #include <mutex>
    #include <thread>
#endif

#include <hex/helpers/logger.hpp>
#include <hex/helpers/fmt.hpp>
#include <hex/helpers/utils_linux.hpp>
#include <hex/helpers/auto_reset.hpp>

#if defined(OS_WINDOWS)
    #include <windows.h>
    #include <shlobj.h>
    #include <shellapi.h>
#elif defined(OS_LINUX) || defined(OS_WEB)
    #include <xdg.hpp>
# if defined(OS_FREEBSD)
    #include <sys/syslimits.h>
# else
    #include <limits.h>
# endif
#endif

#if defined(OS_WEB)
    #include <emscripten.h>
#else
    #include <GLFW/glfw3.h>
    #include <nfd.hpp>
#endif

#include <filesystem>

#include <wolv/io/file.hpp>
#include <wolv/io/fs.hpp>
#include <wolv/utils/string.hpp>

#include <fmt/format.h>
#include <fmt/xchar.h>

namespace hex::fs {

    static AutoReset<std::function<void(const std::string&)>> s_fileBrowserErrorCallback;
    void setFileBrowserErrorCallback(const std::function<void(const std::string&)> &callback) {
        s_fileBrowserErrorCallback = callback;
    }

    // With help from https://github.com/owncloud/client/blob/cba22aa34b3677406e0499aadd126ce1d94637a2/src/gui/openfilemanager.cpp
    void openFileExternal(std::fs::path filePath) {
        filePath.make_preferred();

        // Make sure the file exists before trying to open it
        if (!wolv::io::fs::exists(filePath)) {
            return;
        }

        #if defined(OS_WINDOWS)
            std::ignore = ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        #elif defined(OS_MACOS)
            std::ignore = system(
                fmt::format("open {}", wolv::util::toUTF8String(filePath)).c_str()
            );
        #elif defined(OS_LINUX)
            executeCmd({"xdg-open", wolv::util::toUTF8String(filePath)});
        #endif
    }

    void openFolderExternal(std::fs::path dirPath) {
        dirPath.make_preferred();

        // Make sure the folder exists before trying to open it
        if (!wolv::io::fs::exists(dirPath)) {
            return;
        }

        #if defined(OS_WINDOWS)
            auto args = fmt::format(L"\"{}\"", dirPath.c_str());
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        #elif defined(OS_MACOS)
            std::ignore = system(
                fmt::format("open {}", wolv::util::toUTF8String(dirPath)).c_str()
            );
        #elif defined(OS_LINUX)
            executeCmd({"xdg-open", wolv::util::toUTF8String(dirPath)});
        #endif
    }

    void openFolderWithSelectionExternal(std::fs::path selectedFilePath) {
        selectedFilePath.make_preferred();

        // Make sure the file exists before trying to open it
        if (!wolv::io::fs::exists(selectedFilePath)) {
            return;
        }

        #if defined(OS_WINDOWS)
            auto args = fmt::format(L"/select,\"{}\"", selectedFilePath.c_str());
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        #elif defined(OS_MACOS)
            std::ignore = system(
                fmt::format(
                    R"(osascript -e 'tell application "Finder" to reveal POSIX file "{}"')",
                    wolv::util::toUTF8String(selectedFilePath)
                ).c_str()
            );
            system(R"(osascript -e 'tell application "Finder" to activate')");
        #elif defined(OS_LINUX)
            // Fallback to only opening the folder for now
            // TODO actually select the file
            executeCmd({"xdg-open", wolv::util::toUTF8String(selectedFilePath.parent_path())});
        #endif
    }

    #if defined(OS_WEB)

        std::function<void(std::fs::path)> currentCallback;

        EMSCRIPTEN_KEEPALIVE
        extern "C" void fileBrowserCallback(char* path) {
            currentCallback(path);
        }

        EM_JS(int, callJs_saveFile, (const char *rawFilename), {
            let filename = UTF8ToString(rawFilename) || "file.bin";
            FS.createPath("/", "savedFiles");

            if (FS.analyzePath(filename).exists) {
                FS.unlink(filename);
            }

            // Call callback that will write the file
            Module._fileBrowserCallback(stringToNewUTF8("/savedFiles/" + filename));

            let data;
            try {
                data = FS.readFile("/savedFiles/" + filename);
            } catch (e) {
                console.log(e);
                return;
            }

            const reader = Object.assign(new FileReader(), {
                onload: () => {

                    // Show popup to user to download
                    let saver = document.createElement('a');
                    saver.href = reader.result;
                    saver.download = filename;
                    saver.style = "display: none";

                    saver.click();

                },
                onerror: () => {
                    throw new Error(reader.error);
                },
            });
            reader.readAsDataURL(new File([data], "", { type: "application/octet-stream" }));

        });

        EM_JS(int, callJs_openFile, (bool multiple), {
            let selector = document.createElement("input");
            selector.type = "file";
            selector.style = "display: none";
            if (multiple) {
                selector.multiple = true;
            }
            selector.onchange = () => {
                if (selector.files.length == 0) return;

                FS.createPath("/", "openedFiles");
                for (let file of selector.files) {
                    const fr = new FileReader();
                    fr.onload = () => {
                        let folder = "/openedFiles/"+Math.random().toString(36).substring(2)+"/";
                        FS.createPath("/", folder);
                        if (FS.analyzePath(folder+file.name).exists) {
                            console.log(`Error: ${folder+file.name} already exist`);
                        } else {
                            FS.createDataFile(folder, file.name, fr.result, true, true);
                            Module._fileBrowserCallback(stringToNewUTF8(folder+file.name));
                        }
                    };

                    fr.readAsBinaryString(file);
                }
            };
            selector.click();
        });

        bool openFileBrowser(DialogMode mode, const std::vector<ItemFilter> &validExtensions, const std::function<void(std::fs::path)> &callback, const std::string &defaultPath, bool multiple) {
            switch (mode) {
                case DialogMode::Open: {
                    currentCallback = callback;
                    callJs_openFile(multiple);
                    break;
                }
                case DialogMode::Save: {
                    currentCallback = callback;
                    std::fs::path path;

                    if (!defaultPath.empty())
                        path = std::fs::path(defaultPath).filename();
                    else if (!validExtensions.empty())
                        path = "file." + validExtensions[0].spec;

                    std::fs::create_directory("/savedFiles");
                    callJs_saveFile(path.filename().string().c_str());
                    break;
                }
                case DialogMode::Folder: {
                    throw std::logic_error("Selecting a folder is not implemented");
                    return false;
                }
                default:
                    std::unreachable();
            }
            return true;
        }

    #elif defined(IMHEX_OHOS_PORT)

        // OHOS: file dialogs are opened by the ArkTS side (DocumentViewPicker).
        // openFileBrowser stores the callback and notifies the ArkUI layer;
        // once the user picked files and they were copied into the sandbox,
        // the ArkTS side calls back into fileBrowserCallback() with the path.
        // The callback is consumed (cleared) on use so a cancelled dialog can
        // never fire a stale callback. A mutex guards it because the callback
        // is written on the render thread but consumed on the JS thread.
        static std::mutex sDialogMutex;
        std::function<void(std::fs::path)> currentCallback;
        std::fs::path currentSaveTmpDir;

        // Implemented in the OHOS entry layer (xcomponent_entry.cpp).
        extern "C" void ohosNotifyOpenFileBrowser(bool multiple);
        extern "C" void ohosNotifySaveFileBrowser();

        extern "C" void fileBrowserCallback(const char *path) {
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "fileBrowserCallback: %{public}s", path != nullptr ? path : "(null)");
            if (path == nullptr)
                return;

            std::function<void(std::fs::path)> callback;
            {
                std::scoped_lock lock(sDialogMutex);
                callback = std::move(currentCallback);
            }

            if (callback) {
                // The callback is ImHex UI logic (provider creation, toasts,
                // ImGui state) and must run on the render thread, never on
                // the NAPI/JS thread. Copy the path before leaving this
                // thread — it points into a NAPI-owned buffer.
                hex::TaskManager::doLater([callback = std::move(callback), path = std::string(path)] {
                    callback(path);
                });
            } else {
                // No dialog is pending — the path arrived from outside ImHex
                // (e.g. a file dropped onto the window). Open it directly via
                // the same event the CLI and desktop drag & drop use; the
                // subscriber queues the actual provider open on the ImHex
                // thread via TaskManager::doLater.
                RequestOpenFile::post(path);
            }
        }

        // Clears any pending dialog callback. Called by the ArkTS side when
        // the user cancels the system picker, so a stale callback can never
        // fire later. Also notifies an optional cancel hook (used by the
        // close-with-unsaved-changes flow to abort the pending close).
        static std::function<void()> sDialogCancelCallback;

        extern "C" void ohosSetFileDialogCancelCallback(void (*callback)()) {
            sDialogCancelCallback = callback != nullptr ? std::function<void()>(callback) : std::function<void()>();
        }

        extern "C" void ohosCancelFileDialog() {
            {
                std::scoped_lock lock(sDialogMutex);
                currentCallback = nullptr;
            }
            if (sDialogCancelCallback)
                sDialogCancelCallback();
        }

        bool openFileBrowser(DialogMode mode, const std::vector<ItemFilter> &validExtensions, const std::function<void(std::fs::path)> &callback, const std::string &defaultPath, bool multiple) {
            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                         "openFileBrowser: mode=%d multiple=%d", static_cast<int>(mode), multiple ? 1 : 0);
            switch (mode) {
                case DialogMode::Open: {
                    std::scoped_lock lock(sDialogMutex);
                    currentCallback = callback;
                    ohosNotifyOpenFileBrowser(multiple);
                    break;
                }
                case DialogMode::Save: {
                    // The ArkTS side opens the system save picker
                    // (DocumentViewPicker.save) and passes the resulting file
                    // descriptor back. ohosSaveToFd then runs the pending
                    // callback against a sandbox temp file and streams the
                    // generated content into the fd, so "Save As" / exporters
                    // produce the correct output instead of raw provider data.
                    // Each save gets a unique temp dir so two overlapping
                    // saves can never corrupt each other's output.
                    std::scoped_lock lock(sDialogMutex);
                    currentCallback = callback;
                    static u64 tmpDirCounter = 0;
                    const char *home = getenv("HOME");
                    const auto base = std::fs::path(home != nullptr ? home : "/data/storage/el2/base/haps/entry/files");
                    currentSaveTmpDir = base / (".hmhex_export_tmp_" + std::to_string(tmpDirCounter++));
                    ohosNotifySaveFileBrowser();
                    break;
                }
                case DialogMode::Folder: {
                    // No folder picker on OHOS: fall back to the app sandbox
                    // home so exporters etc. have a valid writable target.
                    OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                 "openFileBrowser: Folder dialog, falling back to HOME");
                    const char *home = getenv("HOME");
                    callback(home != nullptr ? std::fs::path(home) : std::fs::path("/data/storage/el2/base/haps/entry/files"));
                    break;
                }
                default:
                    std::unreachable();
            }
            return true;
        }

        // Called on the render thread once the save picker returned a writable
        // fd (see xcomponent_entry.cpp NapiSaveToFd). Runs the pending save
        // callback into a unique sandbox temp dir, waits for the generated
        // file to stabilize (callbacks may write asynchronously, e.g. provider
        // saveAs), then streams the result into the fd. The temp dir is always
        // cleaned up on every exit path (success or failure).
        extern "C" void ohosSaveToFd(int fd) {
            std::function<void(std::fs::path)> callback;
            std::fs::path tmpDir;
            {
                std::scoped_lock lock(sDialogMutex);
                callback = std::move(currentCallback);
                tmpDir   = currentSaveTmpDir;
            }

            if (!callback || tmpDir.empty()) {
                OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                             "ohosSaveToFd: no pending save callback, closing fd");
                ::close(fd);
                return;
            }

            std::error_code ec;
            std::fs::create_directories(tmpDir, ec);
            if (ec) {
                OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                             "ohosSaveToFd: cannot create temp dir: %s", ec.message().c_str());
                ::close(fd);
                return;
            }

            // Run the actual save logic. It may start an async task
            // (provider->saveAs), so the file may not exist yet.
            try {
                callback(tmpDir / "out");
            } catch (const std::exception &e) {
                OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                             "ohosSaveToFd: save callback threw: %s", e.what());
                std::error_code rmEc;
                std::fs::remove_all(tmpDir, rmEc);
                ::close(fd);
                return;
            }

            // Wait for the generated file to stabilize on a background task,
            // then stream it into the fd.
            hex::TaskManager::createBackgroundTask("hex.builtin.task.ohos_save_export", [fd, tmpDir](auto &) {
                // Clean up the temp dir on every exit path. Each save uses
                // its own directory, so this can never delete another save's
                // in-flight output.
                ON_SCOPE_EXIT {
                    std::error_code rmEc;
                    std::fs::remove_all(tmpDir, rmEc);
                    if (rmEc)
                        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                     "ohosSaveToFd: temp dir cleanup failed: %s", rmEc.message().c_str());
                };

                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                std::fs::path result;
                auto lastSize = std::numeric_limits<size_t>::max();

                while (std::chrono::steady_clock::now() < deadline) {
                    // Find the newest regular file in the temp dir.
                    std::fs::path newest;
                    auto newestTime = std::filesystem::file_time_type::min();
                    std::error_code iterEc;
                    for (const auto &entry : std::fs::directory_iterator(tmpDir, iterEc)) {
                        std::error_code statEc;
                        if (!entry.is_regular_file(statEc) || statEc)
                            continue;
                        const auto t = entry.last_write_time(statEc);
                        if (statEc)
                            continue;
                        if (newest.empty() || t > newestTime) {
                            newest    = entry.path();
                            newestTime = t;
                        }
                    }
                    if (iterEc) {
                        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                     "ohosSaveToFd: temp dir iteration failed");
                        break;
                    }

                    if (newest.empty()) {
                        lastSize = std::numeric_limits<size_t>::max(); // no file yet
                    } else {
                        std::error_code sizeEc;
                        const auto size = std::fs::file_size(newest, sizeEc);
                        if (!sizeEc && size == lastSize) {
                            // Size stable across two samples: writing finished.
                            result = newest;
                            break;
                        }
                        lastSize = sizeEc ? std::numeric_limits<size_t>::max() : size;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                // Timeout fallback: a slow async writer may have produced the
                // file but never looked "stable" within the window. If a file
                // exists now, export it anyway instead of giving the user an
                // empty file.
                if (result.empty()) {
                    std::error_code iterEc;
                    for (const auto &entry : std::fs::directory_iterator(tmpDir, iterEc)) {
                        std::error_code statEc;
                        if (entry.is_regular_file(statEc) && !statEc) {
                            result = entry.path();
                            break;
                        }
                    }
                    if (result.empty()) {
                        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                     "ohosSaveToFd: timed out waiting for export file");
                        ::close(fd);
                        return;
                    }
                    OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                 "ohosSaveToFd: file never stabilized, exporting anyway");
                }

                // Stream the generated file into the user-chosen fd.
                std::ifstream stream(result, std::ios::binary);
                if (!stream) {
                    OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                 "ohosSaveToFd: cannot open export file");
                    ::close(fd);
                    return;
                }

                std::array<char, 65536> buffer;
                size_t total = 0;
                while (stream) {
                    stream.read(buffer.data(), buffer.size());
                    const auto count = stream.gcount();
                    if (count <= 0)
                        break;

                    size_t offset = 0;
                    while (offset < static_cast<size_t>(count)) {
                        const auto written = ::write(fd, buffer.data() + offset, count - offset);
                        if (written < 0 && errno == EINTR)
                            continue;
                        if (written <= 0) {
                            OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                                         "ohosSaveToFd: write failed errno=%{public}d", errno);
                            ::close(fd);
                            return;
                        }
                        offset += written;
                    }
                    total += offset;
                }
                ::close(fd);
                OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD002D00, "ImHexNative",
                             "ohosSaveToFd: wrote %{public}zu bytes", total);
            });
        }

    #else
        bool openFileBrowser(DialogMode mode, const std::vector<ItemFilter> &validExtensions, const std::function<void(std::fs::path)> &callback, const std::string &defaultPath, bool multiple) {
            // Turn the content of the ItemFilter objects into something NFD understands
            std::vector<nfdfilteritem_t> validExtensionsNfd;
            validExtensionsNfd.reserve(validExtensions.size());
            for (const auto &extension : validExtensions) {
                validExtensionsNfd.emplace_back(nfdfilteritem_t{ extension.name.c_str(), extension.spec.c_str() });
            }

            // Clear errors from previous runs
            NFD::ClearError();

            // Try to initialize NFD
            if (NFD::Init() != NFD_OKAY) {
                // Handle errors if initialization failed
                log::error("NFD init returned an error: {}", NFD::GetError());
                if (*s_fileBrowserErrorCallback != nullptr) {
                    const auto error = NFD::GetError();
                    (*s_fileBrowserErrorCallback)(error != nullptr ? error : "No details");
                }

                return false;
            }

            NFD::UniquePathU8 outPath;
            NFD::UniquePathSet outPaths;
            nfdresult_t result = NFD_ERROR;

            // Open the correct file dialog based on the mode
            switch (mode) {
                case DialogMode::Open:
                    if (multiple)
                        result = NFD::OpenDialogMultiple(outPaths, validExtensionsNfd.data(), validExtensionsNfd.size(), defaultPath.empty() ? nullptr : defaultPath.c_str());
                    else
                        result = NFD::OpenDialog(outPath, validExtensionsNfd.data(), validExtensionsNfd.size(), defaultPath.empty() ? nullptr : defaultPath.c_str());
                    break;
                case DialogMode::Save:
                    result = NFD::SaveDialog(outPath, validExtensionsNfd.data(), validExtensionsNfd.size(), defaultPath.empty() ? nullptr : defaultPath.c_str());
                    break;
                case DialogMode::Folder:
                    result = NFD::PickFolder(outPath, defaultPath.empty() ? nullptr : defaultPath.c_str());
                    break;
            }

            if (result == NFD_OKAY){
                // Handle the path if the dialog was opened in single mode
                if (outPath != nullptr) {
                    // Call the provided callback with the path
                    callback(outPath.get());
                }

                // Handle multiple paths if the dialog was opened in multiple mode
                if (outPaths != nullptr) {
                    nfdpathsetsize_t numPaths = 0;
                    if (NFD::PathSet::Count(outPaths, numPaths) == NFD_OKAY) {
                        // Loop over all returned paths and call the callback with each of them
                        for (size_t i = 0; i < numPaths; i++) {
                            NFD::UniquePathSetPath path;
                            if (NFD::PathSet::GetPath(outPaths, i, path) == NFD_OKAY)
                                callback(path.get());
                        }
                    }
                }
            } else if (result == NFD_ERROR) {
                // Handle errors that occurred during the file dialog call

                log::error("Requested file dialog returned an error: {}", NFD::GetError());

                if (*s_fileBrowserErrorCallback != nullptr) {
                    const auto error = NFD::GetError();
                    (*s_fileBrowserErrorCallback)(error != nullptr ? error : "No details");
                }
            }

            NFD::Quit();

            return result == NFD_OKAY;
        }

    #endif

    bool isPathWritable(const std::fs::path &path) {
        constexpr static auto TestFileName = "__imhex__tmp__";

        // Try to open the __imhex__tmp__ file in the given path
        // If one does exist already, try to delete it
        {
            wolv::io::File file(path / TestFileName, wolv::io::File::Mode::Read);
            if (file.isValid()) {
                if (!file.remove())
                    return false;
            }
        }

        // Try to create a new file in the given path
        // If that fails, or the file cannot be deleted anymore afterward; the path is not writable
        wolv::io::File file(path / TestFileName, wolv::io::File::Mode::Create);
        const bool result = file.isValid();
        if (!file.remove())
            return false;

        return result;
    }

    std::fs::path toShortPath(const std::fs::path &path) {
        #if defined(OS_WINDOWS)
            // Get the size of the short path
            size_t size = GetShortPathNameW(path.c_str(), nullptr, 0);
            if (size == 0)
                return path;

            // Get the short path
            std::wstring newPath(size, 0x00);
            GetShortPathNameW(path.c_str(), newPath.data(), newPath.size());
            newPath.pop_back();

            return newPath;
        #else
            // Other supported platforms don't have short paths
            return path;
        #endif
    }


}
