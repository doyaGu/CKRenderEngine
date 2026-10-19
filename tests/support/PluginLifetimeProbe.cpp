#include <Windows.h>

typedef void (__cdecl *FillInfo)(void *);
static FillInfo Fill = nullptr;
static int *DetachCount = nullptr;

extern "C" void __cdecl CKREConfigureLifetimeProbe(FillInfo fill, int *detachCount) {
    Fill = fill;
    DetachCount = detachCount;
}

extern "C" void __cdecl CKRasterizerGetInfo(void *info) {
    // The host supplies the real, unchanged NULL provider's factories.
    // This DLL observes ownership; it does not implement a rasterizer.
    Fill(info);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH && DetachCount)
        ++*DetachCount;
    return TRUE;
}
