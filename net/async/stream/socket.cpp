#include "net/async/stream/socket.hpp"

namespace net {
namespace async {
namespace stream {

// Pointer to the AcceptEx() function.
LPFN_ACCEPTEX socket::_M_acceptex = nullptr;

// Pointer to the GetAcceptExSockaddrs() function.
LPFN_GETACCEPTEXSOCKADDRS socket::_M_getacceptexsockaddrs = nullptr;

// Pointer to the ConnectEx() function.
LPFN_CONNECTEX socket::_M_connectex = nullptr;

// Pointer to the DisconnectEx() function.
LPFN_DISCONNECTEX socket::_M_disconnectex = nullptr;

bool socket::load_functions()
{
  // Create socket.
  const SOCKET sock = ::WSASocket(AF_INET,
                                  SOCK_STREAM,
                                  0,
                                  nullptr,
                                  0,
                                  WSA_FLAG_OVERLAPPED);

  // If the socket could be created...
  if (sock != INVALID_SOCKET) {
    DWORD bytes;

    {
      // Get address of the AcceptEx() function.
      GUID guid = WSAID_ACCEPTEX;
      if (::WSAIoctl(sock,
                     SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid,
                     sizeof(GUID),
                     &_M_acceptex,
                     sizeof(LPFN_ACCEPTEX),
                     &bytes,
                     nullptr,
                     nullptr) != 0) {
        ::closesocket(sock);
        return false;
      }
    }

    {
      // Get address of the GetAcceptExSockaddrs() function.
      GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
      if (::WSAIoctl(sock,
                     SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid,
                     sizeof(GUID),
                     &_M_getacceptexsockaddrs,
                     sizeof(LPFN_GETACCEPTEXSOCKADDRS),
                     &bytes,
                     nullptr,
                     nullptr) != 0) {
        ::closesocket(sock);
        return false;
      }
    }

    {
      // Get address of the ConnectEx() function.
      GUID guid = WSAID_CONNECTEX;
      if (::WSAIoctl(sock,
                     SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid,
                     sizeof(GUID),
                     &_M_connectex,
                     sizeof(LPFN_CONNECTEX),
                     &bytes,
                     nullptr,
                     nullptr) != 0) {
        ::closesocket(sock);
        return false;
      }
    }

    {
      // Get address of the DisconnectEx() function.
      GUID guid = WSAID_DISCONNECTEX;
      if (::WSAIoctl(sock,
                     SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid,
                     sizeof(GUID),
                     &_M_disconnectex,
                     sizeof(LPFN_DISCONNECTEX),
                     &bytes,
                     nullptr,
                     nullptr) != 0) {
        ::closesocket(sock);
        return false;
      }
    }

    ::closesocket(sock);
    return true;
  }

  return false;
}

socket::socket(callbackfn callback,
               void* user,
               PTP_CALLBACK_ENVIRON callbackenv)
  : _M_receiveov{operation::receive},
    _M_sendov{operation::send},
    _M_disconnectov{operation::disconnect},
    _M_callback{callback},
    _M_user{user},
    _M_callbackenv{callbackenv}
{
}

socket::~socket()
{
  // Cancel outstanding socket operations.
  cancel();

  if (_M_io) {
    // Release I/O completion object.
    ::CloseThreadpoolIo(_M_io);
  }

  if (_M_sock != INVALID_SOCKET) {
    // Close socket.
    ::closesocket(_M_sock);
  }
}

bool socket::listen(const net::socket::address& addr)
{
  // Initialize socket.
  if (init(addr.family()) == 0) {
    // IPv4 or IPv6?
    if ((addr.family() == AF_INET) || (addr.family() == AF_INET6)) {
      // Prevent other sockets to be bound to the same address and port.
      static constexpr const BOOL optval = 1;
      if (::setsockopt(_M_sock,
                       SOL_SOCKET,
                       SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&optval),
                       sizeof(BOOL)) != 0) {
        // Close socket.
        close();

        return false;
      }
    }

    // Bind and listen.
    if ((::bind(_M_sock,
                static_cast<const struct sockaddr*>(addr),
                addr.length()) == 0) &&
        (::listen(_M_sock, SOMAXCONN) == 0)) {
      // Save domain.
      _M_domain = addr.family();

      return true;
    }

    // Close socket.
    close();
  }

  return false;
}

void socket::accept(socket& sock, void* addresses, DWORD addrlen)
{
  // Initialize socket.
  DWORD error = sock.init(_M_domain);

  // Error?
  if (error != 0) {
    sock._M_callback(operation::accept, error, 0, sock._M_user);
    return;
  }

  // Save listener.
  sock._M_listener = _M_sock;

  // Set socket operation.
  sock._M_overlapped.operation(operation::accept);

  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  // Start an asynchronous accept.
  DWORD bytes;
  if (_M_acceptex(_M_sock,
                  sock._M_sock,
                  addresses,
                  0,
                  addrlen,
                  addrlen,
                  &bytes,
                  static_cast<OVERLAPPED*>(sock._M_overlapped))) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    // Update accept context.
    error = sock.update_accept_context();

    // Success?
    if (error == 0) {
      sock._M_callback(operation::accept, 0, 0, sock._M_user);
    } else {
      // Close socket.
      sock.close();

      sock._M_callback(operation::accept, error, 0, sock._M_user);
    }
  } else {
    // Get error code.
    const int error = ::WSAGetLastError();

    if (error == WSA_IO_PENDING) {
      sock._M_overlapped.io_pending(true);
    } else {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      // Close socket.
      sock.close();

      sock._M_callback(operation::accept, error, 0, sock._M_user);
    }
  }
}

void socket::local(void* addresses, DWORD addrlen, net::socket::address& addr)
{
  struct sockaddr* local;
  struct sockaddr* remote;
  int locallen;
  int remotelen;
  _M_getacceptexsockaddrs(addresses,
                          0,
                          addrlen,
                          addrlen,
                          &local,
                          &locallen,
                          &remote,
                          &remotelen);

  addr.build(*local, locallen);
}

void socket::remote(void* addresses, DWORD addrlen, net::socket::address& addr)
{
  struct sockaddr* local;
  struct sockaddr* remote;
  int locallen;
  int remotelen;
  _M_getacceptexsockaddrs(addresses,
                          0,
                          addrlen,
                          addrlen,
                          &local,
                          &locallen,
                          &remote,
                          &remotelen);

  addr.build(*remote, remotelen);
}

void socket::connect(const net::socket::address& addr)
{
  // Initialize socket.
  DWORD error = init(addr.family());

  // Success?
  if (error == 0) {
    // Bind.
    error = bind(addr.family());

    // Error?
    if (error != 0) {
      // Close socket.
      close();

      _M_callback(operation::connect, error, 0, _M_user);
      return;
    }
  } else {
    _M_callback(operation::connect, error, 0, _M_user);
    return;
  }

  // Set socket operation.
  _M_overlapped.operation(operation::connect);

  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  // Connect.
  if (_M_connectex(_M_sock,
                   static_cast<const struct sockaddr*>(addr),
                   addr.length(),
                   nullptr,
                   0,
                   nullptr,
                   static_cast<OVERLAPPED*>(_M_overlapped))) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    // Update connect context.
    error = update_connect_context();

    // Success?
    if (error == 0) {
      _M_callback(operation::connect, 0, 0, _M_user);
    } else {
      // Close socket.
      close();

      _M_callback(operation::connect, error, 0, _M_user);
    }
  } else {
    // Get error code.
    const int error = ::WSAGetLastError();

    if (error == WSA_IO_PENDING) {
      _M_overlapped.io_pending(true);
    } else {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      // Close socket.
      close();

      _M_callback(operation::connect, error, 0, _M_user);
    }
  }
}

void socket::receive(void* buf, size_t len, DWORD flags)
{
  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  WSABUF wsabuf{static_cast<ULONG>(len), static_cast<char*>(buf)};
  DWORD received;
  if (::WSARecv(_M_sock,
                &wsabuf,
                1,
                &received,
                &flags,
                static_cast<OVERLAPPED*>(_M_receiveov),
                nullptr) == 0) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    _M_callback(operation::receive, 0, received, _M_user);
  } else {
    // Get error code.
    const int error = ::WSAGetLastError();

    if (error == WSA_IO_PENDING) {
      _M_receiveov.io_pending(true);
    } else {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      _M_callback(operation::receive, error, received, _M_user);
    }
  }
}

void socket::send(const void* buf, size_t len, DWORD flags)
{
  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  WSABUF wsabuf{static_cast<ULONG>(len),
                static_cast<char*>(const_cast<void*>(buf))};

  DWORD sent;
  if (::WSASend(_M_sock,
                &wsabuf,
                1,
                &sent,
                flags,
                static_cast<OVERLAPPED*>(_M_sendov),
                nullptr) == 0) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    _M_callback(operation::send, 0, sent, _M_user);
  } else {
    // Get error code.
    const int error = ::WSAGetLastError();

    if (error == WSA_IO_PENDING) {
      _M_sendov.io_pending(true);
    } else {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      _M_callback(operation::send, error, sent, _M_user);
    }
  }
}

void socket::disconnect()
{
  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  // Disconnect.
  if (_M_disconnectex(_M_sock,
                      static_cast<OVERLAPPED*>(_M_disconnectov),
                      TF_REUSE_SOCKET,
                      0)) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    // Close socket.
    close();

    _M_callback(operation::disconnect, 0, 0, _M_user);
  } else {
    // Save error code.
    const int error = ::WSAGetLastError();

    if (error == WSA_IO_PENDING) {
      _M_disconnectov.io_pending(true);
    } else {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      // Close socket.
      close();

      _M_callback(operation::disconnect, error, 0, _M_user);
    }
  }
}

void socket::cancel()
{
  if (_M_sock != INVALID_SOCKET) {
    // If there is an outstanding receive...
    if (_M_receiveov.io_pending()) {
      ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                   static_cast<OVERLAPPED*>(_M_receiveov));
    }

    // If there is an outstandig send...
    if (_M_sendov.io_pending()) {
      ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                   static_cast<OVERLAPPED*>(_M_sendov));
    }

    // If there is an outstanding accept or connect...
    if (_M_overlapped.io_pending()) {
      ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                   static_cast<OVERLAPPED*>(_M_overlapped));
    }

    // If there is an outstanding disconnect...
    if (_M_disconnectov.io_pending()) {
      ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                   static_cast<OVERLAPPED*>(_M_disconnectov));
    }
  }
}

void socket::cancel(operation op)
{
  if (_M_sock != INVALID_SOCKET) {
    switch (op) {
      case operation::receive:
        // If there is an outstanding receive...
        if (_M_receiveov.io_pending()) {
          ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                       static_cast<OVERLAPPED*>(_M_receiveov));
        }

        break;
      case operation::send:
        // If there is an outstandig send...
        if (_M_sendov.io_pending()) {
          ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                       static_cast<OVERLAPPED*>(_M_sendov));
        }

        break;
      case operation::accept:
      case operation::connect:
        // If there is an outstanding accept or connect...
        if (_M_overlapped.io_pending()) {
          ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                       static_cast<OVERLAPPED*>(_M_overlapped));
        }

        break;
      case operation::disconnect:
        // If there is an outstanding disconnect...
        if (_M_disconnectov.io_pending()) {
          ::CancelIoEx(reinterpret_cast<HANDLE>(_M_sock),
                       static_cast<OVERLAPPED*>(_M_disconnectov));
        }

        break;
      default:
        break;
    }
  }
}

DWORD socket::init(int domain)
{
  // Create non-overlapped socket.
  _M_sock = ::WSASocket(domain,
                        SOCK_STREAM,
                        0,
                        nullptr,
                        0,
                        WSA_FLAG_OVERLAPPED);

  // If the socket could be created...
  if (_M_sock != INVALID_SOCKET) {
    // Do not queue completion packets to the I/O completion port when
    // I/O operations complete immediately.
    static constexpr const UCHAR flags = FILE_SKIP_COMPLETION_PORT_ON_SUCCESS;
    if (::SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(_M_sock),
                                             flags)) {
      // Create I/O completion object.
      _M_io = ::CreateThreadpoolIo(reinterpret_cast<HANDLE>(_M_sock),
                                   io_completion_callback,
                                   this,
                                   _M_callbackenv);

      // If the I/O completion object could be created...
      if (_M_io) {
        // Clear overlapped structures.
        _M_overlapped.clear();
        _M_receiveov.clear();
        _M_sendov.clear();
        _M_disconnectov.clear();

        return 0;
      }
    }

    // Save error code.
    const DWORD error = ::GetLastError();

    // Close socket.
    ::closesocket(_M_sock);
    _M_sock = INVALID_SOCKET;

    return error;
  } else {
    return ::WSAGetLastError();
  }
}

void socket::close()
{
  // Release I/O completion object.
  ::CloseThreadpoolIo(_M_io);
  _M_io = nullptr;

  // Close socket.
  ::closesocket(_M_sock);
  _M_sock = INVALID_SOCKET;
}

DWORD socket::bind(int domain)
{
  switch (domain) {
    case AF_INET:
      {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

        // ConnectEx() requires the socket to be bound.
        if (::bind(_M_sock,
                   reinterpret_cast<const struct sockaddr*>(&addr),
                   sizeof(struct sockaddr_in)) == 0) {
          return 0;
        } else {
          return ::WSAGetLastError();
        }
      }

      break;
    case AF_INET6:
      {
        struct sockaddr_in6 addr;
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = IN6ADDR_ANY_INIT;
        addr.sin6_port = 0;
        addr.sin6_flowinfo = 0;
        addr.sin6_scope_id = 0;

        // ConnectEx() requires the socket to be bound.
        if (::bind(_M_sock,
                   reinterpret_cast<const struct sockaddr*>(&addr),
                   sizeof(struct sockaddr_in6)) == 0) {
          return 0;
        } else {
          return ::WSAGetLastError();
        }
      }

      break;
    case AF_UNIX:
      {
        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = 0;

        // ConnectEx() requires the socket to be bound.
        if (::bind(_M_sock,
                   reinterpret_cast<const struct sockaddr*>(&addr),
                   offsetof(struct sockaddr_un, sun_path) + 1) == 0) {
          return 0;
        } else {
          return ::WSAGetLastError();
        }
      }

      break;
    default:
      return WSAEINVAL;
  }
}

DWORD socket::update_accept_context()
{
  // Update accept context.
  return (::setsockopt(_M_sock,
                       SOL_SOCKET,
                       SO_UPDATE_ACCEPT_CONTEXT,
                       reinterpret_cast<const char*>(&_M_listener),
                       sizeof(SOCKET)) == 0) ? 0 : ::WSAGetLastError();
}

DWORD socket::update_connect_context()
{
  // Update connect context.
  return (::setsockopt(_M_sock,
                       SOL_SOCKET,
                       SO_UPDATE_CONNECT_CONTEXT,
                       nullptr,
                       0) == 0) ? 0 : ::WSAGetLastError();
}

void CALLBACK socket::io_completion_callback(PTP_CALLBACK_INSTANCE instance,
                                             void* context,
                                             void* overlapped,
                                             ULONG result,
                                             ULONG_PTR transferred,
                                             PTP_IO io)
{
  switch (static_cast<class overlapped*>(overlapped)->operation()) {
    case operation::receive:
      {
        socket* const sock = static_cast<socket*>(context);

        sock->_M_receiveov.io_pending(false);
        sock->_M_callback(operation::receive,
                          result,
                          transferred,
                          sock->_M_user);
      }

      break;
    case operation::send:
      {
        socket* const sock = static_cast<socket*>(context);

        sock->_M_sendov.io_pending(false);
        sock->_M_callback(operation::send,
                          result,
                          transferred,
                          sock->_M_user);
      }

      break;
    case operation::accept:
      {
        socket* const sock = CONTAINING_RECORD(overlapped,
                                               socket,
                                               _M_overlapped);

        // Success?
        if (result == 0) {
          // Update accept context.
          result = sock->update_accept_context();

          // Error?
          if (result != 0) {
            // Close socket.
            sock->close();
          }
        } else {
          // Close socket.
          sock->close();
        }

        sock->_M_overlapped.io_pending(false);
        sock->_M_callback(operation::accept,
                          result,
                          transferred,
                          sock->_M_user);
      }

      break;
    case operation::connect:
      {
        socket* const sock = static_cast<socket*>(context);

        // Success?
        if (result == 0) {
          // Update connect context.
          result = sock->update_connect_context();

          // Error?
          if (result != 0) {
            // Close socket.
            sock->close();
          }
        } else {
          // Close socket.
          sock->close();
        }

        sock->_M_overlapped.io_pending(false);
        sock->_M_callback(operation::connect,
                          result,
                          transferred,
                          sock->_M_user);
      }

      break;
    case operation::disconnect:
      {
        socket* const sock = static_cast<socket*>(context);

        if (sock->_M_io) {
          // Release I/O completion object.
          ::CloseThreadpoolIo(sock->_M_io);
          sock->_M_io = nullptr;
        }

        if (sock->_M_sock != INVALID_SOCKET) {
          // Close socket.
          ::closesocket(sock->_M_sock);
          sock->_M_sock = INVALID_SOCKET;
        }

        sock->_M_disconnectov.io_pending(false);
        sock->_M_callback(operation::disconnect,
                          result,
                          transferred,
                          sock->_M_user);
      }

      break;
    default:
      break;
  }
}

} // namespace stream
} // namespace async
} // namespace net