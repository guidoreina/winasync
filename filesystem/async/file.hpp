#pragma once

#include <windows.h>

namespace filesystem {
namespace async {

// Asynchronous file.
class file {
  public:
    // Notify of a completed I/O operation.
    // Arguments:
    //   file&: file
    //   DWORD: error number
    //   DWORD: number of bytes transferred in the I/O operation
    //   void*: pointer to user data
    typedef void (*completefn)(file&, DWORD, DWORD, void*);

    // Constructor.
    file(completefn complete, void* user = nullptr);

    // Destructor.
    ~file();

    // Open mode.
    enum class mode {
      read,
      write
    };

    // Open file.
    bool open(const char* pathname,
              mode m,
              PTP_CALLBACK_ENVIRON callbackenv = nullptr);

    // Is the file open?
    bool open() const;

    // Close.
    void close();

    // Read.
    void read(void* buf, size_t len);

    // Write.
    void write(const void* buf, size_t len);

    // Cancel pending callbacks.
    void cancel();

  protected:
    // File handle.
    HANDLE _M_file = INVALID_HANDLE_VALUE;

    // Overlapped structure.
    OVERLAPPED _M_overlapped;

    // I/O completion object.
    PTP_IO _M_io = nullptr;

    // Completion callback.
    const completefn _M_complete;

    // Pointer to user data.
    void* _M_user;

    // I/O completion callback.
    static void CALLBACK io_completion_callback(PTP_CALLBACK_INSTANCE instance,
                                                void* context,
                                                void* overlapped,
                                                ULONG result,
                                                ULONG_PTR transferred,
                                                PTP_IO io);

    // Disable copy constructor and assignment operator.
    file(const file&) = delete;
    file& operator=(const file&) = delete;
};

} // namespace async
} // namespace filesystem