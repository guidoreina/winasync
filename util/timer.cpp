#include "util/timer.hpp"

namespace util {

timer::timer(callbackfn callback, void* user)
  : _M_callback{callback},
    _M_user{user}
{
}

timer::~timer()
{
  // Cancel timer.
  cancel();

  if (_M_timer) {
    // Release timer object.
    ::CloseThreadpoolTimer(_M_timer);
  }
}

bool timer::create(PTP_CALLBACK_ENVIRON callbackenv)
{
  // Create timer.
  _M_timer = ::CreateThreadpoolTimer(timer_callback, this, callbackenv);

  return (_M_timer != nullptr);
}

void timer::expires_in(uint64_t interval)
{
  set_timer(static_cast<ULONGLONG>(-10 * interval));
}

void timer::expires_at(uint64_t expiry_time)
{
  set_timer(static_cast<ULONGLONG>(10 * expiry_time));
}

void timer::cancel()
{
  if (_M_timer) {
    ::SetThreadpoolTimer(_M_timer, nullptr, 0, 0);

    // Wait for outstanding timer callbacks.
    static constexpr const BOOL cancel_pending_callbacks = TRUE;
    ::WaitForThreadpoolTimerCallbacks(_M_timer, cancel_pending_callbacks);
  }
}

void timer::set_timer(ULONGLONG duetime)
{
  ULARGE_INTEGER ul;
  ul.QuadPart = duetime;

  FILETIME ft;
  ft.dwHighDateTime = ul.HighPart;
  ft.dwLowDateTime = ul.LowPart;

  // Set timer.
  ::SetThreadpoolTimer(_M_timer, &ft, 0, 0);
}

void CALLBACK timer::timer_callback(PTP_CALLBACK_INSTANCE instance,
                                    void* context,
                                    PTP_TIMER timer)
{
  // Invoke callback.
  util::timer* const t = static_cast<util::timer*>(context);
  t->_M_callback(*t, t->_M_user);
}

} // namespace util