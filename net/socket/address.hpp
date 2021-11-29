#pragma once

#include <string.h>

#if !defined(_WIN32)
  #include <sys/socket.h>
  #include <netinet/in.h>
#else
  #include <winsock2.h>
  #include <ws2tcpip.h>

  typedef short sa_family_t;
  typedef unsigned short in_port_t;
#endif

#if !defined(UNIX_PATH_MAX)
  #define UNIX_PATH_MAX 108
#endif

#if defined(__MINGW32__)
  struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[UNIX_PATH_MAX];
  };
#endif // defined(__MINGW32__)

namespace net {
namespace socket {

// Socket address.
class address {
  public:
    // Constructor.
    address() = default;
    address(const struct sockaddr& addr, socklen_t addrlen);

    // Destructor.
    ~address() = default;

    // Build.
    void build(const struct sockaddr& addr, socklen_t addrlen);
    bool build(const char* address);
    bool build(const char* address, in_port_t port);

#if !defined(_WIN32)
    // Abstract socket address is not supported on Windows:
    // https://github.com/microsoft/WSL/issues/4240

    // Build abstract socket address.
    bool build_abstract(const void* address, size_t len);
#endif // !defined(_WIN32)

    // Cast operators.
    operator const struct sockaddr&() const;
    operator const struct sockaddr*() const;
    operator struct sockaddr&();
    operator struct sockaddr*();

    // Get family.
    sa_family_t family() const;

    // Get address length.
    socklen_t length() const;

    // Set address length.
    void length(socklen_t addrlen);

    // To string.
    const char* to_string(char* dst, size_t size) const;

  private:
    // Address.
    struct sockaddr_storage _M_addr;

    // Address length.
    socklen_t _M_length;

    // Extract IP and port.
    static bool extract_ip_port(const char* address, char* ip, in_port_t& port);

    // Parse port.
    static bool parse_port(const char* s, in_port_t& port);
};

inline address::address(const struct sockaddr& addr, socklen_t addrlen)
{
  build(addr, addrlen);
}

inline void address::build(const struct sockaddr& addr, socklen_t addrlen)
{
  memcpy(&_M_addr, &addr, addrlen);
  _M_length = addrlen;
}

inline address::operator const struct sockaddr&() const
{
  return reinterpret_cast<const struct sockaddr&>(_M_addr);
}

inline address::operator const struct sockaddr*() const
{
  return reinterpret_cast<const struct sockaddr*>(&_M_addr);
}

inline address::operator struct sockaddr&()
{
  return reinterpret_cast<struct sockaddr&>(_M_addr);
}

inline address::operator struct sockaddr*()
{
  return reinterpret_cast<struct sockaddr*>(&_M_addr);
}

inline sa_family_t address::family() const
{
  return _M_addr.ss_family;
}

inline socklen_t address::length() const
{
  return _M_length;
}

inline void address::length(socklen_t len)
{
  _M_length = len;
}

} // namespace socket
} // namespace net