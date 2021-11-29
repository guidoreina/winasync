#include "net/async/thread_pool.hpp"

namespace net {
namespace async {

thread_pool::~thread_pool()
{
  // Stop thread pool.
  stop();
}

void thread_pool::stop()
{
  if (_M_threadpool) {
    ::CloseThreadpool(_M_threadpool);
    _M_threadpool = nullptr;
  }
}

bool thread_pool::create(DWORD minthreads, DWORD maxthreads)
{
  // Sanity checks.
  if ((minthreads >= min_threads) &&
      (maxthreads <= max_threads) &&
      (minthreads <= maxthreads)) {
    // Create thread pool.
    _M_threadpool = ::CreateThreadpool(nullptr);

    // If the thread pool could be created...
    if (_M_threadpool) {
      // Set the minimum number of threads.
      if (::SetThreadpoolThreadMinimum(_M_threadpool, minthreads)) {
        // Set the maximum number of threads.
        ::SetThreadpoolThreadMaximum(_M_threadpool, maxthreads);

        // Initialize callback environment.
        ::InitializeThreadpoolEnvironment(&_M_callbackenv);

        // Set thread pool to be used when generating callbacks.
        ::SetThreadpoolCallbackPool(&_M_callbackenv, _M_threadpool);

        return true;
      }
    }
  }

  return false;
}

PTP_CALLBACK_ENVIRON thread_pool::callback_environment()
{
  return &_M_callbackenv;
}

} // namespace async
} // namespace net