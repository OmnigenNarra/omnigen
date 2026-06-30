#pragma once
#include <coroutine>

namespace resumable
{
    struct RetVal
    {
        struct promise_type
        {
            RetVal get_return_object() { return {}; }
            std::suspend_never initial_suspend() { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }
            void unhandled_exception() {}
            void return_void() {}
        };
    };

    struct Awaiter
    {
        static Awaiter& get()
        {
            static Awaiter instance;
            return instance;
        }

        constexpr bool await_ready() const noexcept { return false; }
        constexpr void await_resume() const noexcept {}

        void await_suspend(std::coroutine_handle<> newHandle)
        {
            handle = newHandle;
        }

        static void resume()
        {
            auto& instance = get();
            if (instance.handle)
                instance.handle();
        }

    private:
        std::coroutine_handle<> handle;
    };
}