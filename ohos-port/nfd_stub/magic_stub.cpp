// libmagic stub for the OpenHarmony build
// ========================================
// The system/DevEco toolchain has no libmagic. ImHex uses it to identify
// file types and MIME types (helpers/magic.cpp), which drives the "viable
// pattern" recommendation (data information section). This stub implements
// the minimal API surface used by libimhex plus a lightweight magic-number
// sniffer for common formats, so opening e.g. an ELF file recommends the
// elf.hexpat pattern like on desktop ImHex.
//
// Only magic_buffer's return value depends on the data; the load/compile
// calls are no-ops because the magic database is not bundled.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef __cplusplus
extern "C" {
#endif

// magic_t is an opaque handle
typedef void *magic_t;

#define MAGIC_NONE       0x000000
#define MAGIC_DEBUG      0x000001
#define MAGIC_SYMLINK    0x000002
#define MAGIC_COMPRESS   0x000004
#define MAGIC_DEVICES    0x000008
#define MAGIC_MIME_TYPE  0x000010
#define MAGIC_CONTINUE   0x000020
#define MAGIC_CHECK      0x100000
#define MAGIC_APPLE      0x000004
#define MAGIC_EXTENSION  0x1000000

typedef struct {
    int flags;
} MagicContext;

magic_t magic_open(int flags) {
    auto *ctx = static_cast<MagicContext *>(std::malloc(sizeof(MagicContext)));
    if (ctx == nullptr)
        return nullptr;
    ctx->flags = flags;
    return reinterpret_cast<magic_t>(ctx);
}

void magic_close(magic_t cookie) {
    std::free(cookie);
}

int magic_compile(magic_t, const char *) {
    return 0;
}

int magic_check(magic_t, const char *) {
    return 0;
}

int magic_load(magic_t, const char *) {
    return 0;
}

int magic_setflags(magic_t cookie, int flags) {
    auto *ctx = static_cast<MagicContext *>(cookie);
    if (ctx == nullptr)
        return -1;
    ctx->flags = flags;
    return 0;
}

const char *magic_error(magic_t) {
    return "libmagic not available on OpenHarmony";
}

const char *magic_file(magic_t, const char *) {
    return nullptr;
}

int magic_errno(magic_t) {
    return 0;
}

// ---- Lightweight magic-number sniffer -------------------------------------

namespace {

    struct MagicEntry {
        const char *mime;
        const char *description;
        bool (*match)(const unsigned char *data, size_t size);
    };

    static bool matchELF(const unsigned char *d, size_t n) {
        return n >= 4 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F';
    }
    static bool matchPE(const unsigned char *d, size_t n) {
        return n >= 2 && d[0] == 'M' && d[1] == 'Z';
    }
    static bool matchMachO(const unsigned char *d, size_t n) {
        static const unsigned char magics[][4] = {
            { 0xFE, 0xED, 0xFA, 0xCE }, { 0xFE, 0xED, 0xFA, 0xCF },
            { 0xCE, 0xFA, 0xED, 0xFE }, { 0xCF, 0xFA, 0xED, 0xFE },
        };
        if (n < 4) return false;
        for (auto &m : magics)
            if (std::memcmp(d, m, 4) == 0) return true;
        return false;
    }
    static bool matchPNG(const unsigned char *d, size_t n) {
        static const unsigned char magic[] = { 0x89, 'P', 'N', 'G' };
        return n >= 8 && std::memcmp(d, magic, 4) == 0;
    }
    static bool matchJPEG(const unsigned char *d, size_t n) {
        return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
    }
    static bool matchGIF(const unsigned char *d, size_t n) {
        return n >= 6 && std::memcmp(d, "GIF8", 4) == 0;
    }
    static bool matchBMP(const unsigned char *d, size_t n) {
        return n >= 2 && d[0] == 'B' && d[1] == 'M';
    }
    static bool matchZIP(const unsigned char *d, size_t n) {
        return n >= 4 && d[0] == 'P' && d[1] == 'K' && (d[2] == 3 || d[2] == 5 || d[2] == 7) && d[3] == 4;
    }
    static bool matchGZIP(const unsigned char *d, size_t n) {
        return n >= 2 && d[0] == 0x1F && d[1] == 0x8B;
    }
    static bool matchPDF(const unsigned char *d, size_t n) {
        return n >= 5 && std::memcmp(d, "%PDF-", 5) == 0;
    }
    static bool match7z(const unsigned char *d, size_t n) {
        static const unsigned char magic[] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
        return n >= 6 && std::memcmp(d, magic, 6) == 0;
    }
    static bool matchBZIP2(const unsigned char *d, size_t n) {
        return n >= 3 && d[0] == 'B' && d[1] == 'Z' && d[2] == 'h';
    }
    static bool matchXZ(const unsigned char *d, size_t n) {
        static const unsigned char magic[] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
        return n >= 6 && std::memcmp(d, magic, 6) == 0;
    }
    static bool matchTAR(const unsigned char *d, size_t n) {
        return n >= 262 && std::memcmp(d + 257, "ustar", 5) == 0;
    }
    static bool matchRIFF(const unsigned char *d, size_t n) {
        if (n < 12 || std::memcmp(d, "RIFF", 4) != 0) return false;
        return std::memcmp(d + 8, "WAVE", 4) == 0 || std::memcmp(d + 8, "AVI ", 4) == 0;
    }
    static bool matchMP3(const unsigned char *d, size_t n) {
        return (n >= 3 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') ||
               (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0);
    }
    static bool matchSQLite(const unsigned char *d, size_t n) {
        return n >= 16 && std::memcmp(d, "SQLite format 3\x00", 16) == 0;
    }
    static bool matchWASM(const unsigned char *d, size_t n) {
        return n >= 4 && d[0] == 0x00 && d[1] == 'a' && d[2] == 's' && d[3] == 'm';
    }

    // ELF sub-type (e_type at offset 16, little-endian) -> exact MIME like
    // libmagic, so the elf.hexpat pragma list matches.
    static const char *elfMIME(const unsigned char *d, size_t n) {
        if (n < 18)
            return "application/x-elf";
        const auto eType = d[16] | (d[17] << 8);
        switch (eType) {
            case 1:  return "application/x-object";
            case 2:  return "application/x-executable";
            case 3:  return "application/x-sharedlib";
            case 4:  return "application/x-coredump";
            default: return "application/x-elf";
        }
    }

    static const char *elfDescription(const unsigned char *d, size_t n) {
        const bool is64 = n >= 5 && d[4] == 2;
        const bool le    = n >= 6 && d[5] == 1;
        const auto eType = n >= 18 ? (d[16] | (d[17] << 8)) : 0;
        const char *kind = "relocatable";
        if (eType == 2) kind = "executable";
        else if (eType == 3) kind = "shared object";
        else if (eType == 4) kind = "core file";
        static char buf[64];
        std::snprintf(buf, sizeof(buf), "ELF %d-bit LSB %s", is64 ? 64 : 32, kind);
        (void)le;
        return buf;
    }

    static const MagicEntry *entries() {
        static const MagicEntry table[] = {
            { "application/x-elf",        nullptr, matchELF },
            { "application/x-dosexec",    "PE32 executable (MS-DOS)", matchPE },
            { "application/x-mach-binary", "Mach-O binary", matchMachO },
            { "image/png",                "PNG image data", matchPNG },
            { "image/jpeg",               "JPEG image data", matchJPEG },
            { "image/gif",                "GIF image data", matchGIF },
            { "image/bmp",                "BMP image data", matchBMP },
            { "application/zip",          "Zip archive data", matchZIP },
            { "application/gzip",         "gzip compressed data", matchGZIP },
            { "application/pdf",          "PDF document", matchPDF },
            { "application/x-7z-compressed", "7-zip archive data", match7z },
            { "application/x-bzip2",      "bzip2 compressed data", matchBZIP2 },
            { "application/x-xz",         "XZ compressed data", matchXZ },
            { "application/x-tar",        "POSIX tar archive", matchTAR },
            { "audio/x-wav",              "RIFF (WAV) audio", matchRIFF },
            { "audio/mpeg",               "MPEG audio", matchMP3 },
            { "application/vnd.sqlite3",  "SQLite 3.x database", matchSQLite },
            { "application/wasm",         "WebAssembly binary", matchWASM },
            { nullptr,                    nullptr, nullptr },
        };
        return table;
    }
}

const char *magic_buffer(magic_t cookie, const void *buffer, size_t length) {
    auto *ctx = static_cast<MagicContext *>(cookie);
    if (ctx == nullptr || buffer == nullptr || length == 0)
        return nullptr;

    const auto *data = static_cast<const unsigned char *>(buffer);
    const bool wantMIME = (ctx->flags & MAGIC_MIME_TYPE) != 0;

    const auto *table = entries();
    for (size_t i = 0; table[i].match != nullptr; i++) {
        const auto &entry = table[i];
        if (!entry.match(data, length))
            continue;

        if (wantMIME) {
            if (entry.mime == nullptr)
                continue;
            if (entry.match == matchELF)
                return elfMIME(data, length);
            return entry.mime;
        } else {
            if (entry.match == matchELF)
                return elfDescription(data, length);
            return entry.description != nullptr ? entry.description : entry.mime;
        }
    }

    return nullptr;
}

#ifdef __cplusplus
}
#endif
