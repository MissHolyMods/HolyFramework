#pragma once

namespace HolyFramework
{
    inline std::recursive_mutex& MutationMutex() noexcept
    {
        static std::recursive_mutex mutex;
        return mutex;
    }
}
