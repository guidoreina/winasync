#include <stdlib.h>
#include <stdio.h>
#include "net/tcp/receiver.hpp"
#include "net/library.hpp"

static BOOL WINAPI signal_handler(DWORD control_type);

static HANDLE stop_event = nullptr;

int main(int argc, const char* argv[])
{
  // Check usage.
  if (argc == 4) {
    // Initiate use of the Winsock DLL.
    net::library library;
    if (library.init()) {
      // Build socket address.
      net::socket::address addr;
      if (addr.build(argv[1])) {
        // Load functions.
        if (net::async::stream::socket::load_functions()) {
          // Create event.
          stop_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

          // If the event could be created...
          if (stop_event) {
            // Install signal handler.
            if (::SetConsoleCtrlHandler(signal_handler, TRUE)) {
              // Create receiver.
              net::tcp::receiver receiver;
              if (receiver.create(argv[2], argv[3])) {
                // Listen.
                if (receiver.listen(addr)) {
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
                fprintf(stderr, "Error creating TCP receiver.\n");
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
        fprintf(stderr, "Error building socket address '%s'.\n", argv[1]);
      }
    } else {
      fprintf(stderr, "Error initiating use of the Winsock DLL.\n");
    }
  } else {
    fprintf(stderr, "Usage: %s <address> <temp-dir> <final-dir>\n", argv[0]);
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