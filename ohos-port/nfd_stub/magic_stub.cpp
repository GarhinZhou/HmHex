// libmagic stub for the OpenHarmony build
// ========================================
// The system/DevEco toolchain has no libmagic. ImHex only uses it to
// identify file types; this stub implements the minimal API surface used
// by libimhex (helpers/magic.cpp) and always reports an unknown type.
// TODO: bundle the real magic database for full file type detection.

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// magic_t is an opaque handle
typedef void *magic_t;

#define MAGIC_NONE       0x000000
#define MAGIC_CHECK      0x100000
#define MAGIC_CONTINUE   0x000020

magic_t magic_open(int) {
    return reinterpret_cast<magic_t>(1);
}

void magic_close(magic_t) {
}

int magic_compile(magic_t, const char *) {
    return -1;
}

int magic_check(magic_t, const char *) {
    return -1;
}

int magic_load(magic_t, const char *) {
    return -1;
}

int magic_setflags(magic_t, int) {
    return 0;
}

const char *magic_error(magic_t) {
    return "libmagic not available on OpenHarmony";
}

const char *magic_file(magic_t, const char *) {
    return nullptr;
}

const char *magic_buffer(magic_t, const void *, size_t) {
    return nullptr;
}

int magic_errno(magic_t) {
    return 0;
}

const char *magic_version(void) {
    return "0 (stub)";
}

#ifdef __cplusplus
}
#endif
