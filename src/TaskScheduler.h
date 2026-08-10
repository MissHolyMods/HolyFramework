#pragma once

namespace HolyFramework
{
    class TaskScheduler final
    {
    public:
        static TaskScheduler& GetSingleton() noexcept;

        HF_TaskHandle Queue(
            HF_TaskQueue a_queue,
            HF_TaskCallback a_callback,
            void* a_userData,
            std::uint32_t a_flags);

        HF_TaskHandle QueueDelayed(
            HF_TaskQueue a_queue,
            std::uint32_t a_delayMs,
            HF_TaskCallback a_callback,
            void* a_userData,
            std::uint32_t a_flags);

        // Internal framework scheduling. Used by HolyFramework itself without
        // pretending that the callback belongs to a native module.
        HF_TaskHandle QueueFrameworkDelayed(
            HF_TaskQueue a_queue,
            std::uint32_t a_delayMs,
            HF_TaskCallback a_callback,
            void* a_userData,
            std::uint32_t a_flags);

        bool Cancel(HF_TaskHandle a_handle) noexcept;
        bool CancelOwned(
            HF_TaskHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t CancelOwnedBy(std::string_view a_moduleName) noexcept;
        [[nodiscard]] std::uint32_t GetPendingCount() const noexcept;

    private:
        struct TaskRecord
        {
            HF_TaskHandle handle{ HF_INVALID_TASK_HANDLE };
            HF_TaskQueue queue{ HF_TASK_QUEUE_GAME };
            HF_TaskCallback callback{ nullptr };
            void* userData{ nullptr };
            std::uint32_t flags{ HF_TASK_FLAG_NONE };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::uint32_t checkpoint{ 0 };
            std::uint64_t sessionGeneration{ 0 };
            std::chrono::steady_clock::time_point due{};
            std::atomic_bool canceled{ false };
            bool submitted{ false };
            bool executing{ false };
        };

        TaskScheduler();

        HF_TaskHandle QueueOwned(
            HF_TaskQueue a_queue,
            std::uint32_t a_delayMs,
            HF_TaskCallback a_callback,
            void* a_userData,
            std::uint32_t a_flags,
            std::string a_moduleName,
            HF_LogHandle a_logger,
            std::uint32_t a_checkpoint);

        [[nodiscard]] static bool IsValidQueue(HF_TaskQueue a_queue) noexcept;
        [[nodiscard]] bool ResolveOwner(
            HF_TaskCallback a_callback,
            std::string& a_moduleName,
            HF_LogHandle& a_logger,
            std::uint32_t& a_checkpoint) const noexcept;

        void WorkerLoop();
        void Submit(const std::shared_ptr<TaskRecord>& a_task);
        void Execute(const std::shared_ptr<TaskRecord>& a_task);
        void Finalize(HF_TaskHandle a_handle) noexcept;
        [[nodiscard]] bool ShouldRun(const TaskRecord& a_task) const noexcept;

        mutable std::mutex _lock;
        std::condition_variable _cv;
        std::unordered_map<HF_TaskHandle, std::shared_ptr<TaskRecord>> _tasks;
        std::atomic<HF_TaskHandle> _nextHandle{ 1 };
    };
}
