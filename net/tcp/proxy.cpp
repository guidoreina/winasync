#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <new>
#include "net/tcp/proxy.hpp"

#define DEBUG 1

namespace net {
namespace tcp {

// Thread-safe printf().
static void print(const char* fmt, ...)
{
  static uint32_t mutex = 0;

  while (::InterlockedCompareExchange(&mutex, 1, 0) != 0);

  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);

  ::InterlockedDecrement(&mutex);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Proxy.                                                                     //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool proxy::create(DWORD minthreads,
                   DWORD maxthreads,
                   size_t nconnections,
                   uint64_t timeout)
{
  // Sanity checks.
  if ((nconnections >= min_connections) &&
      (nconnections <= max_connections) &&
      (timeout >= min_timeout) &&
      (timeout <= max_timeout)) {
    // Create thread pool.
    if (_M_thread_pool.create(minthreads, maxthreads)) {
      // Save number of connections per acceptor.
      _M_config.nconnections = nconnections;

      // Save connection timeout.
      _M_config.timeout = timeout;

      return true;
    }
  }

  return false;
}

bool proxy::listen(const socket::address& local, const socket::address& remote)
{
  return _M_acceptors.listen(local,
                             remote,
                             _M_config,
                             _M_thread_pool.callback_environment());
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Connection.                                                                //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

proxy::connection::connection(acceptor& acceptor,
                              PTP_CALLBACK_ENVIRON callbackenv)
  : _M_server{acceptor, _M_client, callbackenv},
    _M_client{_M_server, callbackenv}
{
}

bool proxy::connection::create(PTP_CALLBACK_ENVIRON callbackenv)
{
  return _M_server.create(callbackenv);
}

void proxy::connection::accept()
{
  _M_server.accept();
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Server connection.                                                         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

proxy::connection::server::server(acceptor& acceptor,
                                  client& client,
                                  PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{complete, this, callbackenv},
    _M_acceptor{acceptor},
    _M_client{client},
    _M_timer{timer, this}
{
}

bool proxy::connection::server::create(PTP_CALLBACK_ENVIRON callbackenv)
{
  // Create timer.
  return _M_timer.create(callbackenv);
}

void proxy::connection::server::accept()
{
#if DEBUG
  print("[server] Starting an asynchronous accept...\n");
#endif

  // Start an asynchronous accept.
  _M_acceptor.socket().accept(_M_sock, _M_addresses, address_length);
}

void proxy::connection::server::connected()
{
  // Two open connections.
  _M_nconnections = 2;

  // Start an asynchronous receive on the server side.
  receive();

  // Start an asynchronous receive on the client side.
  _M_client.receive();
}

void proxy::connection::server::receive()
{
  // Stop timer.
  stop_timer();

  // Start an asynchronous receive.
  _M_sock.receive(_M_recvbuf, sizeof(_M_recvbuf));
}

void proxy::connection::server::send(const void* buf, DWORD len)
{
  // Save view.
  _M_sendbuf.data = static_cast<const uint8_t*>(buf);
  _M_sendbuf.length = len;

  // Start timer.
  start_timer();

  // Start an asynchronous send.
  _M_sock.send(buf, len);
}

void proxy::connection::server::close_connections(bool cancel_timer)
{
#if DEBUG
  print("[server] Closing connections...\n");
#endif

  // Close client connection.
  _M_client.close();

  // Close server connection.
  close(cancel_timer);
}

void proxy::connection::server::close(bool cancel_timer)
{
  // Lock mutex.
  while (::InterlockedCompareExchange(&_M_mutex, 1, 0) != 0);

  // If the connection is open...
  if (_M_open) {
    _M_open = false;

    // Unlock mutex.
    ::InterlockedDecrement(&_M_mutex);

#if DEBUG
    print("[server] Closing connection...\n");
#endif

    // If the timer should be canceled...
    if (cancel_timer) {
      // Stop timer.
      stop_timer();
    }

    // Cancel outstanding requests.
    _M_sock.cancel(async::stream::socket::operation::receive);
    _M_sock.cancel(async::stream::socket::operation::send);

    // Disconnect.
    _M_sock.disconnect();
  } else {
    // Unlock mutex.
    ::InterlockedDecrement(&_M_mutex);
  }
}

void proxy::connection::server::disconnected()
{
  if (::InterlockedDecrement(&_M_nconnections) == 0) {
    // Start another asynchronous accept.
    accept();
  }
}

void proxy::connection::server::complete(async::stream::socket::operation op,
                                         DWORD error,
                                         DWORD transferred)
{
  // Success?
  if (error == 0) {
    switch (op) {
      case async::stream::socket::operation::receive:
        // Data has been received.
        received(transferred);

        break;
      case async::stream::socket::operation::send:
        // Data has been sent.
        sent(transferred);

        break;
      case async::stream::socket::operation::disconnect:
#if DEBUG
        print("[server] Disconnected.\n");
#endif

        // Disconnected.
        disconnected();

        break;
      case async::stream::socket::operation::accept:
        // Connection has been accepted.
        accepted();

        break;
      case async::stream::socket::operation::connect:
      default:
        break;
    }
  } else {
    print("[server] I/O failed (error %lu).\n", error);

    switch (op) {
      case async::stream::socket::operation::receive:
      case async::stream::socket::operation::send:
        if (error != WSA_OPERATION_ABORTED) {
          // Close server and client connections.
          close_connections();
        }

        break;
      case async::stream::socket::operation::disconnect:
        // Disconnected.
        disconnected();

        break;
      case async::stream::socket::operation::accept:
        // Start another asynchronous accept.
        accept();

        break;
      case async::stream::socket::operation::connect:
      default:
        break;
    }
  }
}

void proxy::connection::server::accepted()
{
#if DEBUG
  // Get remote address.
  socket::address addr;
  _M_sock.remote(_M_addresses, address_length, addr);

  char s[UNIX_PATH_MAX];
  if (addr.to_string(s, sizeof(s))) {
    print("[server] Received connection from '%s'.\n", s);
  }
#endif // DEBUG

  // One open connection.
  _M_nconnections = 1;

  _M_open = true;

  // Connect client.
  connect();
}

void proxy::connection::server::connect()
{
  // Start timer.
  start_timer();

  // Connect client.
  _M_client.connect(_M_acceptor.remote());
}

void proxy::connection::server::received(DWORD transferred)
{
#if DEBUG
  print("[server] Received %lu byte(s).\n", transferred);
#endif

  // If some data has been received...
  if (transferred > 0) {
#if DEBUG
    print("%.*s\n",
          static_cast<int>(transferred),
          reinterpret_cast<const char*>(_M_recvbuf));
#endif // DEBUG

    // Start timer.
    start_timer();

    // Send data to the client.
    _M_client.send(_M_recvbuf, transferred);
  } else {
    // Close server and client connections.
    close_connections();
  }
}

void proxy::connection::server::sent(DWORD count)
{
  // If we have sent all the data...
  if (count == _M_sendbuf.length) {
    // Stop timer.
    stop_timer();

    // Start an asynchronous receive on the client side.
    _M_client.receive();
  } else {
    // Send the rest.
    send(_M_sendbuf.data + count, _M_sendbuf.length - count);
  }
}

void proxy::connection::server::timer()
{
#if DEBUG
  print("[Connection timer] About to close the connections.\n");
#endif

  // Close server and client connections.
  // Do not cancel the timer, otherwise this function won't be further executed.
  static constexpr const bool cancel_timer = false;
  close_connections(cancel_timer);
}

void proxy::connection::server::start_timer()
{
  _M_timer.expires_in(_M_acceptor.config().timeout * 1000 * 1000);
}

void proxy::connection::server::stop_timer()
{
  _M_timer.cancel();
}

void proxy::connection::server::complete(async::stream::socket::operation op,
                                         DWORD error,
                                         DWORD transferred,
                                         void* user)
{
  static_cast<server*>(user)->complete(op, error, transferred);
}

void proxy::connection::server::timer(util::timer& timer, void* user)
{
  static_cast<server*>(user)->timer();
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Client connection.                                                         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

proxy::connection::client::client(server& server,
                                  PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{complete, this, callbackenv},
    _M_server{server}
{
}

void proxy::connection::client::connect(const socket::address& addr)
{
#if DEBUG
  print("[client] Connecting...\n");
#endif

  _M_sock.connect(addr);
}

void proxy::connection::client::close()
{
  // Lock mutex.
  while (::InterlockedCompareExchange(&_M_mutex, 1, 0) != 0);

  // If the connection is open...
  if (_M_open) {
    _M_open = false;

    // Unlock mutex.
    ::InterlockedDecrement(&_M_mutex);

#if DEBUG
    print("[client] Closing connection...\n");
#endif

    // Cancel outstanding requests.
    _M_sock.cancel(async::stream::socket::operation::receive);
    _M_sock.cancel(async::stream::socket::operation::send);

    // Disconnect.
    _M_sock.disconnect();
  } else {
    // Unlock mutex.
    ::InterlockedDecrement(&_M_mutex);
  }
}

void proxy::connection::client::receive()
{
  // Start an asynchronous receive.
  _M_sock.receive(_M_recvbuf, sizeof(_M_recvbuf));
}

void proxy::connection::client::send(const void* buf, DWORD len)
{
  // Save view.
  _M_sendbuf.data = static_cast<const uint8_t*>(buf);
  _M_sendbuf.length = len;

  // Start an asynchronous send.
  _M_sock.send(buf, len);
}

void proxy::connection::client::complete(async::stream::socket::operation op,
                                         DWORD error,
                                         DWORD transferred)
{
  // Success?
  if (error == 0) {
    switch (op) {
      case async::stream::socket::operation::receive:
        // Data has been received.
        received(transferred);

        break;
      case async::stream::socket::operation::send:
        // Data has been sent.
        sent(transferred);

        break;
      case async::stream::socket::operation::disconnect:
#if DEBUG
        print("[client] Disconnected.\n");
#endif

        // Disconnected.
        disconnected();

        break;
      case async::stream::socket::operation::connect:
        // Connected.
        connected();

        break;
      case async::stream::socket::operation::accept:
      default:
        break;
    }
  } else {
    print("[client] I/O failed (error %lu).\n", error);

    switch (op) {
      case async::stream::socket::operation::receive:
      case async::stream::socket::operation::send:
        if (error != WSA_OPERATION_ABORTED) {
          // Close server and client connections.
          _M_server.close_connections();
        }

        break;
      case async::stream::socket::operation::disconnect:
        // Disconnected.
        disconnected();

        break;
      case async::stream::socket::operation::connect:
        // Close server connection.
        _M_server.close();

        break;
      case async::stream::socket::operation::accept:
      default:
        break;
    }
  }
}

void proxy::connection::client::connected()
{
#if DEBUG
  print("[client] Connected.\n");
#endif

  _M_open = true;

  // Notify the server connection that the connection suceeded.
  _M_server.connected();
}

void proxy::connection::client::received(DWORD transferred)
{
#if DEBUG
  print("[client] Received %lu byte(s).\n", transferred);
#endif

  // If some data has been received...
  if (transferred > 0) {
#if DEBUG
    print("%.*s\n",
          static_cast<int>(transferred),
          reinterpret_cast<const char*>(_M_recvbuf));
#endif // DEBUG

    // Send data to the server.
    _M_server.send(_M_recvbuf, transferred);
  } else {
    // Close server and client connections.
    _M_server.close_connections();
  }
}

void proxy::connection::client::sent(DWORD count)
{
  // If we have sent all the data...
  if (count == _M_sendbuf.length) {
    // Start an asynchronous receive on the server side.
    _M_server.receive();
  } else {
    // Send the rest.
    send(_M_sendbuf.data + count, _M_sendbuf.length - count);
  }
}

void proxy::connection::client::disconnected()
{
  // Notify the server that the connection has been disconnected.
  _M_server.disconnected();
}

void proxy::connection::client::complete(async::stream::socket::operation op,
                                         DWORD error,
                                         DWORD transferred,
                                         void* user)
{
  static_cast<client*>(user)->complete(op, error, transferred);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Acceptor.                                                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

proxy::acceptor::acceptor(const configuration& config,
                          PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{nullptr, nullptr, callbackenv},
    _M_config{config}
{
}

proxy::acceptor::~acceptor()
{
  if (_M_connections) {
    for (size_t i = 0; i < _M_nconnections; i++) {
      delete _M_connections[i];
    }

    free(_M_connections);
  }
}

bool proxy::acceptor::listen(const socket::address& local,
                             const socket::address& remote,
                             PTP_CALLBACK_ENVIRON callbackenv)
{
  // Listen.
  if (_M_sock.listen(local)) {
    _M_connections = static_cast<connection**>(
                       malloc(_M_config.nconnections * sizeof(connection*))
                     );

    if (_M_connections) {
      for (_M_nconnections = 0;
           _M_nconnections < _M_config.nconnections;
           _M_nconnections++) {
        // Create connection.
        connection* const conn = new (std::nothrow) connection{*this,
                                                               callbackenv};

        // If the connection could be created...
        if (conn) {
          if (conn->create(callbackenv)) {
            // Start an asynchronous accept.
            conn->accept();

            // Save connection.
            _M_connections[_M_nconnections] = conn;
          } else {
            delete conn;
            return false;
          }
        } else {
          return false;
        }
      }

      // Save remote address.
      _M_remote = remote;

      return true;
    }
  }

  return false;
}

async::stream::socket& proxy::acceptor::socket()
{
  return _M_sock;
}

const socket::address& proxy::acceptor::remote() const
{
  return _M_remote;
}

const proxy::configuration& proxy::acceptor::config() const
{
  return _M_config;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Acceptors.                                                                 //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

proxy::acceptors::~acceptors()
{
  if (_M_acceptors) {
    for (size_t i = 0; i < _M_used; i++) {
      delete _M_acceptors[i];
    }

    free(_M_acceptors);
  }
}

bool proxy::acceptors::listen(const socket::address& local,
                              const socket::address& remote,
                              const configuration& config,
                              PTP_CALLBACK_ENVIRON callbackenv)
{
  // If space for a new acceptor can be allocated...
  if (allocate()) {
    // Create acceptor.
    proxy::acceptor* const
      acceptor = new (std::nothrow) proxy::acceptor{config, callbackenv};

    // If the acceptor could be created...
    if (acceptor) {
      // Listen.
      if (acceptor->listen(local, remote, callbackenv)) {
        // Save acceptor.
        _M_acceptors[_M_used++] = acceptor;

        return true;
      } else {
        delete acceptor;
      }
    }
  }

  return false;
}

bool proxy::acceptors::allocate()
{
  if (_M_used < _M_size) {
    return true;
  } else {
    const size_t size = (_M_size > 0) ? _M_size * 2 : allocation;

    acceptor** acceptors = static_cast<acceptor**>(
                             realloc(_M_acceptors, size * sizeof(acceptor*))
                           );

    if (acceptors) {
      _M_acceptors = acceptors;
      _M_size = size;

      return true;
    } else {
      return false;
    }
  }
}

} // namespace tcp
} // namespace net