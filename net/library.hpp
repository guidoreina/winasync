#pragma once

#undef _WINSOCKAPI_

#include <winsock2.h>

namespace net {

// Library.
class library {
  public:
    // Constructor.
    library() = default;

    // Destructor.
    ~library();

    // Initialize.
    bool init();

    // Cleanup.
    bool cleanup();

  private:
    // Has the library been initialized=
    bool _M_initialized = false;

    // Disable copy constructor and assignment operator.
    library(const library&) = delete;
    library& operator=(const library&) = delete;
};

library::~library()
{
  // Cleanup.
  cleanup();
}

inline bool library::init()
{
  // Initiate use of the Winsock DLL.
  WSADATA wsadata;
  if (WSAStartup(MAKEWORD(2, 2), &wsadata) == 0) {
    if ((LOBYTE(wsadata.wVersion) == 2) && (HIBYTE(wsadata.wVersion) == 2)) {
      _M_initialized = true;
      return true;
    }

    WSACleanup();
  }

  return false;
}

inline bool library::cleanup()
{
  if (_M_initialized) {
    // Terminate use of the Winsock DLL.
    if (WSACleanup() == 0) {
      _M_initialized = false;
      return true;
    } else {
      return false;
    }
  } else {
    return true;
  }
}

} // namespace net