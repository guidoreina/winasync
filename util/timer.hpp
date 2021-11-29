#pragma once

#include <stdint.h>
#include <windows.h>

namespace util {

// Asynchronous timer.
class timer {
  public:
    // Callback.
    typedef void (*callbackfn)(timer&, void*);

    // Constructor.
    timer(callbackfn callback, void* user = nullptr);

    // Destructor.
    ~timer();

    // Create timer.
    bool create(PTP_CALLBACK_ENVIRON callbackenv = nullptr);

    // Timer expires in `interval` microseconds.
    void expires_in(uint64_t interval);

    // Timer expires at `expiry_time` (time in microseconds).
    void expires_at(uint64_t expiry_time);

    // Cancel timer.
    void cancel();

  private:
    // Timer.
    PTP_TIMER _M_timer = nullptr;

    // Callback.
    const callbackfn _M_callback;

    // Pointer to user data.
    void* _M_user;

    // Set timer.
    void set_timer(ULONGLONG duetime);

    // Timer callback.
    static void CALLBACK timer_callback(PTP_CALLBACK_INSTANCE instance,
                                        void* context,
                                        PTP_TIMER timer);

    // Disable copy constructor and assignment operator.
    timer(const timer&) = delete;
    timer& operator=(const timer&) = delete;
};

} // namespace util