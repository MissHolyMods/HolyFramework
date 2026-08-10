#include "pch.h"
#include "FrameworkUI.h"

namespace HolyFramework
{
    bool QueueNotification(std::string a_message, const bool a_warning) noexcept
    {
        const auto tasks = F4SE::GetTaskInterface();
        if (!tasks || a_message.empty()) {
            return false;
        }

        try {
            tasks->AddUITask([message = std::move(a_message), a_warning]() {
                RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, false, a_warning);
            });
            return true;
        } catch (...) {
            return false;
        }
    }
}
