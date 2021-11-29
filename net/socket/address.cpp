#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#if !defined(_WIN32)
  #include <sys/un.h>
  #include <arpa/inet.h>
#endif // !defined(_WIN32)

#include <errno.h>
#include "net/socket/address.hpp"

bool net::socket::address::build(const char* address)
{
  char ip[INET6_ADDRSTRLEN];
  in_port_t port;
  if (extract_ip_port(address, ip, port)) {
    return build(ip, port);
  } else {
    // Unix socket.

    // Compute length of the address including the terminating '\0'.
    const size_t len = strlen(address) + 1;

    // If the length is not too long...
    if (len <= UNIX_PATH_MAX) {
      struct sockaddr_un* const
        sin = reinterpret_cast<struct sockaddr_un*>(&_M_addr);

      sin->sun_family = AF_UNIX;
      memcpy(sin->sun_path, address, len);

      _M_length = offsetof(struct sockaddr_un, sun_path) + len;

      return true;
    }
  }

  return false;
}

bool net::socket::address::build(const char* address, in_port_t port)
{
  // IPv4 address?
  {
    struct sockaddr_in* const
      sin = reinterpret_cast<struct sockaddr_in*>(&_M_addr);

    if (inet_pton(AF_INET, address, &sin->sin_addr) == 1) {
      sin->sin_family = AF_INET;
      sin->sin_port = htons(port);

      memset(sin->sin_zero, 0, sizeof(sin->sin_zero));

      _M_length = sizeof(struct sockaddr_in);

      return true;
    }
  }

  // IPv6 address?
  {
    struct sockaddr_in6* const
      sin = reinterpret_cast<struct sockaddr_in6*>(&_M_addr);

    if (inet_pton(AF_INET6, address, &sin->sin6_addr) == 1) {
      sin->sin6_family = AF_INET6;
      sin->sin6_port = htons(port);

      sin->sin6_flowinfo = 0;
      sin->sin6_scope_id = 0;

      _M_length = sizeof(struct sockaddr_in6);

      return true;
    }
  }

  return false;
}

#if !defined(_WIN32)
bool net::socket::address::build_abstract(const void* address, size_t len)
{
  if (len <= UNIX_PATH_MAX) {
    struct sockaddr_un* const
      sin = reinterpret_cast<struct sockaddr_un*>(&_M_addr);

    sin->sun_family = AF_UNIX;
    memcpy(sin->sun_path, address, len);

    _M_length = offsetof(struct sockaddr_un, sun_path) + len;

    return true;
  }

  return false;
}
#endif // !defined(_WIN32)

const char* net::socket::address::to_string(char* dst, size_t size) const
{
  switch (_M_addr.ss_family) {
    case AF_INET:
      {
        const struct sockaddr_in* const
          sin = reinterpret_cast<const struct sockaddr_in*>(&_M_addr);

        if (inet_ntop(AF_INET, &sin->sin_addr, dst, size)) {
          const size_t len = strlen(dst);

          const size_t left = size - len;

          if (snprintf(dst + len,
                       left,
                       ":%u",
                       ntohs(sin->sin_port)) < static_cast<ssize_t>(left)) {
            return dst;
          } else {
            errno = ENOSPC;
          }
        }
      }

      break;
    case AF_INET6:
      if (size > 2) {
        const struct sockaddr_in6* const
          sin = reinterpret_cast<const struct sockaddr_in6*>(&_M_addr);

        if (inet_ntop(AF_INET6, &sin->sin6_addr, dst + 1, size - 1)) {
          const size_t len = 1 + strlen(dst + 1);

          const size_t left = size - len;

          if (snprintf(dst + len,
                       left,
                       "]:%u",
                       ntohs(sin->sin6_port)) < static_cast<ssize_t>(left)) {
            *dst = '[';

            return dst;
          } else {
            errno = ENOSPC;
          }
        }
      } else {
        errno = ENOSPC;
      }

      break;
    case AF_UNIX:
      {
        const struct sockaddr_un* const
          sin = reinterpret_cast<const struct sockaddr_un*>(&_M_addr);

        const size_t pathlen =
          _M_length - offsetof(struct sockaddr_un, sun_path);

        // If not an abstract socket address...
        if (sin->sun_path[0]) {
          // If the buffer is big enough...
          if (pathlen <= size) {
            // The terminating '\0' is already included in `pathlen`.
            memcpy(dst, sin->sun_path, pathlen);

            return dst;
          }
        } else {
          // If the buffer is big enough...
          if (pathlen < size) {
            *dst = '@';
            memcpy(dst + 1, sin->sun_path + 1, pathlen - 1);
            dst[pathlen] = 0;

            return dst;
          }
        }

        errno = ENOSPC;
      }

      break;
  }

  return nullptr;
}

bool net::socket::address::extract_ip_port(const char* address,
                                           char* ip,
                                           in_port_t& port)
{
  // Search last colon.
  const char* const last_colon = strrchr(address, ':');

  // If the last colon was found...
  if (last_colon) {
    size_t len = last_colon - address;
    if (len > 0) {
      if (*address == '[') {
        if ((len > 2) && (last_colon[-1] == ']')) {
          // Skip '['.
          address++;

          len -= 2;
        } else {
          return false;
        }
      }

      if (len < INET6_ADDRSTRLEN) {
        if (parse_port(last_colon + 1, port)) {
          memcpy(ip, address, len);
          ip[len] = 0;

          return true;
        }
      }
    }
  }

  return false;
}

bool net::socket::address::parse_port(const char* s, in_port_t& port)
{
  unsigned n = 0;
  while (*s) {
    if ((*s >= '0') &&
        (*s <= '9') &&
        ((n = (n * 10) + (*s - '0')) <= 65535)) {
      s++;
    } else {
      return false;
    }
  }

  if (n > 0) {
    port = static_cast<in_port_t>(n);
    return true;
  }

  return false;
}
