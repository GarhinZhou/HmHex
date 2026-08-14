#include <hex/helpers/magic.hpp>

#include <hex/helpers/utils.hpp>
#include <hex/helpers/fs.hpp>
#include <hex/helpers/logger.hpp>
#include <hex/helpers/default_paths.hpp>

#include <wolv/utils/guards.hpp>
#include <wolv/utils/string.hpp>

#include <hex/providers/provider.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <magic.h>
#include <hex/api/task_manager.hpp>
#include <hex/api/content_registry/pattern_language.hpp>
#include <hex/helpers/binary_pattern.hpp>

#if defined(_MSC_VER)
    #include <direct.h>
#else
    #include <unistd.h>
#endif

#if defined(OS_WINDOWS)
    #define MAGIC_PATH_SEPARATOR ";"
#else
    #define MAGIC_PATH_SEPARATOR ":"
#endif


namespace hex::magic {

    static std::optional<std::string> getMagicFiles(bool sourceFiles = false) {
        std::string magicFiles;

        std::error_code error;
        for (const auto &dir : paths::Magic.read()) {
            for (const auto &entry : std::fs::directory_iterator(dir, error)) {
                auto path = std::fs::absolute(entry.path());

                if (sourceFiles) {
                    if (path.extension().empty() || entry.is_directory())
                        magicFiles += wolv::util::toUTF8String(path) + MAGIC_PATH_SEPARATOR;
                } else {
                    if (path.extension() == ".mgc")
                        magicFiles += wolv::util::toUTF8String(path) + MAGIC_PATH_SEPARATOR;
                }
            }
        }

        if (!magicFiles.empty())
            magicFiles.pop_back();

        if (error)
            return std::nullopt;
        else
            return magicFiles;
    }

    bool compile() {
        magic_t ctx = magic_open(MAGIC_CHECK);
        ON_SCOPE_EXIT { magic_close(ctx); };

        auto magicFiles = getMagicFiles(true);

        if (!magicFiles.has_value())
            return false;

        if (magicFiles->empty())
            return true;

        std::array<char, 1024> cwd = { };
        if (getcwd(cwd.data(), cwd.size()) == nullptr)
            return false;

        std::optional<std::fs::path> magicFolder;
        for (const auto &dir : paths::Magic.write()) {
            if (std::fs::exists(dir) && fs::isPathWritable(dir)) {
                magicFolder = dir;
                break;
            }
        }

        if (!magicFolder.has_value()) {
            log::error("Could not find a writable magic folder");
            return false;
        }

        if (chdir(wolv::util::toUTF8String(*magicFolder).c_str()) != 0)
            return false;

        auto result = magic_compile(ctx, magicFiles->c_str()) == 0;
        if (!result) {
            log::error("Failed to compile magic files \"{}\": {}", *magicFiles, magic_error(ctx));
        }

        if (chdir(cwd.data()) != 0)
            return false;

        return result;
    }

    std::string getDescription(const std::vector<u8> &data, bool firstEntryOnly) {
        if (data.empty()) return "";

        auto magicFiles = getMagicFiles();

        if (magicFiles.has_value()) {
            magic_t ctx = magic_open(firstEntryOnly ? MAGIC_NONE : MAGIC_CONTINUE);
            ON_SCOPE_EXIT { magic_close(ctx); };

            if (magic_load(ctx, magicFiles->c_str()) == 0) {
                if (auto description = magic_buffer(ctx, data.data(), data.size()); description != nullptr) {
                    auto result = wolv::util::replaceStrings(description, "\\012-", "\n-");
                    if (result.ends_with("- data"))
                        result = result.substr(0, result.size() - 6);

                    return result;
                }
            }
        }

        return "";
    }

    std::string getDescription(prv::Provider *provider, u64 address, size_t size, bool firstEntryOnly) {
        std::vector<u8> buffer(std::min<u64>(provider->getSize(), size), 0x00);
        provider->read(address, buffer.data(), buffer.size());

        return getDescription(buffer, firstEntryOnly);
    }

    std::string getMIMEType(const std::vector<u8> &data, bool firstEntryOnly) {
        if (data.empty()) return "";

        auto magicFiles = getMagicFiles();

        if (magicFiles.has_value()) {
            magic_t ctx = magic_open(MAGIC_MIME_TYPE | (firstEntryOnly ? MAGIC_NONE : MAGIC_CONTINUE));
            ON_SCOPE_EXIT { magic_close(ctx); };

            if (magic_load(ctx, magicFiles->c_str()) == 0) {
                if (auto mimeType = magic_buffer(ctx, data.data(), data.size()); mimeType != nullptr) {
                    auto result = wolv::util::replaceStrings(mimeType, "\\012-", "\n-");
                    if (result.ends_with("- application/octet-stream"))
                        result = result.substr(0, result.size() - 26);

                    return result;
                }
            }
        }

        return "";
    }

    std::string getMIMEType(prv::Provider *provider, u64 address, size_t size, bool firstEntryOnly) {
        std::vector<u8> buffer(std::min<u64>(provider->getSize(), size), 0x00);
        provider->read(address, buffer.data(), buffer.size());

        return getMIMEType(buffer, firstEntryOnly);
    }

    std::string getExtensions(prv::Provider *provider, u64 address, size_t size, bool firstEntryOnly) {
        std::vector<u8> buffer(std::min<u64>(provider->getSize(), size), 0x00);
        provider->read(address, buffer.data(), buffer.size());

        return getExtensions(buffer, firstEntryOnly);
    }

    std::string getExtensions(const std::vector<u8> &data, bool firstEntryOnly) {
        if (data.empty()) return "";

        auto magicFiles = getMagicFiles();

        if (magicFiles.has_value()) {
            magic_t ctx = magic_open(MAGIC_EXTENSION | (firstEntryOnly ? MAGIC_NONE : MAGIC_CONTINUE));
            ON_SCOPE_EXIT { magic_close(ctx); };

            if (magic_load(ctx, magicFiles->c_str()) == 0) {
                if (auto extension = magic_buffer(ctx, data.data(), data.size()); extension != nullptr) {
                    auto result = wolv::util::replaceStrings(extension, "\\012-", "\n-");
                    if (result.ends_with("- ???"))
                        result = result.substr(0, result.size() - 5);

                    return result;
                }
            }
        }

        return "";
    }

    std::string getAppleCreatorType(prv::Provider *provider, u64 address, size_t size, bool firstEntryOnly) {
        std::vector<u8> buffer(std::min<u64>(provider->getSize(), size), 0x00);
        provider->read(address, buffer.data(), buffer.size());

        return getAppleCreatorType(buffer, firstEntryOnly);
    }

    std::string getAppleCreatorType(const std::vector<u8> &data, bool firstEntryOnly) {
        if (data.empty()) return "";

        auto magicFiles = getMagicFiles();

        if (magicFiles.has_value()) {
            magic_t ctx = magic_open(MAGIC_APPLE | (firstEntryOnly ? MAGIC_NONE : MAGIC_CONTINUE));
            ON_SCOPE_EXIT { magic_close(ctx); };

            if (magic_load(ctx, magicFiles->c_str()) == 0) {
                if (auto result = magic_buffer(ctx, data.data(), data.size()); result != nullptr)
                    return wolv::util::replaceStrings(result, "\\012-", "\n-");
            }
        }

        return {};
    }

    bool isValidMIMEType(const std::string &mimeType) {
        // MIME types always contain a slash
        if (!mimeType.contains("/"))
            return false;

        // The MIME type "application/octet-stream" is a fallback type for arbitrary binary data.
        // Specifying this in a pattern would make it get suggested for every single unknown binary that's being loaded.
        // We don't want that, so we ignore it here
        if (mimeType == "application/octet-stream")
            return false;

        return true;
    }

    // ---- Pattern MIME index (lightweight) ----------------------------------
    // The bundled patterns declare their file types with C-style pragmas
    // ("#pragma MIME application/x-elf"), which the pl lexer's
    // getPragmaValues does not match (it expects @pragma("key","value")).
    // Scanning every pattern file with the full lexer on every open is also
    // slow, so build a MIME -> pattern index once by scanning only the file
    // header for both pragma styles.

    struct PatternMeta {
        std::fs::path path;
        std::string author;
        std::string description;
        std::vector<std::string> mimes;
        std::vector<std::string> magics;
    };

    static std::once_flag s_patternIndexFlag;
    static std::vector<PatternMeta> s_patternMetas;
    static std::unordered_map<std::string, std::vector<size_t>> s_mimeIndex;

    static std::multimap<std::string, std::string> scanPatternHeaderPragmas(const std::string &head) {
        std::multimap<std::string, std::string> result;

        // C-style: "#pragma key value"
        size_t pos = 0;
        while ((pos = head.find("#pragma", pos)) != std::string::npos) {
            auto eol = head.find('\n', pos);
            if (eol == std::string::npos)
                eol = head.size();

            auto line = wolv::util::trim(head.substr(pos + 7, eol - pos - 7));
            auto sep = line.find_first_of(" \t");
            if (sep != std::string::npos)
                result.emplace(line.substr(0, sep), wolv::util::trim(line.substr(sep + 1)));

            pos = eol + 1;
        }

        // pl-style: '@pragma("key", "value")'
        pos = 0;
        while ((pos = head.find("@pragma", pos)) != std::string::npos) {
            auto open = head.find('(', pos);
            auto close = head.find(')', pos);
            if (open == std::string::npos || close == std::string::npos || close < open) {
                pos = open == std::string::npos ? head.size() : open + 1;
                continue;
            }
            auto args = head.substr(open + 1, close - open - 1);
            auto comma = args.find(',');
            if (comma != std::string::npos) {
                auto trimQuotes = [](std::string s) {
                    s = wolv::util::trim(s);
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                        return s.substr(1, s.size() - 2);
                    return s;
                };
                result.emplace(trimQuotes(args.substr(0, comma)), trimQuotes(args.substr(comma + 1)));
            }
            pos = close + 1;
        }

        return result;
    }

    static void buildPatternIndex() {
        std::call_once(s_patternIndexFlag, [] {
            for (const auto &dir : paths::Patterns.read()) {
                std::error_code errorCode;
                for (auto &entry : std::fs::recursive_directory_iterator(dir, errorCode)) {
                    if (!entry.is_regular_file())
                        continue;
                    if (entry.path().extension() != ".hexpat")
                        continue;

                    wolv::io::File file(entry.path(), wolv::io::File::Mode::Read);
                    if (!file.isValid())
                        continue;

                    // Pragmas live at the top of the file; scanning the first
                    // 16 KiB is plenty and keeps this fast.
                    auto head = file.readString(16 * 1024);
                    auto pragmas = scanPatternHeaderPragmas(head);

                    PatternMeta meta;
                    meta.path = entry.path();
                    if (auto it = pragmas.find("author"); it != pragmas.end())
                        meta.author = it->second;
                    if (auto it = pragmas.find("description"); it != pragmas.end())
                        meta.description = it->second;
                    for (auto [it, end] = pragmas.equal_range("MIME"); it != end; ++it)
                        meta.mimes.push_back(it->second);
                    for (auto [it, end] = pragmas.equal_range("magic"); it != end; ++it)
                        meta.magics.push_back(it->second);

                    const auto index = s_patternMetas.size();
                    s_patternMetas.push_back(std::move(meta));
                    for (const auto &mime : s_patternMetas[index].mimes)
                        if (isValidMIMEType(mime))
                            s_mimeIndex[mime].push_back(index);
                }
            }
        });
    }

    std::vector<FoundPattern> findViablePatterns(prv::Provider *provider, Task* task) {
        std::vector<FoundPattern> result;

        buildPatternIndex();

        auto mimeType = getMIMEType(provider, 0, 4_KiB, true);

        // Fast path: patterns whose header declares the detected MIME type.
        if (!mimeType.empty()) {
            if (auto it = s_mimeIndex.find(mimeType); it != s_mimeIndex.end()) {
                for (const auto index : it->second) {
                    const auto &meta = s_patternMetas[index];
                    result.emplace_back(meta.path, meta.author, meta.description, mimeType, std::nullopt);
                }
            }
        }

        // Binary magic matching ("#pragma magic [ AA BB CC ] @ 0x..."); only
        // patterns declaring such a pragma are checked, and only against the
        // provider data — no lexer involved.
        for (const auto &meta : s_patternMetas) {
            if (task != nullptr)
                task->update();

            for (const auto &magicString : meta.magics) {
                const auto pattern = [value = magicString]() mutable -> std::optional<BinaryPattern> {
                    value = wolv::util::trim(value);

                    if (value.empty())
                        return std::nullopt;

                    if (!value.starts_with('['))
                        return std::nullopt;

                    value = value.substr(1);

                    const auto end = value.find(']');
                    if (end == std::string::npos)
                        return std::nullopt;
                    value.resize(end);

                    value = wolv::util::trim(value);

                    return BinaryPattern(value);
                }();

                const auto address = [provider, value = magicString]() mutable -> std::optional<u64> {
                    value = wolv::util::trim(value);

                    if (value.empty())
                        return std::nullopt;

                    const auto start = value.find('@');
                    if (start == std::string::npos)
                        return std::nullopt;

                    value = value.substr(start + 1);
                    value = wolv::util::trim(value);

                    size_t end = 0;
                    auto result = std::stoll(value, &end, 0);
                    if (end != value.length())
                        return std::nullopt;

                    if (result < 0) {
                        const auto size = provider->getActualSize();
                        if (u64(-result) > size) {
                            return std::nullopt;
                        }

                        return size + result;
                    } else {
                        return result;
                    }
                }();

                if (address && pattern) {
                    std::vector<u8> bytes(pattern->getSize());
                    if (!bytes.empty()) {
                        provider->read(*address, bytes.data(), bytes.size());

                        if (pattern->matches(bytes)) {
                            result.emplace_back(meta.path, meta.author, meta.description, std::nullopt, address);
                        }
                    }
                }
            }
        }

        return result;
    }

}