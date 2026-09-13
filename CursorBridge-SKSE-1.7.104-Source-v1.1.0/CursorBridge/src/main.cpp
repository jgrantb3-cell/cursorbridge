#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <Windows.h>

#include <atomic>
#include <thread>

namespace
{
    std::atomic_bool started{ false };

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

    void CursorWorker()
    {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        SKSE::log::info("Continuous cursor release worker started");

        for (;;) {
            if (IsSkyrimForeground()) {
                // Skyrim and display plugins can restore the clip every frame.
                // Releasing it continuously keeps the pointer free to cross monitors.
                ::ClipCursor(nullptr);
            }
            ::Sleep(1);
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SKSE::log::info("CursorBridge 1.2.0 loading (Skyrim 1.7.104 build)");

    if (!started.exchange(true)) {
        std::thread(CursorWorker).detach();
    }
    return true;
}
