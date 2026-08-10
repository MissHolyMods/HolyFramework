#include "pch.h"
#include "TaskScheduler.h"

#include "Diagnostics.h"
#include "ModuleContext.h"
#include "ModuleLoader.h"
#include "PerformanceMonitor.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    TaskScheduler& TaskScheduler::GetSingleton() noexcept
    {
        // Intentionally process-lifetime. Fallout/F4SE owns the plugin for the
        // lifetime of the process; avoiding static thread teardown also avoids
        // joining a scheduler thread during DLL/process destruction.
        static TaskScheduler* instance = new TaskScheduler();
        return *instance;
    }

    TaskScheduler::TaskScheduler()
    {
        std::thread([this]() {
            WorkerLoop();
        }).detach();
    }

    bool TaskScheduler::IsValidQueue(const HF_TaskQueue a_queue) noexcept
    {
        return a_queue == HF_TASK_QUEUE_GAME || a_queue == HF_TASK_QUEUE_UI;
    }

    bool TaskScheduler::ResolveOwner(
        const HF_TaskCallback a_callback,
        std::string& a_moduleName,
        HF_LogHandle& a_logger,
        std::uint32_t& a_checkpoint) const noexcept
    {
        const auto context = ModuleContext::Current();
        if (context.name && *context.name) {
            a_moduleName = context.name;
            a_logger = context.logger;
            a_checkpoint = context.checkpoint;
            return true;
        }

        // A module may schedule from one of its own worker threads, where the
        // HolyFramework TLS execution context is naturally absent. The callback
        // address still belongs to the module image, so recover ownership from it.
        if (ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                reinterpret_cast<const void*>(a_callback),
                a_moduleName,
                a_logger)) {
            a_checkpoint = 0;
            return true;
        }

        return false;
    }

    HF_TaskHandle TaskScheduler::Queue(
        const HF_TaskQueue a_queue,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags)
    {
        return QueueDelayed(a_queue, 0, a_callback, a_userData, a_flags);
    }

    HF_TaskHandle TaskScheduler::QueueDelayed(
        const HF_TaskQueue a_queue,
        const std::uint32_t a_delayMs,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags)
    {
        if (!IsValidQueue(a_queue) || !a_callback) {
            return HF_INVALID_TASK_HANDLE;
        }

        std::string moduleName;
        HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
        std::uint32_t checkpoint = 0;
        if (!ResolveOwner(a_callback, moduleName, logger, checkpoint)) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_TASK_OWNER_UNKNOWN);
            return HF_INVALID_TASK_HANDLE;
        }

        return QueueOwned(
            a_queue,
            a_delayMs,
            a_callback,
            a_userData,
            a_flags,
            std::move(moduleName),
            logger,
            checkpoint);
    }

    HF_TaskHandle TaskScheduler::QueueFrameworkDelayed(
        const HF_TaskQueue a_queue,
        const std::uint32_t a_delayMs,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags)
    {
        if (!IsValidQueue(a_queue) || !a_callback) {
            return HF_INVALID_TASK_HANDLE;
        }

        return QueueOwned(
            a_queue,
            a_delayMs,
            a_callback,
            a_userData,
            a_flags,
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            0);
    }

    HF_TaskHandle TaskScheduler::QueueOwned(
        const HF_TaskQueue a_queue,
        const std::uint32_t a_delayMs,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags,
        std::string a_moduleName,
        const HF_LogHandle a_logger,
        const std::uint32_t a_checkpoint)
    {
        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_TASK_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        auto task = std::make_shared<TaskRecord>();
        task->handle = handle;
        task->queue = a_queue;
        task->callback = a_callback;
        task->userData = a_userData;
        task->flags = a_flags;
        task->moduleName = std::move(a_moduleName);
        task->logger = a_logger;
        task->checkpoint = a_checkpoint;
        task->sessionGeneration = RuntimeState::GetSingleton().GetSessionGeneration();
        task->due = std::chrono::steady_clock::now() + std::chrono::milliseconds{ a_delayMs };

        {
            std::scoped_lock lock{ _lock };
            _tasks.emplace(handle, task);
        }

        // Never submit to F4SE synchronously from the caller's thread.
        // Native modules may queue work from arbitrary worker threads, and
        // blocking a game/UI callback while another thread enters the F4SE
        // task interface can create lock inversions. The scheduler worker owns
        // all task submission, including zero-delay tasks.
        _cv.notify_one();

        return handle;
    }

    bool TaskScheduler::Cancel(const HF_TaskHandle a_handle) noexcept
    {
        if (a_handle == HF_INVALID_TASK_HANDLE) {
            return false;
        }

        std::shared_ptr<TaskRecord> task;
        {
            std::scoped_lock lock{ _lock };
            const auto it = _tasks.find(a_handle);
            if (it == _tasks.end() || it->second->executing) {
                return false;
            }
            task = it->second;
            task->canceled.store(true, std::memory_order_release);
            _tasks.erase(it);
        }
        _cv.notify_one();
        return true;
    }

    bool TaskScheduler::CancelOwned(
        const HF_TaskHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_TASK_HANDLE || a_moduleName.empty()) {
            return false;
        }

        {
            std::scoped_lock lock{ _lock };
            const auto it = _tasks.find(a_handle);
            if (it == _tasks.end() || !it->second || it->second->executing) {
                return false;
            }
            if (it->second->moduleName != a_moduleName) {
                if (a_outActualOwner) {
                    *a_outActualOwner = it->second->moduleName;
                }
                return false;
            }
            it->second->canceled.store(true, std::memory_order_release);
            _tasks.erase(it);
        }
        _cv.notify_one();
        return true;
    }

    std::uint32_t TaskScheduler::CancelOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::uint32_t canceled = 0;
        {
            std::scoped_lock lock{ _lock };
            for (auto it = _tasks.begin(); it != _tasks.end();) {
                auto& task = it->second;
                if (task &&
                    !task->executing &&
                    task->moduleName == a_moduleName) {
                    task->canceled.store(true, std::memory_order_release);
                    it = _tasks.erase(it);
                    ++canceled;
                } else {
                    ++it;
                }
            }
        }
        if (canceled != 0) {
            _cv.notify_one();
        }
        return canceled;
    }

    std::uint32_t TaskScheduler::GetPendingCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(_tasks.size());
    }

    bool TaskScheduler::ShouldRun(const TaskRecord& a_task) const noexcept
    {
        if (a_task.canceled.load(std::memory_order_acquire)) {
            return false;
        }

        const auto& runtime = RuntimeState::GetSingleton();
        if ((a_task.flags & HF_TASK_FLAG_CANCEL_ON_SESSION_CHANGE) != 0 &&
            runtime.GetSessionGeneration() != a_task.sessionGeneration) {
            return false;
        }
        if ((a_task.flags & HF_TASK_FLAG_REQUIRE_SESSION_ACTIVE) != 0 &&
            !runtime.HasState(HF_RUNTIME_STATE_SESSION_ACTIVE)) {
            return false;
        }
        if ((a_task.flags & HF_TASK_FLAG_REQUIRE_SESSION_READY) != 0 &&
            !runtime.HasState(HF_RUNTIME_STATE_SESSION_READY)) {
            return false;
        }

        return true;
    }

    void TaskScheduler::Submit(const std::shared_ptr<TaskRecord>& a_task)
    {
        if (!a_task || a_task->canceled.load(std::memory_order_acquire)) {
            if (a_task) {
                Finalize(a_task->handle);
            }
            return;
        }

        const auto tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_task->moduleName,
                a_task->logger,
                HF_ERROR_TASK_INTERFACE_UNAVAILABLE);
            Finalize(a_task->handle);
            return;
        }

        try {
            if (a_task->queue == HF_TASK_QUEUE_UI) {
                tasks->AddUITask([this, task = a_task]() {
                    Execute(task);
                });
            } else {
                tasks->AddTask([this, task = a_task]() {
                    Execute(task);
                });
            }
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_task->moduleName,
                a_task->logger,
                HF_ERROR_TASK_QUEUE_FAILED);
            Finalize(a_task->handle);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_task->moduleName,
                a_task->logger,
                HF_ERROR_TASK_QUEUE_FAILED);
            Finalize(a_task->handle);
        }
    }

    void TaskScheduler::Execute(const std::shared_ptr<TaskRecord>& a_task)
    {
        if (!a_task) {
            return;
        }

        {
            std::scoped_lock lock{ _lock };
            const auto it = _tasks.find(a_task->handle);
            if (it == _tasks.end() || it->second.get() != a_task.get()) {
                return;
            }
            if (!ShouldRun(*a_task)) {
                _tasks.erase(it);
                return;
            }
            a_task->executing = true;
        }

        ModuleContext::Scope scope{
            a_task->moduleName.c_str(),
            a_task->logger,
            a_task->checkpoint
        };
        PerformanceMonitor::Scope perfScope{
            a_task->moduleName,
            a_task->queue == HF_TASK_QUEUE_UI ? "task.ui" : "task.game"
        };

        try {
            a_task->callback(a_task->userData);
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_task->moduleName,
                a_task->logger,
                HF_ERROR_TASK_CALLBACK_EXCEPTION);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_task->moduleName,
                a_task->logger,
                HF_ERROR_TASK_CALLBACK_EXCEPTION);
        }

        Finalize(a_task->handle);
    }

    void TaskScheduler::Finalize(const HF_TaskHandle a_handle) noexcept
    {
        std::scoped_lock lock{ _lock };
        _tasks.erase(a_handle);
    }

    void TaskScheduler::WorkerLoop()
    {
        for (;;) {
            std::vector<std::shared_ptr<TaskRecord>> dueTasks;

            {
                std::unique_lock lock{ _lock };

                for (;;) {
                    const auto now = std::chrono::steady_clock::now();
                    auto nextDue = std::chrono::steady_clock::time_point::max();

                    for (auto& [handle, task] : _tasks) {
                        (void)handle;
                        if (!task || task->submitted || task->canceled.load(std::memory_order_acquire)) {
                            continue;
                        }

                        if (task->due <= now) {
                            task->submitted = true;
                            dueTasks.push_back(task);
                        } else if (task->due < nextDue) {
                            nextDue = task->due;
                        }
                    }

                    if (!dueTasks.empty()) {
                        break;
                    }

                    if (nextDue == std::chrono::steady_clock::time_point::max()) {
                        _cv.wait(lock);
                    } else {
                        _cv.wait_until(lock, nextDue);
                    }
                }
            }

            for (const auto& task : dueTasks) {
                Submit(task);
            }
        }
    }
}
