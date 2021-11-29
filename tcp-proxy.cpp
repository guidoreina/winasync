#include <stdlib.h>
#include <stdio.h>
#include "net/tcp/proxy.hpp"
#include "net/library.hpp"

static BOOL WINAPI signal_handler(DWORD control_type);

static HANDLE stop_event = nullptr;

int main(int argc, const char* argv[])
{
  // Check usage.
  if (argc == 3) {
    // Initiate use of the Winsock DLL.
    net::library library;
    if (library.init()) {
      // Parse local address.
      net::socket::address local;
      if (local.build(argv[1])) {
        // Parse remote address.
        net::socket::address remote;
        if (remote.build(argv[2])) {
          // Load functions.
          if (net::async::stream::socket::load_functions()) {
            // Create event.
            stop_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

            // If the event could be created...
            if (stop_event) {
              // Install signal handler.
              if (::SetConsoleCtrlHandler(signal_handler, TRUE)) {
                // Create proxy.
                net::tcp::proxy proxy;
                if (proxy.create()) {
                  // Listen.
                  if (proxy.listen(local, remote)) {
                    printf("Waiting for signal to arrive.\n");

                    // Wait for signal to arrive.
                    ::WaitForSingleObject(stop_event, INFINITE);

                    printf("Signal received.\n");

                    ::CloseHandle(stop_event);

                    return EXIT_SUCCESS;
                  } else {
                    fprintf(stderr, "Error listening on '%s'.\n", argv[1]);
                  }
                } else {
                  fprintf(stderr, "Error creating proxy.\n");
                }
              } else {
                fprintf(stderr, "Error installing signal handler.\n");
              }

              ::CloseHandle(stop_event);
            } else {
              fprintf(stderr, "Error creating event.\n");
            }
          } else {
            fprintf(stderr, "Error loading functions.\n");
          }
        } else {
          fprintf(stderr, "Invalid address '%s'.\n", argv[2]);
        }
      } else {
        fprintf(stderr, "Invalid address '%s'\n", argv[1]);
      }
    } else {
      fprintf(stderr, "Error initiating use of the Winsock DLL.\n");
    }
  } else {
    fprintf(stderr, "Usage: %s <local-address> <remote-address>\n", argv[0]);
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