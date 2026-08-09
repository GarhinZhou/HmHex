// NFD (nativefiledialog) stub for the OpenHarmony port
// =====================================================
// ImHex links against NFD for its file open/save dialogs. On OpenHarmony
// the desktop GTK/portal backends are unavailable, so this stub implements
// the NFD C API and reports "cancel" for every dialog.
//
// TODO: bridge to the OpenHarmony file picker (OH_FilePicker / NAPI picker)
// to enable opening files from the sandbox.

#include <nfd.h>

#include <cstring>

#define NFD_STUB_ERROR "File dialogs are not available on OpenHarmony yet"

namespace {

    const char *g_lastError = nullptr;

    void setError(const char *message) {
        g_lastError = message;
    }

}

extern "C" {

NFD_API nfdresult_t NFD_Init(void) {
    return NFD_OKAY;
}

NFD_API void NFD_Quit(void) {
}

NFD_API const char *NFD_GetError(void) {
    return g_lastError;
}

NFD_API void NFD_ClearError(void) {
    g_lastError = nullptr;
}

NFD_API void NFD_FreePathN(nfdnchar_t *) {
}

NFD_API void NFD_FreePathU8(nfdu8char_t *) {
}

NFD_API nfdresult_t NFD_OpenDialogN(nfdnchar_t **outPath,
                                    const nfdnfilteritem_t *filterList,
                                    nfdfiltersize_t filterCount,
                                    const nfdnchar_t *defaultPath) {
    (void)outPath; (void)filterList; (void)filterCount; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogU8(nfdu8char_t **outPath,
                                     const nfdu8filteritem_t *filterList,
                                     nfdfiltersize_t filterCount,
                                     const nfdu8char_t *defaultPath) {
    (void)outPath; (void)filterList; (void)filterCount; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t **outPaths,
                                            const nfdnfilteritem_t *filterList,
                                            nfdfiltersize_t filterCount,
                                            const nfdnchar_t *defaultPath) {
    (void)outPaths; (void)filterList; (void)filterCount; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t **outPaths,
                                             const nfdu8filteritem_t *filterList,
                                             nfdfiltersize_t filterCount,
                                             const nfdu8char_t *defaultPath) {
    (void)outPaths; (void)filterList; (void)filterCount; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_SaveDialogN(nfdnchar_t **outPath,
                                    const nfdnfilteritem_t *filterList,
                                    nfdfiltersize_t filterCount,
                                    const nfdnchar_t *defaultPath,
                                    const nfdnchar_t *defaultName) {
    (void)outPath; (void)filterList; (void)filterCount; (void)defaultPath; (void)defaultName;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_SaveDialogU8(nfdu8char_t **outPath,
                                     const nfdu8filteritem_t *filterList,
                                     nfdfiltersize_t filterCount,
                                     const nfdu8char_t *defaultPath,
                                     const nfdu8char_t *defaultName) {
    (void)outPath; (void)filterList; (void)filterCount; (void)defaultPath; (void)defaultName;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_PickFolderN(nfdnchar_t **outPath, const nfdnchar_t *defaultPath) {
    (void)outPath; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_PickFolderU8(nfdu8char_t **outPath, const nfdu8char_t *defaultPath) {
    (void)outPath; (void)defaultPath;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

// --- Versioned "With_Impl" entry points (called by nfd.hpp) ---

NFD_API nfdresult_t NFD_OpenDialogN_With_Impl(nfdversion_t version,
                                              nfdnchar_t **outPath,
                                              const nfdopendialognargs_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogU8_With_Impl(nfdversion_t version,
                                               nfdu8char_t **outPath,
                                               const nfdopendialogu8args_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogMultipleN_With_Impl(nfdversion_t version,
                                                      const nfdpathset_t **outPaths,
                                                      const nfdopendialognargs_t *args) {
    (void)version; (void)outPaths; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_OpenDialogMultipleU8_With_Impl(nfdversion_t version,
                                                       const nfdpathset_t **outPaths,
                                                       const nfdopendialogu8args_t *args) {
    (void)version; (void)outPaths; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_SaveDialogN_With_Impl(nfdversion_t version,
                                              nfdnchar_t **outPath,
                                              const nfdsavedialognargs_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_SaveDialogU8_With_Impl(nfdversion_t version,
                                               nfdu8char_t **outPath,
                                               const nfdsavedialogu8args_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_PickFolderN_With_Impl(nfdversion_t version,
                                              nfdnchar_t **outPath,
                                              const nfdpickfoldernargs_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

NFD_API nfdresult_t NFD_PickFolderU8_With_Impl(nfdversion_t version,
                                               nfdu8char_t **outPath,
                                               const nfdpickfolderu8args_t *args) {
    (void)version; (void)outPath; (void)args;
    setError(NFD_STUB_ERROR);
    return NFD_CANCEL;
}

// --- Path set API (multiple selection) ---

NFD_API nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t *pathSet, nfdpathsetsize_t *count) {
    (void)pathSet;
    if (count != nullptr) *count = 0;
    return NFD_OKAY;
}

NFD_API nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t *pathSet,
                                         nfdpathsetsize_t index,
                                         nfdnchar_t **outPath) {
    (void)pathSet; (void)index;
    if (outPath != nullptr) *outPath = nullptr;
    setError(NFD_STUB_ERROR);
    return NFD_ERROR;
}

NFD_API nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t *pathSet,
                                          nfdpathsetsize_t index,
                                          nfdu8char_t **outPath) {
    (void)pathSet; (void)index;
    if (outPath != nullptr) *outPath = nullptr;
    setError(NFD_STUB_ERROR);
    return NFD_ERROR;
}

NFD_API void NFD_PathSet_FreePathN(const nfdnchar_t *) {
}

NFD_API void NFD_PathSet_FreePathU8(const nfdu8char_t *) {
}

NFD_API nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t *pathSet,
                                        nfdpathsetenum_t *outEnumerator) {
    (void)pathSet; (void)outEnumerator;
    setError(NFD_STUB_ERROR);
    return NFD_ERROR;
}

NFD_API void NFD_PathSet_FreeEnum(nfdpathsetenum_t *) {
}

NFD_API nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t *enumerator, nfdnchar_t **outPath) {
    (void)enumerator;
    if (outPath != nullptr) *outPath = nullptr;
    return NFD_ERROR;
}

NFD_API nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t *enumerator, nfdu8char_t **outPath) {
    (void)enumerator;
    if (outPath != nullptr) *outPath = nullptr;
    return NFD_ERROR;
}

NFD_API void NFD_PathSet_Free(const nfdpathset_t *pathSet) {
    (void)pathSet;
}

}
