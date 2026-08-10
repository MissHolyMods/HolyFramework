#include "pch.h"
#include "WindowService.h"

#include "GraphicsService.h"
#include "PresentationService.h"

namespace HolyFramework
{
    WindowService::WindowService() noexcept
    {
        const auto user32 = REX::W32::GetModuleHandleW(L"user32.dll");
        if (!user32) {
            return;
        }
        _getForegroundWindow = reinterpret_cast<GetForegroundWindow_t>(
            REX::W32::GetProcAddress(user32, "GetForegroundWindow"));
        _clientToScreen = reinterpret_cast<ClientToScreen_t>(
            REX::W32::GetProcAddress(user32, "ClientToScreen"));
        _clipCursor = reinterpret_cast<ClipCursor_t>(
            REX::W32::GetProcAddress(user32, "ClipCursor"));
        _getClipCursor = reinterpret_cast<GetClipCursor_t>(
            REX::W32::GetProcAddress(user32, "GetClipCursor"));
    }

    WindowService& WindowService::GetSingleton() noexcept
    {
        static WindowService* instance = new WindowService();
        return *instance;
    }

    bool WindowService::NamesEqualInsensitive(
        const std::string_view a_left,
        const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) !=
                std::tolower(static_cast<unsigned char>(a_right[i]))) {
                return false;
            }
        }
        return true;
    }

    REX::W32::HWND WindowService::ResolveGameWindow() noexcept
    {
        HF_GraphicsNativeHandlesV1 handles{};
        if (!GraphicsService::GetSingleton().GetNativeHandles(handles) || handles.windowHandle == 0) {
            return nullptr;
        }
        return reinterpret_cast<REX::W32::HWND>(static_cast<std::uintptr_t>(handles.windowHandle));
    }

    bool WindowService::RectEqual(
        const REX::W32::RECT& a_left,
        const REX::W32::RECT& a_right) noexcept
    {
        return a_left.x1 == a_right.x1 &&
               a_left.y1 == a_right.y1 &&
               a_left.x2 == a_right.x2 &&
               a_left.y2 == a_right.y2;
    }

    bool WindowService::ResolveClientScreenRect(
        const REX::W32::HWND a_window,
        REX::W32::RECT& a_outRect) noexcept
    {
        if (!a_window) {
            return false;
        }

        REX::W32::RECT client{};
        if (!REX::W32::GetClientRect(a_window, std::addressof(client))) {
            return false;
        }

        REX::W32::POINT topLeft{ client.x1, client.y1 };
        REX::W32::POINT bottomRight{ client.x2, client.y2 };
        if (!_clientToScreen ||
            !_clientToScreen(a_window, std::addressof(topLeft)) ||
            !_clientToScreen(a_window, std::addressof(bottomRight))) {
            return false;
        }
        if (bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) {
            return false;
        }

        a_outRect = REX::W32::RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        return true;
    }

    bool WindowService::IsAvailable() const noexcept
    {
        return _getForegroundWindow != nullptr;
    }

    bool WindowService::IsForeground() const noexcept
    {
        const auto window = ResolveGameWindow();
        return _getForegroundWindow && window && _getForegroundWindow() == window;
    }

    bool WindowService::GetState(HF_WindowStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_WindowStateV1);

        const auto window = ResolveGameWindow();
        const auto foreground = _getForegroundWindow ? _getForegroundWindow() : nullptr;
        a_outState.foregroundWindowHandle =
            static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(foreground));

        if (_getForegroundWindow) {
            a_outState.flags |= HF_WINDOW_STATE_AVAILABLE;
        }
        if (window) {
            a_outState.flags |= HF_WINDOW_STATE_WINDOW_AVAILABLE;
            if (_clipCursor && _getClipCursor && _clientToScreen) {
                a_outState.flags |= HF_WINDOW_STATE_CURSOR_CLIP_AVAILABLE;
            }
            a_outState.windowHandle =
                static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(window));
            if (foreground == window) {
                a_outState.flags |= HF_WINDOW_STATE_FOREGROUND;
            }
        }

        {
            std::scoped_lock lock{ _lock };
            a_outState.cursorClipRequestCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(_requests.size(), std::numeric_limits<std::uint32_t>::max()));
            if (_clipApplied) {
                a_outState.flags |= HF_WINDOW_STATE_CURSOR_CLIPPED;
            }
        }
        return (a_outState.flags & HF_WINDOW_STATE_AVAILABLE) != 0;
    }

    void WindowService::UpdatePresentDemandLocked() noexcept
    {
        PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason::WindowCursor,
            !_requests.empty() || _clipApplied);
    }

    HF_CursorClipHandle WindowService::AcquireCursorClipOwned(
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || !_clipCursor || !_getClipCursor || !_clientToScreen) {
            return HF_INVALID_CURSOR_CLIP_HANDLE;
        }

        std::scoped_lock lock{ _lock };
        auto handle = _nextHandle++;
        if (handle == HF_INVALID_CURSOR_CLIP_HANDLE) {
            handle = _nextHandle++;
        }
        if (handle == HF_INVALID_CURSOR_CLIP_HANDLE) {
            return HF_INVALID_CURSOR_CLIP_HANDLE;
        }

        try {
            _requests.push_back(Request{
                .handle = handle,
                .moduleName = std::string{ a_moduleName },
                .logger = a_logger
            });
        } catch (...) {
            return HF_INVALID_CURSOR_CLIP_HANDLE;
        }
        UpdatePresentDemandLocked();
        return handle;
    }

    bool WindowService::ReleaseAppliedClipLocked() noexcept
    {
        if (!_clipApplied) {
            return true;
        }

        REX::W32::RECT current{};
        if (_getClipCursor && _getClipCursor(std::addressof(current)) && !RectEqual(current, _clipRect)) {
            // Another component replaced HolyFramework's clip. Do not clear it.
            _clipApplied = false;
            _clipWindow = nullptr;
            _clipRect = {};
            return true;
        }

        if (!_clipCursor || !_clipCursor(nullptr)) {
            return false;
        }

        _clipApplied = false;
        _clipWindow = nullptr;
        _clipRect = {};
        return true;
    }

    bool WindowService::ReleaseCursorClipOwned(
        const HF_CursorClipHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) {
            a_outActualOwner->clear();
        }
        if (a_handle == HF_INVALID_CURSOR_CLIP_HANDLE || a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_requests, [a_handle](const Request& request) {
            return request.handle == a_handle;
        });
        if (it == _requests.end()) {
            return false;
        }
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->moduleName;
            }
            return false;
        }

        _requests.erase(it);
        if (_requests.empty()) {
            (void)ReleaseAppliedClipLocked();
        }
        UpdatePresentDemandLocked();
        return true;
    }

    std::uint32_t WindowService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::scoped_lock lock{ _lock };
        const auto before = _requests.size();
        std::erase_if(_requests, [&](const Request& request) {
            return NamesEqualInsensitive(request.moduleName, a_moduleName);
        });
        const auto removed = before - _requests.size();
        if (_requests.empty()) {
            (void)ReleaseAppliedClipLocked();
        }
        UpdatePresentDemandLocked();
        return static_cast<std::uint32_t>(
            std::min<std::size_t>(removed, std::numeric_limits<std::uint32_t>::max()));
    }

    void WindowService::MaintainOnPresent() noexcept
    {
        std::scoped_lock lock{ _lock };
        if (_requests.empty()) {
            (void)ReleaseAppliedClipLocked();
            UpdatePresentDemandLocked();
            return;
        }

        const auto window = ResolveGameWindow();
        if (!_getForegroundWindow || !window || _getForegroundWindow() != window) {
            (void)ReleaseAppliedClipLocked();
            return;
        }

        REX::W32::RECT target{};
        if (!ResolveClientScreenRect(window, target)) {
            (void)ReleaseAppliedClipLocked();
            return;
        }

        if (_clipApplied && _clipWindow == window && RectEqual(_clipRect, target)) {
            REX::W32::RECT current{};
            if (_getClipCursor && _getClipCursor(std::addressof(current)) && RectEqual(current, target)) {
                return;
            }
        }

        if (_clipCursor && _clipCursor(std::addressof(target))) {
            _clipApplied = true;
            _clipWindow = window;
            _clipRect = target;
        } else {
            _clipApplied = false;
            _clipWindow = nullptr;
            _clipRect = {};
        }
    }
}
