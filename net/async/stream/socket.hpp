#pragma once

#undef _WINSOCKAPI_

#include <string.h>
#include <winsock2.h>
#include <mswsock.h>
#include "net/socket/address.hpp"

namespace net {
namespace async {
namespace stream {

// Asynchronous stream socket.
class socket {
  public:
    // Socket operation.
    enum class operation {
      accept,
      connect,
      receive,
      send,
      disconnect
    };

    // Completion callback.
    // Arguments:
    //   operation: socket operation which triggered the callback
    //   DWORD: error code
    //   DWORD: how much data was transferred
    //   void*: pointer to user data
    typedef void (*callbackfn)(operation, DWORD, DWORD, void*);

    // Load functions.
    static bool load_functions();

    // Constructor.
    socket(callbackfn callback,
           void* user = nullptr,
           PTP_CALLBACK_ENVIRON callbackenv = nullptr);

    // Destructor.
    ~socket();

    // Listen.
    bool listen(const net::socket::address& addr);

    // Accept.
    void accept(socket& sock, void* addresses, DWORD addrlen);

    // Get local address.
    void local(void* addresses, DWORD addrlen, net::socket::address& addr);

    // Get remote addess.
    void remote(void* addresses, DWORD addrlen, net::socket::address& addr);

    // Connect.
    void connect(const net::socket::address& addr);

    // Receive.
    void receive(void* buf, size_t len, DWORD flags = 0);

    // Send.
    void send(const void* buf, size_t len, DWORD flags = 0);

    // Disconnect.
    void disconnect();

    // Cancel all outstanding operations.
    void cancel();

    // Cancel outstanding operation.
    void cancel(operation op);

  private:
    // Extended overlapped structure containing a socket operation.
    class overlapped {
      public:
        // Constructor.
        overlapped();
        overlapped(enum operation op);

        // Destructor.
        ~overlapped() = default;

        // Clear.
        void clear();

        // Get operation.
        enum operation operation() const;

        // Set operation.
        void operation(enum operation op);

        // Get `I/O pending` flag.
        bool io_pending() const;

        // Set `I/O pending` flag.
        void io_pending(bool val);

        // Cast operators.
        operator const OVERLAPPED*() const;
        operator OVERLAPPED*();

      private:
        // Overlapped structure.
        OVERLAPPED _M_overlapped;

        // Socket operation.
        enum operation _M_operation;

        // I/O pending?
        bool _M_io_pending = false;
    };

    // Socket handle.
    SOCKET _M_sock = INVALID_SOCKET;

    // I/O completion object.
    PTP_IO _M_io = nullptr;

    union {
      // Communication domain.
      int _M_domain;

      // Listener socket.
      SOCKET _M_listener;
    };

    // Overlapped structures.
    overlapped _M_overlapped;
    overlapped _M_receiveov;
    overlapped _M_sendov;
    overlapped _M_disconnectov;

    // Callback.
    const callbackfn _M_callback;

    // Pointer to user data.
    void* _M_user;

    // Callback environment.
    PTP_CALLBACK_ENVIRON _M_callbackenv;

    // Pointer to the AcceptEx() function.
    static LPFN_ACCEPTEX _M_acceptex;

    // Pointer to the GetAcceptExSockaddrs() function.
    static LPFN_GETACCEPTEXSOCKADDRS _M_getacceptexsockaddrs;

    // Pointer to the ConnectEx() function.
    static LPFN_CONNECTEX _M_connectex;

    // Pointer to the DisconnectEx() function.
    static LPFN_DISCONNECTEX _M_disconnectex;

    // Initialize socket.
    DWORD init(int domain);

    // Close socket.
    void close();

    // Bind socket.
    DWORD bind(int domain);

    // Update accept context.
    DWORD update_accept_context();

    // Update connect context.
    DWORD update_connect_context();

    // I/O completion callback.
    static void CALLBACK io_completion_callback(PTP_CALLBACK_INSTANCE instance,
                                                void* context,
                                                void* overlapped,
                                                ULONG result,
                                                ULONG_PTR transferred,
                                                PTP_IO io);

    // Disable copy constructor and assignment operator.
    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;
};

inline socket::overlapped::overlapped()
{
  clear();
}

inline socket::overlapped::overlapped(enum operation op)
  : _M_operation{op}
{
  clear();
}

inline void socket::overlapped::clear()
{
  memset(&_M_overlapped, 0, sizeof(OVERLAPPED));
}

inline enum socket::operation socket::overlapped::operation() const
{
  return _M_operation;
}

inline void socket::overlapped::operation(enum operation op)
{
  _M_operation = op;
}

inline bool socket::overlapped::io_pending() const
{
  return _M_io_pending;
}

inline void socket::overlapped::io_pending(bool val)
{
  _M_io_pending = val;
}

inline socket::overlapped::operator const OVERLAPPED*() const
{
  return &_M_overlapped;
}

inline socket::overlapped::operator OVERLAPPED*()
{
  return &_M_overlapped;
}

} // namespace stream
} // namespace async
} // namespace net