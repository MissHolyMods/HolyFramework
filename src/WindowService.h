#pragma once

namespace HolyFramework
{
    class WindowService final
    {
    public:
        static WindowService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool GetState(HF_WindowStateV1& a_outState) const noexcept;
        [[nodiscard]] bool IsForeground() const noexcept;

        HF_CursorClipHandle AcquireCursorClipOwned(
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool ReleaseCursorClipOwned(
            HF_CursorClipHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        // Called by the shared pre-Present path while cursor-confinement
        // ownership exists or a final release needs to be retried.
        void MaintainOnPresent() noexcept;

    private:
        struct Request final
        {
            HF_CursorClipHandle handle{ HF_INVALID_CURSOR_CLIP_HANDLE };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        using GetForegroundWindow_t = REX::W32::HWND(__stdcall*)();
        using ClientToScreen_t = REX::W32::BOOL(__stdcall*)(REX::W32::HWND, REX::W32::POINT*);
        using ClipCursor_t = REX::W32::BOOL(__stdcall*)(const REX::W32::RECT*);
        using GetClipCursor_t = REX::W32::BOOL(__stdcall*)(REX::W32::RECT*);

        WindowService() noexcept;

        [[nodiscard]] static bool NamesEqualInsensitive(
            std::string_view a_left,
            std::string_view a_right) noexcept;
        [[nodiscard]] static REX::W32::HWND ResolveGameWindow() noexcept;
        [[nodiscard]] static bool RectEqual(
            const REX::W32::RECT& a_left,
            const REX::W32::RECT& a_right) noexcept;
        [[nodiscard]] bool ResolveClientScreenRect(
            REX::W32::HWND a_window,
            REX::W32::RECT& a_outRect) noexcept;

        // Requires _lock. Returns true when HolyFramework no longer owns a
        // cursor clip that still needs release/retry.
        [[nodiscard]] bool ReleaseAppliedClipLocked() noexcept;
        void UpdatePresentDemandLocked() noexcept;

        GetForegroundWindow_t _getForegroundWindow{};
        ClientToScreen_t _clientToScreen{};
        ClipCursor_t _clipCursor{};
        GetClipCursor_t _getClipCursor{};

        mutable std::mutex _lock;
        std::vector<Request> _requests;
        HF_CursorClipHandle _nextHandle{ 1 };
        bool _clipApplied{};
        REX::W32::HWND _clipWindow{};
        REX::W32::RECT _clipRect{};
    };
}
