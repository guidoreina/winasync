#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <new>
#include "net/tcp/receiver.hpp"

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
// Receiver.                                                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool receiver::create(const char* tmpdir,
                      const char* finaldir,
                      DWORD minthreads,
                      DWORD maxthreads,
                      size_t nconnections,
                      uint64_t timeout,
                      uint64_t maxfilesize,
                      uint64_t maxfileage)
{
  // Sanity checks.
  if ((nconnections >= min_connections) &&
      (nconnections <= max_connections) &&
      (timeout >= min_timeout) &&
      (timeout <= max_timeout) &&
      (maxfilesize >= min_file_size) &&
      (maxfilesize <= max_file_size) &&
      (maxfileage >= min_file_age) &&
      (maxfileage <= max_file_age)) {
    const size_t tmpdirlen = strlen(tmpdir);
    if (tmpdirlen < sizeof(_M_config.tmpdir)) {
      const size_t finaldirlen = strlen(finaldir);
      struct _stat64 sbuf;
      if ((finaldirlen < sizeof(_M_config.finaldir)) &&
          (_stricmp(tmpdir, finaldir) != 0) &&
          (_stat64(tmpdir, &sbuf) == 0) &&
          ((sbuf.st_mode & _S_IFDIR) != 0) &&
          (_stat64(finaldir, &sbuf) == 0) &&
          ((sbuf.st_mode & _S_IFDIR) != 0)) {
        // Create thread pool.
        if (_M_thread_pool.create(minthreads, maxthreads)) {
          // Save number of connections per acceptor.
          _M_config.nconnections = nconnections;

          // Save directory where to store the temporary files.
          memcpy(_M_config.tmpdir, tmpdir, tmpdirlen + 1);

          // Save directory where to store the final files.
          memcpy(_M_config.finaldir, finaldir, finaldirlen + 1);

          // Save connection timeout.
          _M_config.timeout = timeout;

          // Save maximum file size.
          _M_config.maxfilesize = maxfilesize;

          // Save maximum file age.
          _M_config.maxfileage = maxfileage;

          return true;
        }
      }
    }
  }

  return false;
}

bool receiver::listen(const socket::address& addr)
{
  return _M_acceptors.listen(addr,
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

receiver::connection::connection(acceptor& acceptor,
                                 size_t nconnection,
                                 PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{complete, this, callbackenv},
    _M_acceptor{acceptor},
    _M_file{complete, this},
    _M_connection_timer{connection_timer, this},
    _M_file_timer{file_timer, this},
    _M_nconnection{nconnection},
    _M_callbackenv{callbackenv}
{
}

bool receiver::connection::create()
{
  // Create timers.
  return ((_M_connection_timer.create(_M_callbackenv)) &&
          (_M_file_timer.create(_M_callbackenv)));
}

void receiver::connection::accept()
{
  // Start an asynchronous accept.
  _M_acceptor.socket().accept(_M_sock, _M_addresses, address_length);
}

bool receiver::connection::open_file()
{
  // Compose name of the file to be created.
  char pathname[MAX_PATH];
  snprintf(pathname,
           sizeof(pathname),
           "%s\\file-%zu-%zu.bin",
           _M_acceptor.config().tmpdir,
           _M_nconnection,
           ++_M_nfile);

  // Open file for writing.
  if (_M_file.open(pathname,
                   filesystem::async::file::mode::write,
                   _M_callbackenv)) {
#if DEBUG
    print("Opened file '%s'.\n", pathname);
#endif

    // Reset file size.
    _M_filesize = 0;

    // Start file timer.
    _M_file_timer.expires_in(_M_acceptor.config().maxfileage * 1000 * 1000);

    // Set timestamp of the file creation.
    _M_file_creation = time(nullptr);

    return true;
  }

  return false;
}

void receiver::connection::write_file(size_t len)
{
  // Lock file mutex.
  while (::InterlockedCompareExchange(&_M_file_mutex, 1, 0) != 0);

  // If the file has not been opened yet...
  if (!_M_file.open()) {
    // Open file.
    if (!open_file()) {
      // Unlock file mutex.
      ::InterlockedDecrement(&_M_file_mutex);

      // Close connection.
      close_connection();

      return;
    }
  }

  // Start an asynchronous write.
  _M_file.write(_M_buf, len);
}

void receiver::connection::close_file(bool cancel_file_timer)
{
  // If the file timer should be canceled...
  if (cancel_file_timer) {
    // Cancel file timer.
    _M_file_timer.cancel();
  }

  _M_file.close();
}

void receiver::connection::close_connection(bool cancel_connection_timer)
{
#if DEBUG
  print("Closing connection...\n");
#endif

  // If the connection timer should be canceled...
  if (cancel_connection_timer) {
    // Cancel connection timer.
    _M_connection_timer.cancel();
  }

  // Cancel outstanding requests.
  _M_sock.cancel(async::stream::socket::operation::receive);

  // Disconnect.
  _M_sock.disconnect();
}

void receiver::connection::error_writing_file()
{
  // Close file.
  close_file();

  // If the file is not empty...
  if (_M_filesize > 0) {
    // Move file to the final directory.
    move_file();
  } else {
    // Compose name of the file to be deleted.
    char pathname[MAX_PATH];
    snprintf(pathname,
             sizeof(pathname),
             "%s\\file-%zu-%zu.bin",
             _M_acceptor.config().tmpdir,
             _M_nconnection,
             _M_nfile);

    ::DeleteFile(pathname);
  }

  // Unlock file mutex.
  ::InterlockedDecrement(&_M_file_mutex);

  // Close connection.
  close_connection();
}

void receiver::connection::complete(async::stream::socket::operation op,
                                    DWORD error,
                                    DWORD transferred)
{
  // Success?
  if (error == 0) {
    switch (op) {
      case async::stream::socket::operation::receive:
        // If the connection mutex can be locked...
        if (::InterlockedCompareExchange(&_M_connection_mutex, 1, 0) == 0) {
          // Data has been received.
          received(transferred);

          // Unlock connection mutex.
          ::InterlockedDecrement(&_M_connection_mutex);
        }

        break;
      case async::stream::socket::operation::disconnect:
#if DEBUG
        print("Disconnected.\n");
#endif

        // Disconnected.
        disconnected();

        break;
      case async::stream::socket::operation::accept:
        // Connection has been accepted.
        accepted();

        break;
      case async::stream::socket::operation::connect:
      case async::stream::socket::operation::send:
      default:
        break;
    }
  } else {
    print("I/O failed (error %lu).\n", error);

    switch (op) {
      case async::stream::socket::operation::receive:
        if (error != WSA_OPERATION_ABORTED) {
          // Close connection.
          close_connection();
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
      case async::stream::socket::operation::send:
      default:
        break;
    }
  }
}

void receiver::connection::complete(DWORD error, DWORD transferred)
{
  // Success?
  if (error == 0) {
    // Data has been written to the file.
    written(transferred);
  } else {
    // Error writing to file.
    error_writing_file();
  }
}

void receiver::connection::accepted()
{
#if DEBUG
  // Get remote address.
  socket::address addr;
  _M_sock.remote(_M_addresses, address_length, addr);

  char s[UNIX_PATH_MAX];
  if (addr.to_string(s, sizeof(s))) {
    print("Received connection from '%s'.\n", s);
  }
#endif // DEBUG

  // Start an asynchronous read.
  receive();
}

void receiver::connection::receive()
{
  // Set connection timer.
  _M_connection_timer.expires_in(_M_acceptor.config().timeout * 1000 * 1000);

  // Start an asynchronous read.
  _M_sock.receive(_M_buf, sizeof(_M_buf));
}

void receiver::connection::received(DWORD transferred)
{
#if DEBUG
  print("Received %lu byte(s).\n", transferred);
#endif

  // If some data has been received...
  if (transferred > 0) {
#if DEBUG
    print("%.*s\n",
          static_cast<int>(transferred),
          reinterpret_cast<const char*>(_M_buf));
#endif // DEBUG

    // Write to file.
    write_file(transferred);
  } else {
    // Close connection.
    close_connection();
  }
}

void receiver::connection::written(DWORD count)
{
  // Increment file size.
  _M_filesize += count;

#if DEBUG
  char pathname[MAX_PATH];
  snprintf(pathname,
           sizeof(pathname),
           "%s\\file-%zu-%zu.bin",
           _M_acceptor.config().tmpdir,
           _M_nconnection,
           _M_nfile);

  print("Successfully written %lu byte(s) to the file '%s' (file size: %llu)."
        "\n",
        count,
        pathname,
        _M_filesize);
#endif // DEBUG

  // If the file is too big or too old...
  if ((_M_filesize >= _M_acceptor.config().maxfilesize) ||
      (_M_file_creation + _M_acceptor.config().maxfileage <=
       static_cast<uint64_t>(time(nullptr)))) {
    // Close file.
    close_file();

    // Move file to the final directory.
    move_file();
  }

  // Unlock file mutex.
  ::InterlockedDecrement(&_M_file_mutex);

  // Start an asynchronous read.
  receive();
}

void receiver::connection::disconnected()
{
  // Start another asynchronous accept.
  accept();
}

bool receiver::connection::move_file()
{
  // Compose name of the old file.
  char oldpath[MAX_PATH];
  snprintf(oldpath,
           sizeof(oldpath),
           "%s\\file-%zu-%zu.bin",
           _M_acceptor.config().tmpdir,
           _M_nconnection,
           _M_nfile);

  // Compose name of the new file.
  char newpath[MAX_PATH];
  snprintf(newpath,
           sizeof(newpath),
           "%s\\file-%zu-%zu.bin",
           _M_acceptor.config().finaldir,
           _M_nconnection,
           _M_nfile);

#if DEBUG
  print("Moving file '%s' -> '%s'.\n", oldpath, newpath);
#endif

  // Move file.
  return (::MoveFileEx(oldpath, newpath, MOVEFILE_REPLACE_EXISTING) == TRUE);
}

void receiver::connection::connection_timer()
{
  // If the connection mutex can be locked...
  if (::InterlockedCompareExchange(&_M_connection_mutex, 1, 0) == 0) {
#if DEBUG
    print("[Connection timer] About to close the connection.\n");
#endif

    // Close connection.
    // Do not cancel the connection timer, otherwise this function won't be
    // further executed.
    static constexpr const bool cancel_connection_timer = false;
    close_connection(cancel_connection_timer);

    // Unlock connection mutex.
    ::InterlockedDecrement(&_M_connection_mutex);
  }
}

void receiver::connection::file_timer()
{
  // If the file mutex can be locked...
  if (::InterlockedCompareExchange(&_M_file_mutex, 1, 0) == 0) {
#if DEBUG
    print("[File timer] About to close and move file.\n");
#endif

    // Close file.
    // Do not cancel the file timer, otherwise this function won't be further
    // executed.
    static constexpr const bool cancel_file_timer = false;
    close_file(cancel_file_timer);

    // Move file to the final directory.
    move_file();

    // Unlock file mutex.
    ::InterlockedDecrement(&_M_file_mutex);
  }
}

void receiver::connection::complete(async::stream::socket::operation op,
                                    DWORD error,
                                    DWORD transferred,
                                    void* user)
{
  static_cast<connection*>(user)->complete(op, error, transferred);
}

void receiver::connection::complete(filesystem::async::file& file,
                                    DWORD error,
                                    DWORD transferred,
                                    void* user)
{
  static_cast<connection*>(user)->complete(error, transferred);
}

void receiver::connection::connection_timer(util::timer& timer, void* user)
{
  static_cast<connection*>(user)->connection_timer();
}

void receiver::connection::file_timer(util::timer& timer, void* user)
{
  static_cast<connection*>(user)->file_timer();
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Acceptor.                                                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

receiver::acceptor::acceptor(const configuration& config,
                             PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{nullptr, nullptr, callbackenv},
    _M_config{config}
{
}

receiver::acceptor::~acceptor()
{
  if (_M_connections) {
    for (size_t i = 0; i < _M_nconnections; i++) {
      delete _M_connections[i];
    }

    free(_M_connections);
  }
}

bool receiver::acceptor::listen(const socket::address& addr,
                                size_t nacceptor,
                                PTP_CALLBACK_ENVIRON callbackenv)
{
  // Listen.
  if (_M_sock.listen(addr)) {
    _M_connections = static_cast<connection**>(
                       malloc(_M_config.nconnections * sizeof(connection*))
                     );

    if (_M_connections) {
      // Connection number.
      size_t nconnection = nacceptor * _M_config.nconnections;

      for (_M_nconnections = 0;
           _M_nconnections < _M_config.nconnections;
           _M_nconnections++, nconnection++) {
        // Create connection.
        connection* const conn = new (std::nothrow) connection{*this,
                                                               nconnection,
                                                               callbackenv};

        // If the connection could be created...
        if (conn) {
          if (conn->create()) {
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

      return true;
    }
  }

  return false;
}

async::stream::socket& receiver::acceptor::socket()
{
  return _M_sock;
}

const receiver::configuration& receiver::acceptor::config() const
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

receiver::acceptors::~acceptors()
{
  if (_M_acceptors) {
    for (size_t i = 0; i < _M_used; i++) {
      delete _M_acceptors[i];
    }

    free(_M_acceptors);
  }
}

bool receiver::acceptors::listen(const socket::address& addr,
                                 const configuration& config,
                                 PTP_CALLBACK_ENVIRON callbackenv)
{
  // If space for a new acceptor can be allocated...
  if (allocate()) {
    // Create acceptor.
    receiver::acceptor* const
      acceptor = new (std::nothrow) receiver::acceptor{config, callbackenv};

    // If the acceptor could be created...
    if (acceptor) {
      // Listen.
      if (acceptor->listen(addr, _M_used, callbackenv)) {
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

bool receiver::acceptors::allocate()
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