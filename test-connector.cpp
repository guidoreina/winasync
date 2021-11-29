#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <share.h>
#include <limits.h>
#include <new>
#include "net/async/thread_pool.hpp"
#include "net/async/stream/socket.hpp"
#include "net/library.hpp"

static BOOL WINAPI signal_handler(DWORD control_type);

static HANDLE stop_event = nullptr;


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Configuration.                                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class configuration {
  public:
    // Constructor.
    configuration() = default;

    // Destructor.
    ~configuration();

    // Parse.
    bool parse(int argc, const char* argv[]);

    // Get address to connect to.
    const net::socket::address& address() const;

    // Get number of connections.
    size_t number_connections() const;

    // Get number of transfers per connection.
    unsigned number_transfers_per_connection() const;

    // Get number of loops.
    unsigned number_loops() const;

    // Get data to be sent.
    const void* data() const;

    // Get length of the data to be sent.
    size_t length() const;

  private:
    // Minimum number of connections.
    static constexpr const size_t min_connections = 1;

    // Maximum number of connections.
    static constexpr const size_t max_connections = 4096;

    // Default number of connections.
    static constexpr const size_t default_connections = 4;

    // Minimum number of transfers per connection.
    static constexpr const unsigned min_transfers = 1;

    // Maximum number of transfers per connection.
    static constexpr const unsigned max_transfers = 1000 * 1000;

    // Default number of transfers per connection.
    static constexpr const unsigned default_transfers = 1;

    // Minimum number of loops.
    static constexpr const unsigned min_loops = 1;

    // Maximum number of loops.
    static constexpr const unsigned max_loops = 1000 * 1000;

    // Default number of loops.
    static constexpr const unsigned default_loops = 1;

    // Minimum number of bytes to be transferred.
    static constexpr const size_t min_data_transfer = 1;

    // Maximum number of bytes to be transferred.
    static constexpr const size_t max_data_transfer = 64ul * 1024ul * 1024ul;

    // Address to connect to.
    net::socket::address _M_address;

    // Number of connections.
    size_t _M_nconnections;

    // Number of transfers per connection.
    unsigned _M_ntransfers;

    // Number of loops.
    unsigned _M_nloops;

    // Data to be sent.
    uint8_t* _M_data = nullptr;

    // Length of the data to be sent.
    size_t _M_length;

    // Load file.
    bool load_file(const char* filename);

    // Print usage.
    static void usage(const char* program);

    // Parse number.
    static bool parse(const char* s,
                      uint64_t& n,
                      uint64_t min = 0,
                      uint64_t max = ULLONG_MAX);

    // Disable copy constructor and assignment operator.
    configuration(const configuration&) = delete;
    configuration& operator=(const configuration&) = delete;
};

configuration::~configuration()
{
  if (_M_data) {
    free(_M_data);
  }
}

bool configuration::parse(int argc, const char* argv[])
{
  const char* address = nullptr;
  _M_nconnections = default_connections;
  _M_ntransfers = default_transfers;
  _M_nloops = default_loops;

  int i = 1;
  while (i < argc) {
    if (_stricmp(argv[i], "--address") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        // If the address has not been provided yet...
        if (!address) {
          // Build address.
          if (_M_address.build(argv[i + 1])) {
            address = argv[i + 1];

            i += 2;
          } else {
            fprintf(stderr, "Invalid address '%s'.\n", argv[i + 1]);
            return false;
          }
        } else {
          fprintf(stderr, "\"--address\" has been already provided.\n");
          return false;
        }
      } else {
        fprintf(stderr, "Expected argument after \"--address\".\n");
        return false;
      }
    } else if (_stricmp(argv[i], "--number-connections") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        uint64_t n;
        if (parse(argv[i + 1], n, min_connections, max_connections)) {
          _M_nconnections = static_cast<size_t>(n);

          i += 2;
        } else {
          fprintf(stderr,
                  "Invalid number of connections '%s' "
                  "(valid range: %zu .. %zu).\n",
                  argv[i + 1],
                  min_connections,
                  max_connections);

          return false;
        }
      } else {
        fprintf(stderr, "Expected argument after \"--number-connections\".\n");
        return false;
      }
    } else if (_stricmp(argv[i], "--number-transfers-per-connection") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        uint64_t n;
        if (parse(argv[i + 1], n, min_transfers, max_transfers)) {
          _M_ntransfers = static_cast<unsigned>(n);

          i += 2;
        } else {
          fprintf(stderr,
                  "Invalid number of transfers '%s' (valid range: %u .. %u).\n",
                  argv[i + 1],
                  min_transfers,
                  max_transfers);

          return false;
        }
      } else {
        fprintf(stderr,
                "Expected argument after "
                "\"--number-transfers-per-connection\".\n");

        return false;
      }
    } else if (_stricmp(argv[i], "--number-loops") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        uint64_t n;
        if (parse(argv[i + 1], n, min_loops, max_loops)) {
          _M_nloops = static_cast<unsigned>(n);

          i += 2;
        } else {
          fprintf(stderr,
                  "Invalid number of loops '%s' (valid range: %u .. %u).\n",
                  argv[i + 1],
                  min_loops,
                  max_loops);

          return false;
        }
      } else {
        fprintf(stderr, "Expected argument after \"--number-loops\".\n");
        return false;
      }
    } else if (_stricmp(argv[i], "--file") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        // If the `_M_data` buffer has not been created yet...
        if (!_M_data) {
          // Load file.
          if (load_file(argv[i + 1])) {
            i += 2;
          } else {
            return false;
          }
        } else {
          fprintf(stderr,
                  "\"--file\" or \"--data\" has been already provided.\n");

          return false;
        }
      } else {
        fprintf(stderr, "Expected argument after \"--file\".\n");
        return false;
      }
    } else if (_stricmp(argv[i], "--data") == 0) {
      // If not the last argument...
      if (i + 1 < argc) {
        // If the `_M_data` buffer has not been created yet...
        if (!_M_data) {
          uint64_t n;
          if (parse(argv[i + 1], n, min_data_transfer, max_data_transfer)) {
            _M_length = static_cast<size_t>(n);

            // Allocate memory.
            _M_data = static_cast<uint8_t*>(malloc(_M_length));

            // If the data could be allocated...
            if (_M_data) {
              memset(_M_data, '0', _M_length);

              i += 2;
            } else {
              fprintf(stderr, "Error allocating memory.\n");
              return false;
            }
          } else {
            fprintf(stderr,
                    "Invalid data transfer '%s' (valid range: %zu .. %zu).\n",
                    argv[i + 1],
                    min_data_transfer,
                    max_data_transfer);

            return false;
          }
        } else {
          fprintf(stderr,
                  "\"--file\" or \"--data\" has been already provided.\n");

          return false;
        }
      } else {
        fprintf(stderr, "Expected argument after \"--data\".\n");
        return false;
      }
    } else if (_stricmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return false;
    } else {
      fprintf(stderr, "Invalid option '%s'.\n", argv[i]);
      return false;
    }
  }

  // If at least one argument has been provided...
  if (argc > 1) {
    // If the address has been provided...
    if (address) {
      if (_M_data) {
        return true;
      } else {
        fprintf(stderr,
                "Either the argument \"--file\" or \"--data\" has to be "
                "provided.\n");
      }
    } else {
      fprintf(stderr, "Argument \"--address\" has to be provided.\n");
    }
  } else {
    usage(argv[0]);
  }

  return false;
}

const net::socket::address& configuration::address() const
{
  return _M_address;
}

size_t configuration::number_connections() const
{
  return _M_nconnections;
}

unsigned configuration::number_transfers_per_connection() const
{
  return _M_ntransfers;
}

unsigned configuration::number_loops() const
{
  return _M_nloops;
}

const void* configuration::data() const
{
  return _M_data;
}

size_t configuration::length() const
{
  return _M_length;
}

bool configuration::load_file(const char* filename)
{
  // If `filename` exists and is a regular file...
  struct _stat64 sbuf;
  if ((_stat64(filename, &sbuf) == 0) && ((sbuf.st_mode & _S_IFREG) != 0)) {
    // If the file is neither too small nor too big...
    if ((sbuf.st_size >= static_cast<int64_t>(min_data_transfer)) &&
        (sbuf.st_size <= static_cast<int64_t>(max_data_transfer))) {
      // Open file for reading.
      int fd;
      if (_sopen_s(&fd,
                   filename,
                   _O_RDONLY | _O_BINARY,
                   _SH_DENYNO,
                   0) == 0) {
        // Allocate memory.
        _M_data = static_cast<uint8_t*>(malloc(sbuf.st_size));

        // If the data could be allocated...
        if (_M_data) {
          // Read file.
          _M_length = 0;
          do {
            // Read from file.
            const int ret = _read(fd,
                                  _M_data + _M_length,
                                  sbuf.st_size - _M_length);

            if (ret > 0) {
              _M_length += ret;

              // If we have read the whole file...
              if (static_cast<int64_t>(_M_length) == sbuf.st_size) {
                _close(fd);

                return true;
              }
            } else if (ret < 0) {
              fprintf(stderr, "Error reading from '%s'.\n", filename);

              _close(fd);

              return false;
            }
          } while (true);
        } else {
          fprintf(stderr, "Error allocating memory.\n");
        }

        _close(fd);
      } else {
        fprintf(stderr, "Error opening file '%s' for reading.\n", filename);
      }
    } else {
      fprintf(stderr,
              "File size (%lld) out of range (valid range: %zu .. %zu).\n",
              sbuf.st_size,
              min_data_transfer,
              max_data_transfer);
    }
  } else {
    fprintf(stderr,
            "File '%s' doesn't exist or is not a regular file.\n",
            filename);
  }

  return false;
}

void configuration::usage(const char* program)
{
  fprintf(stderr,
          "Usage: %s [OPTIONS] --address <address> "
          "(--file <filename> | --data <number-bytes>)\n\n",
          program);

  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --help\n");
  fprintf(stderr, "  --number-connections <number-connections>\n");
  fprintf(stderr,
          "  --number-transfers-per-connection "
          "<number-transfers-per-connection>\n");

  fprintf(stderr, "  --number-loops <number-loops>\n\n");

  fprintf(stderr, "Valid values:\n");
  fprintf(stderr,
          "  <number-connections> ::= %zu .. %zu (default: %zu)\n",
          min_connections,
          max_connections,
          default_connections);

  fprintf(stderr,
          "  <number-transfers-per-connection> ::= %u .. %u (default: %u)\n",
          min_transfers,
          max_transfers,
          default_transfers);

  fprintf(stderr,
          "  <number-loops> ::= %u .. %u (default: %u)\n",
          min_loops,
          max_loops,
          default_loops);

  fprintf(stderr,
          "  <number-bytes> ::= %zu .. %zu\n",
          min_data_transfer,
          max_data_transfer);
}

bool configuration::parse(const char* s,
                          uint64_t& n,
                          uint64_t min,
                          uint64_t max)
{
  if (*s) {
    uint64_t res = 0;

    do {
      // Digit?
      if ((*s >= '0') && (*s <= '9')) {
        const uint64_t tmp = (res * 10) + (*s - '0');

        // If the number doesn't overflow and is not too big...
        if ((tmp >= res) && (tmp <= max)) {
          res = tmp;
        } else {
          return false;
        }
      } else {
        return false;
      }
    } while (*++s);

    // If the number is not too small...
    if (res >= min) {
      n = res;
      return true;
    }
  }

  return false;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Connection.                                                                //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class connection {
  public:
    // Constructor.
    connection(const configuration& config,
               uint32_t* nconnections,
               PTP_CALLBACK_ENVIRON callbackenv = nullptr);

    // Destructor.
    ~connection() = default;

    // Connect.
    void connect();

  private:
    // Buffer view.
    struct buffer_view {
      const uint8_t* data;
      DWORD length;
    };

    // Socket.
    net::async::stream::socket _M_sock;

    // Number of transfers per connection.
    unsigned _M_ntransfers;

    // Number of loops.
    unsigned _M_nloops = 0;

    // Send buffer view.
    buffer_view _M_sendbuf;

    // Number of connections.
    uint32_t* _M_nconnections;

    // Configuration.
    const configuration& _M_config;

    // Connected.
    void connected();

    // Send.
    void send(const void* buf, DWORD len);

    // Data has been sent.
    void sent(DWORD count);

    // Close connection.
    void close();

    // Disconnected.
    void disconnected();

    // Notify of a completed socket I/O operation.
    void complete(net::async::stream::socket::operation op,
                  DWORD error,
                  DWORD transferred);

    // Notify of a completed socket I/O operation.
    static void complete(net::async::stream::socket::operation op,
                         DWORD error,
                         DWORD transferred,
                         void* user);

    // Disable copy constructor and assignment operation.
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
};

connection::connection(const configuration& config,
                       uint32_t* nconnections,
                       PTP_CALLBACK_ENVIRON callbackenv)
  : _M_sock{complete, this, callbackenv},
    _M_nconnections{nconnections},
    _M_config{config}
{
}

void connection::connect()
{
  // Start an asynchronous connect.
  _M_sock.connect(_M_config.address());
}

void connection::connected()
{
  // Reset number of transfers per connection.
  _M_ntransfers = 0;

  // Start an asynchronous send.
  send(_M_config.data(), _M_config.length());
}

void connection::send(const void* buf, DWORD len)
{
  // Save view.
  _M_sendbuf.data = static_cast<const uint8_t*>(buf);
  _M_sendbuf.length = len;

  // Start an asynchronous send.
  _M_sock.send(buf, len);
}

void connection::sent(DWORD count)
{
  // If we have sent all the data...
  if (count == _M_sendbuf.length) {
    // If not the last transfer of the connection...
    if (++_M_ntransfers < _M_config.number_transfers_per_connection()) {
      // Start an asynchronous send.
      send(_M_config.data(), _M_config.length());
    } else {
      // Close connection.
      close();
    }
  } else {
    // Send the rest.
    send(_M_sendbuf.data + count, _M_sendbuf.length - count);
  }
}

void connection::close()
{
  // Cancel outstanding requests.
  _M_sock.cancel(net::async::stream::socket::operation::send);

  // Disconnect.
  _M_sock.disconnect();
}

void connection::disconnected()
{
  // If not the last loop...
  if (++_M_nloops < _M_config.number_loops()) {
    // Start an asynchronous connect.
    connect();
  } else {
    // Decrement number of running connections.
    if (::InterlockedDecrement(_M_nconnections) == 0) {
      ::SetEvent(stop_event);
    }
  }
}

void connection::complete(net::async::stream::socket::operation op,
                          DWORD error,
                          DWORD transferred)
{
  // Success?
  if (error == 0) {
    switch (op) {
      case net::async::stream::socket::operation::send:
        // Data has been sent.
        sent(transferred);

        break;
      case net::async::stream::socket::operation::disconnect:
        // Disconnected.
        disconnected();

        break;
      case net::async::stream::socket::operation::connect:
        // Connected.
        connected();

        break;
      case net::async::stream::socket::operation::accept:
      case net::async::stream::socket::operation::receive:
      default:
        break;
    }
  } else {
    switch (op) {
      case net::async::stream::socket::operation::send:
        if (error != WSA_OPERATION_ABORTED) {
          // Close connection.
          close();
        }

        break;
      case net::async::stream::socket::operation::disconnect:
        // Disconnected.
        disconnected();

        break;
      case net::async::stream::socket::operation::accept:
      case net::async::stream::socket::operation::connect:
      case net::async::stream::socket::operation::receive:
      default:
        break;
    }
  }
}

void connection::complete(net::async::stream::socket::operation op,
                          DWORD error,
                          DWORD transferred,
                          void* user)
{
  static_cast<connection*>(user)->complete(op, error, transferred);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Connections.                                                               //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class connections {
  public:
    // Constructor.
    connections() = default;

    // Destructor.
    ~connections();

    // Create.
    bool create(const configuration& config,
                PTP_CALLBACK_ENVIRON callbackenv = nullptr);

  private:
    // Connections.
    connection** _M_connections = nullptr;

    // Number of connections.
    size_t _M_nconnections;

    // Number of running connections.
    uint32_t _M_nrunning;

    // Disable copy constructor and assignment operator.
    connections(const connections&) = delete;
    connections& operator=(const connections&) = delete;
};

connections::~connections()
{
  if (_M_connections) {
    for (size_t i = 0; i < _M_nconnections; i++) {
      delete _M_connections[i];
    }

    free(_M_connections);
  }
}

bool connections::create(const configuration& config,
                         PTP_CALLBACK_ENVIRON callbackenv)
{
  const size_t nconnections = config.number_connections();

  _M_connections = static_cast<connection**>(
                     malloc(nconnections * sizeof(connection*))
                   );

  if (_M_connections) {
    // Create connections.
    for (_M_nconnections = 0;
         _M_nconnections < nconnections;
         _M_nconnections++) {
      _M_connections[_M_nconnections] =
        new (std::nothrow) connection{config, &_M_nrunning, callbackenv};

      if (!_M_connections[_M_nconnections]) {
        return false;
      }
    }

    _M_nrunning = static_cast<uint32_t>(_M_nconnections);

    // Connect.
    for (size_t i = 0; i < nconnections; i++) {
      _M_connections[i]->connect();
    }

    return true;
  }

  return false;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Main function.                                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char* argv[])
{
  // Parse command-line arguments.
  configuration config;
  if (config.parse(argc, argv)) {
    // Initiate use of the Winsock DLL.
    net::library library;
    if (library.init()) {
      // Load functions.
      if (net::async::stream::socket::load_functions()) {
        // Create thread pool.
        net::async::thread_pool thread_pool;
        if (thread_pool.create()) {
          // Create event.
          stop_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

          // If the event could be created...
          if (stop_event) {
            // Install signal handler.
            if (::SetConsoleCtrlHandler(signal_handler, TRUE)) {
              // Create connections.
              connections connections;
              if (connections.create(config)) {
                printf("Waiting for signal to arrive or tests to finish.\n");

                // Wait for signal to arrive or tests to finish.
                ::WaitForSingleObject(stop_event, INFINITE);

                ::CloseHandle(stop_event);

                printf("Exiting...\n");

                return EXIT_SUCCESS;
              } else {
                fprintf(stderr, "Error creating connections.\n");
              }
            } else {
              fprintf(stderr, "Error installing signal handler.\n");
            }

            ::CloseHandle(stop_event);
          } else {
            fprintf(stderr, "Error creating event.\n");
          }
        } else {
          fprintf(stderr, "Error creating thread pool.\n");
        }
      } else {
        fprintf(stderr, "Error loading functions.\n");
      }
    } else {
      fprintf(stderr, "Error initiating use of the Winsock DLL.\n");
    }
  }

  return EXIT_FAILURE;
}

BOOL WINAPI signal_handler(DWORD control_type)
{
  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
      ::SetEvent(stop_event);

      return TRUE;
    default:
      return FALSE;
  }
}