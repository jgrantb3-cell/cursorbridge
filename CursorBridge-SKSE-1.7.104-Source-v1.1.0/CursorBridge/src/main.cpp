#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <thread>

namespace
{
    std::atomic_bool started{ false };

    using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
    using SetCursorPos_t = BOOL(WINAPI*)(int, int);

    ClipCursor_t originalClipCursor = nullptr;
    SetCursorPos_t originalSetCursorPos = nullptr;

    bool IsSkyrimForeground()
    {
        const HWND foreground = ::GetForegroundWindow();
        if (!foreground) {
            return false;
        }

        DWORD foregroundProcess = 0;
        ::GetWindowThreadProcessId(foreground, &foregroundProcess);
        return foregroundProcess == ::GetCurrentProcessId();
    }

    BOOL WINAPI HookClipCursor(const RECT* rect)
    {
        if (IsSkyrimForeground()) {
            return originalClipCursor(nullptr);
        }
        return originalClipCursor(rect);
    }

    BOOL WINAPI HookSetCursorPos(int x, int y)
    {
        if (IsSkyrimForeground()) {
            // Skyrim recenters the system cursor while using relative mouse input.
            // Reporting success without moving it prevents the pointer being pulled
            // back into the game window.
            return TRUE;
        }
        return originalSetCursorPos(x, y);
    }

    bool InstallCursorHooks()
    {
        const MH_STATUS initializeStatus = ::MH_Initialize();
        if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error("MinHook initialization failed: {}", static_cast<int>(initializeStatus));
            return false;
        }

        const MH_STATUS clipStatus = ::MH_CreateHookApi(
            L"user32.dll",
            "ClipCursor",
            reinterpret_cast<LPVOID>(&HookClipCursor),
            reinterpret_cast<LPVOID*>(&originalClipCursor));

        const MH_STATUS positionStatus = ::MH_CreateHookApi(
            L"user32.dll",
            "SetCursorPos",
            reinterpret_cast<LPVOID>(&HookSetCursorPos),
            reinterpret_cast<LPVOID*>(&originalSetCursorPos));

        const bool clipCreated = clipStatus == MH_OK || clipStatus == MH_ERROR_ALREADY_CREATED;
        const bool positionCreated =
            positionStatus == MH_OK || positionStatus == MH_ERROR_ALREADY_CREATED;

        if (!clipCreated) {
            SKSE::log::error("ClipCursor hook creation failed: {}", static_cast<int>(clipStatus));
        }
        if (!positionCreated) {
            SKSE::log::error("SetCursorPos hook creation failed: {}", static_cast<int>(positionStatus));
        }
        if (!clipCreated && !positionCreated) {
            return false;
        }

        const MH_STATUS enableStatus = ::MH_EnableHook(MH_ALL_HOOKS);
        if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
            SKSE::log::error("Cursor hook activation failed: {}", static_cast<int>(enableStatus));
            return false;
        }

        SKSE::log::info(
            "Cursor hooks active (ClipCursor={}, SetCursorPos={})",
            clipCreated,
            positionCreated);
        return true;
    }

    void CursorWorker()
    {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        SKSE::log::info("Cursor release fallback worker started");

        for (;;) {
            if (IsSkyrimForeground()) {
                // Call the unhooked function when available so the fallback cannot
                // recurse through HookClipCursor.
                if (originalClipCursor) {
                    originalClipCursor(nullptr);
                } else {
                    ::ClipCursor(nullptr);
                }
            }
            ::Sleep(1);
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SKSE::log::info("CursorBridge 1.3.0 loading (Skyrim 1.7.104 build)");

    const bool hooksInstalled = InstallCursorHooks();
    SKSE::log::info("Direct cursor interception: {}", hooksInstalled ? "enabled" : "unavailable");

    if (!started.exchange(true)) {
        std::thread(CursorWorker).detach();
    }
    return true;
}
