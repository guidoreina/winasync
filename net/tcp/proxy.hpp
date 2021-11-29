#pragma once

#include <stdint.h>
#include "net/async/thread_pool.hpp"
#include "net/async/stream/socket.hpp"
#include "util/timer.hpp"

namespace net {
namespace tcp {

// TCP proxy.
class proxy {
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

    // Constructor.
    proxy() = default;

    // Destructor.
    ~proxy() = default;

    // Create.
    bool create(DWORD minthreads = async::thread_pool::min_threads,
                DWORD maxthreads = async::thread_pool::default_max_threads,
                size_t nconnections = default_connections,
                uint64_t timeout = default_timeout);

    // Listen.
    bool listen(const socket::address& local, const socket::address& remote);

  private:
    // Thread pool.
    async::thread_pool _M_thread_pool;

    // Configuration.
    struct configuration {
      // Number of connections per acceptor.
      size_t nconnections;

      // Connection timeout (seconds).
      uint64_t timeout;
    };

    configuration _M_config;

    // Forward declaration.
    class acceptor;

    // Connection.
    class connection {
      public:
        // Constructor.
        connection(acceptor& acceptor,
                   PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Destructor.
        ~connection() = default;

        // Create connection.
        bool create(PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Accept connection.
        void accept();

      private:
        // Buffer size.
        static constexpr const size_t buffer_size = 32 * 1024;

        // Buffer view.
        struct buffer_view {
          const uint8_t* data;
          DWORD length;
        };

        // Forward declaration.
        class client;

        // Server connection.
        class server {
          public:
            // Constructor.
            server(acceptor& acceptor,
                   client& client,
                   PTP_CALLBACK_ENVIRON callbackenv = nullptr);

            // Destructor.
            ~server() = default;

            // Create server connection.
            bool create(PTP_CALLBACK_ENVIRON callbackenv = nullptr);

            // Accept connection.
            void accept();

            // Client connected.
            void connected();

            // Receive.
            void receive();

            // Send.
            void send(const void* buf, DWORD len);

            // Close connections.
            void close_connections(bool cancel_timer = true);

            // Close server connection.
            void close(bool cancel_timer = true);

            // Either the client or the server has disconnected.
            void disconnected();

          private:
            // Address length.
            static constexpr const
              DWORD address_length = sizeof(struct sockaddr_storage) + 16;

            // Socket.
            async::stream::socket _M_sock;

            // Buffer for storing the local and remote addresses.
            uint8_t _M_addresses[2 * address_length];

            // Acceptor.
            acceptor& _M_acceptor;

            // Client.
            client& _M_client;

            // Receive buffer.
            uint8_t _M_recvbuf[buffer_size];

            // Send buffer view.
            buffer_view _M_sendbuf;

            // Number of open connections.
            uint32_t _M_nconnections;

            // Timer.
            util::timer _M_timer;

            // Is the connection open?
            bool _M_open = false;

            // Mutex.
            uint32_t _M_mutex = 0;

            // Notify of a completed socket I/O operation.
            void complete(async::stream::socket::operation op,
                          DWORD error,
                          DWORD transferred);

            // Connection has been accepted.
            void accepted();

            // Connect client.
            void connect();

            // Data has been received.
            void received(DWORD transferred);

            // Data has been sent.
            void sent(DWORD count);

            // Timer.
            void timer();

            // Start timer.
            void start_timer();

            // Stop timer.
            void stop_timer();

            // Notify of a completed socket I/O operation.
            static void complete(async::stream::socket::operation op,
                                 DWORD error,
                                 DWORD transferred,
                                 void* user);

            // Timer.
            static void timer(util::timer& t, void* user);

            // Disable copy constructor and assignment operator.
            server(const server&) = delete;
            server& operator=(const server&) = delete;
        };

        // Client connection.
        class client {
          public:
            // Constructor.
            client(server& server, PTP_CALLBACK_ENVIRON callbackenv = nullptr);

            // Destructor.
            ~client() = default;

            // Connect.
            void connect(const socket::address& addr);

            // Close connection.
            void close();

            // Receive.
            void receive();

            // Send.
            void send(const void* buf, DWORD len);

          private:
            // Socket.
            async::stream::socket _M_sock;

            // Server.
            server& _M_server;

            // Receive buffer.
            uint8_t _M_recvbuf[buffer_size];

            // Send buffer view.
            buffer_view _M_sendbuf;

            // Is the connection open?
            bool _M_open = false;

            // Mutex.
            uint32_t _M_mutex = 0;

            // Notify of a completed socket I/O operation.
            void complete(async::stream::socket::operation op,
                          DWORD error,
                          DWORD transferred);

            // Client connected.
            void connected();

            // Data has been received.
            void received(DWORD transferred);

            // Data has been sent.
            void sent(DWORD count);

            // Disconnected.
            void disconnected();

            // Notify of a completed socket I/O operation.
            static void complete(async::stream::socket::operation op,
                                 DWORD error,
                                 DWORD transferred,
                                 void* user);

            // Disable copy constructor and assignment operator.
            client(const client&) = delete;
            client& operator=(const client&) = delete;
        };

        // Server connection.
        server _M_server;

        // Client connection.
        client _M_client;

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
        bool listen(const socket::address& local,
                    const socket::address& remote,
                    PTP_CALLBACK_ENVIRON callbackenv = nullptr);

        // Get acceptor socket.
        async::stream::socket& socket();

        // Get remote address.
        const socket::address& remote() const;

        // Get configuration.
        const configuration& config() const;

      private:
        // Acceptor.
        async::stream::socket _M_sock;

        // Connections.
        connection** _M_connections = nullptr;

        // Number of connections.
        size_t _M_nconnections = 0;

        // Remote address to connect to.
        socket::address _M_remote;

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
        bool listen(const socket::address& local,
                    const socket::address& remote,
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
    proxy(const proxy&) = delete;
    proxy& operator=(const proxy&) = delete;
};

} // namespace tcp
} // namespace net