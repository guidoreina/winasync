#pragma once

#include <windows.h>

namespace net {
namespace async {

// Thread pool.
class thread_pool {
  public:
    // Minimum number of threads.
    static constexpr const DWORD min_threads = 1;

    // Maximum number of threads.
    static constexpr const DWORD max_threads = 256;

    // Default maximum number of threads.
    static constexpr const DWORD default_max_threads = 4;

    // Constructor.
    thread_pool() = default;

    // Destructor.
    ~thread_pool();

    // Stop.
    void stop();

    // Create thread pool.
    bool create(DWORD minthreads = min_threads,
                DWORD maxthreads = default_max_threads);

    // Get callback environment.
    PTP_CALLBACK_ENVIRON callback_environment();

  private:
    // Thread pool.
    PTP_POOL _M_threadpool = nullptr;

    // Callback environment.
    TP_CALLBACK_ENVIRON _M_callbackenv;

    // Disable copy constructor and assignment operator.
    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;
};

} // namespace async
} // namespace net