#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

namespace
{
    using namespace std::chrono_literals;

    std::atomic_bool started{ false };
    std::atomic_bool releaseCursor{ false };

    // Includes paused and unpaused interfaces which intentionally expose a cursor.
    constexpr std::array<std::string_view, 16> cursorMenus{
        "BarterMenu", "Book Menu", "Console", "ContainerMenu",
        "Crafting Menu", "Credits Menu", "Dialogue Menu", "FavoritesMenu",
        "GiftMenu", "InventoryMenu", "Journal Menu", "LevelUp Menu",
        "Lockpicking Menu", "MagicMenu", "MapMenu", "TweenMenu"
    };

    bool IsCursorMenu(const RE::BSFixedString& menuName)
    {
        for (const auto name : cursorMenus) {
            if (std::string_view{ menuName.c_str() } == name) {
                return true;
            }
        }
        return false;
    }

    class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MenuSink* GetSingleton()
        {
            static MenuSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!event || !IsCursorMenu(event->menuName)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const std::string menuName{ event->menuName.c_str() };
            if (event->opening) {
                openMenus.insert(menuName);
            } else {
                openMenus.erase(menuName);
            }
            releaseCursor.store(!openMenus.empty(), std::memory_order_release);
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        std::unordered_set<std::string> openMenus;
    };

    void CursorWorker()
    {
        // Skyrim and other display plugins can reapply ClipCursor, so release it
        // repeatedly while a cursor-driven menu is present.
        for (;;) {
            if (releaseCursor.load(std::memory_order_acquire)) {
                ::ClipCursor(nullptr);
            }
            std::this_thread::sleep_for(8ms);
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        if (message->type == SKSE::MessagingInterface::kDataLoaded && !started.exchange(true)) {
            RE::UI::GetSingleton()->AddEventSink(MenuSink::GetSingleton());
            std::thread(CursorWorker).detach();
            SKSE::log::info("Cursor release worker started");
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SKSE::log::info("CursorBridge 1.1.0 loading (Skyrim 1.7.104 build)");

    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        SKSE::log::critical("Could not register SKSE messaging listener");
        return false;
    }
    return true;
}
