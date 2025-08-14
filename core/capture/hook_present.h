#ifndef HOOK_PRESENT_H
#define HOOK_PRESENT_H

#include <d3d9.h>
#include <dxgi.h>
#include <GL/gl.h>

class HookPresent {
public:
    HookPresent();
    ~HookPresent();

    void Initialize();
    void Shutdown();

    // Hooking functions
    static HRESULT WINAPI Present(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
    static HRESULT WINAPI Present1(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static void WINAPI wglSwapBuffers(HDC hdc);

private:
    // Internal state and resources
    bool isHooked;
    // Additional members for managing hooks
};

#endif // HOOK_PRESENT_H