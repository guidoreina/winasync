#pragma once

#include <stdint.h>
#include <time.h>
#include "net/async/thread_pool.hpp"
#include "net/async/stream/socket.hpp"
#include "filesystem/async/file.hpp"
#include "util/timer.hpp"

namespace net {
namespace tcp {

// TCP receiver.
class receiver {
  public:
    // Minimum number of connections per acceptor.
    static constexpr const size_t min_connections = 1;

    // Maximum number of connections per acceptor.
    static constexpr const size_t max_connections = 4096;

    // Default number of connections per acceptor.
    static constexpr const size_t default_connections = 256;

    // Minimum connection timeout (seconds).
    static constexpr const uint64_t min_timeout = 5;

    // Maximum connection timeout (seconds).
    static constexpr const uint64_t max_timeout = 24 * 60 * 60;

    // Default connection timeout (seconds).
    static constexpr const uint64_t default_timeout = 30;

    // Minimum file size (bytes).
    static constexpr const uint64_t min_file_size = 4 * 1024;

    // Maximum file size (bytes).
    static constexpr const uint64_t max_file_size = 1024ull * 1024ull * 1024ull;

    // Default file size (bytes).
    static constexpr const
      uint64_t default_file_size = 32ull * 1024ull * 1024ull;

    // Minimum file age (seconds).
    static constexpr const uint64_t min_file_age = 1;

    // Maximum file age (seconds).
    static constexpr const uint64_t max_file_age = 3600;

    // Default file age (seconds).
    static constexpr const uint64_t default_file_age = 300;

    // Constructor.
    receiver() = default;

    // Destructor.
    ~receiver() = default;

    // Create.
    bool create(const char* tmpdir,
                const char* finaldir,
                DWORD minthreads = async::thread_pool::min_threads,
                DWORD maxthreads = async::thread_pool::default_max_threads,
                size_t nconnections = default_connections,
                uint64_t timeout = default_timeout,
                uint64_t maxfilesize = default_file_size,
                uint64_t maxfileage = default_file_age);

    // Listen.
    bool listen(const socket::address& addr);

  private:
    // Thread pool.
    async::thread_pool _M_thread_pool;

    // Configuration.
    struct configuration {
      // Number of connections per acceptor.
      size_t nconnections;

      // Directory where to store the temporary files.
      char tmpdir[MAX_PATH];

      // Directory where to store the final files.
      char finaldir[MAX_PATH];

      // Connection timeout (seconds).
      uint64_t timeout;

      // Maximum file size (bytes).
      uint64_t maxfilesize;

      // Maximum file age (seconds).
      uint64_t maxfileage;
    };

    configuration _M_config;

    // Forward declaration.
    class acceptor;

    // Connection.
    class connection {
      public:
        // Constructor.
        connection(acceptor& acceptor,
                   size_t nconnection,
                   PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Destructor.
        ~connection() = default;

        // Create connection.
        bool create();

        // Accept connection.
        void accept();

      private:
        // Address length.
        static constexpr const
          DWORD address_length = sizeof(struct sockaddr_storage) + 16;

        // Buffer size.
        static constexpr const size_t buffer_size = 32 * 1024;

        // Socket.
        async::stream::socket _M_sock;

        // Buffer for storing the local and remote addresses.
        uint8_t _M_addresses[2 * address_length];

        // Acceptor.
        acceptor& _M_acceptor;

        // File.
        filesystem::async::file _M_file;

        // Connection timer.
        util::timer _M_connection_timer;

        // File timer.
        util::timer _M_file_timer;

        // Connection mutex.
        uint32_t _M_connection_mutex = 0;

        // File mutex.
        uint32_t _M_file_mutex = 0;

        // Connection number.
        size_t _M_nconnection;

        // File number.
        size_t _M_nfile = 0;

        // Timestamp of the file creation.
        time_t _M_file_creation;

        // File size.
        uint64_t _M_filesize;

        // Buffer.
        uint8_t _M_buf[buffer_size];

        // Callback environment.
        PTP_CALLBACK_ENVIRON _M_callbackenv;

        // Open file.
        bool open_file();

        // Write to file.
        void write_file(size_t len);

        // Close file.
        void close_file(bool cancel_file_timer = true);

        // Close connection.
        void close_connection(bool cancel_connection_timer = true);

        // Error writing to file.
        void error_writing_file();

        // Notify of a completed socket I/O operation.
        void complete(async::stream::socket::operation op,
                      DWORD error,
                      DWORD transferred);

        // Notify of a completed file I/O operation.
        void complete(DWORD error, DWORD transferred);

        // Accepted.
        void accepted();

        // Receive.
        void receive();

        // Data has been received.
        void received(DWORD transferred);

        // Data has been written to the file.
        void written(DWORD count);

        // Disconnected.
        void disconnected();

        // Move file to the final directory.
        bool move_file();

        // Connection timer.
        void connection_timer();

        // File timer.
        void file_timer();

        // Notify of a completed socket I/O operation.
        static void complete(async::stream::socket::operation op,
                             DWORD error,
                             DWORD transferred,
                             void* user);

        // Notify of a completed file I/O operation.
        static void complete(filesystem::async::file& file,
                             DWORD error,
                             DWORD transferred,
                             void* user);

        // Connection timer.
        static void connection_timer(util::timer& timer, void* user);

        // File timer.
        static void file_timer(util::timer& timer, void* user);

        // Disable copy constructor and assignment operator.
        connection(const connection&) = delete;
        connection& operator=(const connection&) = delete;
    };

    // Acceptor.
    class acceptor {
      public:
        // Constructor.
        acceptor(const configuration& config,
                 PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Destructor.
        ~acceptor();

        // Listen.
        bool listen(const socket::address& addr,
                    size_t nacceptor,
                    PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Get acceptor socket.
        async::stream::socket& socket();

        // Get configuration.
        const configuration& config() const;

      private:
        // Acceptor.
        async::stream::socket _M_sock;

        // Connections.
        connection** _M_connections = nullptr;

        // Number of connections.
        size_t _M_nconnections = 0;

        // Configuration.
        const configuration& _M_config;

        // Disable copy constructor and assignment operator.
        acceptor(const acceptor&) = delete;
        acceptor& operator=(const acceptor&) = delete;
    };

    // Acceptors.
    class acceptors {
      public:
        // Constructor.
        acceptors() = default;

        // Destructor.
        ~acceptors();

        // Listen.
        bool listen(const socket::address& addr,
                    const configuration& config,
                    PTP_CALLBACK_ENVIRON callbackenv = nullptr);

      private:
        // Allocation.
        static constexpr const size_t allocation = 8;

        // Acceptors.
        acceptor** _M_acceptors = nullptr;
        size_t _M_size = 0;
        size_t _M_used = 0;

        // Allocate.
        bool allocate();

        // Disable copy constructor and assignment operator.
        acceptors(const acceptors&) = delete;
        acceptors& operator=(const acceptors&) = delete;
    };

    // Acceptors.
    acceptors _M_acceptors;

    // Disable copy constructor and assignment operator.
    receiver(const receiver&) = delete;
    receiver& operator=(const receiver&) = delete;
};

} // namespace tcp
} // namespace net