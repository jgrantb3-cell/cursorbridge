#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <thread>

namespace
{
    std::atomic_bool started{ false };
    std::atomic_bool cursorMenuActive{ false };

    using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
    using SetCursorPos_t = BOOL(WINAPI*)(int, int);
    using ShowCursor_t = int(WINAPI*)(BOOL);
    using SetCursor_t = HCURSOR(WINAPI*)(HCURSOR);

    ClipCursor_t originalClipCursor = nullptr;
    SetCursorPos_t originalSetCursorPos = nullptr;
    ShowCursor_t originalShowCursor = nullptr;
    SetCursor_t originalSetCursor = nullptr;
    HCURSOR arrowCursor = nullptr;

    HWND GetSkyrimWindow()
    {
        const HWND foreground = ::GetForegroundWindow();
        if (!foreground) {
            return nullptr;
        }

        DWORD foregroundProcess = 0;
        ::GetWindowThreadProcessId(foreground, &foregroundProcess);
        return foregroundProcess == ::GetCurrentProcessId() ? foreground : nullptr;
    }

    bool IsCursorMenuOpen()
    {
        const auto ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        for (const auto& menu : ui->menuStack) {
            if (menu && menu->OnStack() && menu->UsesCursor()) {
                return true;
            }
        }
        return false;
    }

    void RefreshCursorMenuState()
    {
        const bool active = IsCursorMenuOpen();
        const bool previous = cursorMenuActive.exchange(active);
        if (active != previous) {
            SKSE::log::info("Cursor menu {}", active ? "opened" : "closed");

            // ShowCursor's display counter belongs to the window-owning UI thread.
            // RefreshCursorMenuState runs through SKSE's main-thread task queue, so
            // visibility must be changed here rather than in CursorWorker.
            if (originalShowCursor) {
                int visibilityCount = 0;
                if (active) {
                    do {
                        visibilityCount = originalShowCursor(TRUE);
                    } while (visibilityCount < 0);

                    if (originalSetCursor && arrowCursor) {
                        originalSetCursor(arrowCursor);
                    }
                } else {
                    do {
                        visibilityCount = originalShowCursor(FALSE);
                    } while (visibilityCount >= 0);
                }
                SKSE::log::info(
                    "UI-thread Windows cursor visibility count: {}",
                    visibilityCount);
            }
        }
    }

    class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent*,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            // MenuOpenCloseEvent is sent while the menu stack is changing. Queue the
            // scan so the final top-level cursor state is observed on the game thread.
            if (const auto tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(RefreshCursorMenuState);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        static MenuEventSink* GetSingleton()
        {
            static MenuEventSink singleton;
            return std::addressof(singleton);
        }
    };

    BOOL WINAPI HookClipCursor(const RECT* rect)
    {
        if (cursorMenuActive.load() && GetSkyrimWindow()) {
            return originalClipCursor(nullptr);
        }
        return originalClipCursor(rect);
    }

    BOOL WINAPI HookSetCursorPos(int x, int y)
    {
        if (cursorMenuActive.load() && GetSkyrimWindow()) {
            return TRUE;
        }
        return originalSetCursorPos(x, y);
    }

    int WINAPI HookShowCursor(BOOL show)
    {
        if (cursorMenuActive.load()) {
            // CursorWorker owns the Windows visibility count while a cursor-driven
            // Skyrim menu is open.
            return 0;
        }
        return originalShowCursor(show);
    }

    HCURSOR WINAPI HookSetCursor(HCURSOR cursor)
    {
        if (cursorMenuActive.load()) {
            // Skyrim normally installs a transparent/null system cursor and draws
            // its own bounded Scaleform pointer. Keep the real desktop pointer
            // visible so it can cross onto another monitor.
            return originalSetCursor(arrowCursor ? arrowCursor : cursor);
        }
        return originalSetCursor(cursor);
    }

    bool CreateApiHook(
        const char* name,
        LPVOID hook,
        LPVOID* original)
    {
        const MH_STATUS status =
            ::MH_CreateHookApi(L"user32.dll", name, hook, original);
        if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
            SKSE::log::error("{} hook creation failed: {}", name, static_cast<int>(status));
            return false;
        }
        return true;
    }

    bool InstallCursorHooks()
    {
        const MH_STATUS initializeStatus = ::MH_Initialize();
        if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error("MinHook initialization failed: {}", static_cast<int>(initializeStatus));
            return false;
        }

        const bool clipCreated = CreateApiHook(
            "ClipCursor",
            reinterpret_cast<LPVOID>(&HookClipCursor),
            reinterpret_cast<LPVOID*>(&originalClipCursor));
        const bool positionCreated = CreateApiHook(
            "SetCursorPos",
            reinterpret_cast<LPVOID>(&HookSetCursorPos),
            reinterpret_cast<LPVOID*>(&originalSetCursorPos));
        const bool visibilityCreated = CreateApiHook(
            "ShowCursor",
            reinterpret_cast<LPVOID>(&HookShowCursor),
            reinterpret_cast<LPVOID*>(&originalShowCursor));
        const bool cursorImageCreated = CreateApiHook(
            "SetCursor",
            reinterpret_cast<LPVOID>(&HookSetCursor),
            reinterpret_cast<LPVOID*>(&originalSetCursor));

        if (!clipCreated || !positionCreated || !visibilityCreated || !cursorImageCreated) {
            return false;
        }

        const MH_STATUS enableStatus = ::MH_EnableHook(MH_ALL_HOOKS);
        if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
            SKSE::log::error("Cursor hook activation failed: {}", static_cast<int>(enableStatus));
            return false;
        }

        SKSE::log::info(
            "Cursor hooks active (ClipCursor={}, SetCursorPos={}, ShowCursor={}, SetCursor={})",
            clipCreated,
            positionCreated,
            visibilityCreated,
            cursorImageCreated);
        return true;
    }

    void PositionWindowsCursorFromMenu(HWND window)
    {
        const auto cursor = RE::MenuCursor::GetSingleton();
        if (!cursor || !originalSetCursorPos) {
            return;
        }

        const auto& data = cursor->GetRuntimeData();
        RECT client{};
        if (!::GetClientRect(window, &client) ||
            data.screenWidthX <= 0.0F ||
            data.screenWidthY <= 0.0F) {
            return;
        }

        POINT origin{ client.left, client.top };
        ::ClientToScreen(window, &origin);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int x = origin.x + static_cast<int>(data.cursorPosX * width / data.screenWidthX);
        const int y = origin.y + static_cast<int>(data.cursorPosY * height / data.screenWidthY);
        originalSetCursorPos(x, y);
    }

    void SynchronizeMenuCursor(HWND window)
    {
        const auto cursor = RE::MenuCursor::GetSingleton();
        if (!cursor) {
            return;
        }

        RECT client{};
        POINT point{};
        if (!::GetClientRect(window, &client) || !::GetCursorPos(&point)) {
            return;
        }

        POINT origin{ client.left, client.top };
        ::ClientToScreen(window, &origin);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int clientX = point.x - origin.x;
        const int clientY = point.y - origin.y;

        if (width <= 0 || height <= 0 ||
            clientX < 0 || clientY < 0 || clientX >= width || clientY >= height) {
            return;
        }

        auto& data = cursor->GetRuntimeData();
        data.cursorPosX = static_cast<float>(clientX) * data.screenWidthX /
                          static_cast<float>(width);
        data.cursorPosY = static_cast<float>(clientY) * data.screenWidthY /
                          static_cast<float>(height);
    }

    void ForceWindowsCursorVisible()
    {
        if (!originalShowCursor) {
            return;
        }
        while (originalShowCursor(TRUE) < 0) {
        }
    }

    void ForceWindowsCursorHidden()
    {
        if (!originalShowCursor) {
            return;
        }
        while (originalShowCursor(FALSE) >= 0) {
        }
    }

    void CursorWorker()
    {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        SKSE::log::info("Menu-aware dual cursor worker started");

        bool wasActive = false;
        for (;;) {
            const bool active = cursorMenuActive.load();
            const HWND window = GetSkyrimWindow();

            if (active && window) {
                if (!wasActive) {
                    PositionWindowsCursorFromMenu(window);
                    SKSE::log::info("Windows cursor position bridged to Skyrim menu");
                }

                if (originalClipCursor) {
                    originalClipCursor(nullptr);
                }
                SynchronizeMenuCursor(window);
            } else if (!active && wasActive) {
                SKSE::log::info("Windows cursor returned to gameplay mode");
            }

            wasActive = active;
            ::Sleep(active ? 1 : 8);
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (message && message->type == SKSE::MessagingInterface::kDataLoaded) {
            if (const auto ui = RE::UI::GetSingleton()) {
                ui->AddEventSink(MenuEventSink::GetSingleton());
                RefreshCursorMenuState();
                SKSE::log::info("Menu event tracking active");
            }
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SKSE::log::info("CursorBridge 1.6.0 loading (Skyrim 1.7.104 build)");

    arrowCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    SKSE::log::info("Windows arrow cursor loaded: {}", arrowCursor != nullptr);

    const bool hooksInstalled = InstallCursorHooks();
    SKSE::log::info("Menu cursor interception: {}", hooksInstalled ? "enabled" : "unavailable");

    if (const auto messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener(OnSKSEMessage);
    }

    if (!started.exchange(true)) {
        std::thread(CursorWorker).detach();
    }
    return true;
}
