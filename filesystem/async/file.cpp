#include "filesystem/async/file.hpp"

namespace filesystem {
namespace async {

file::file(completefn complete, void* user)
  : _M_complete{complete},
    _M_user{user}
{
}

file::~file()
{
  // Close file.
  close();
}

bool file::open(const char* pathname, mode m, PTP_CALLBACK_ENVIRON callbackenv)
{
  // Open file for reading?
  if (m == mode::read) {
    // Open file for reading.
    _M_file = ::CreateFile(pathname,
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                           nullptr);
  } else {
    // Open file for writing.
    _M_file = ::CreateFile(pathname,
                           GENERIC_WRITE,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL |
                           FILE_APPEND_DATA |
                           FILE_FLAG_OVERLAPPED,
                           nullptr);
  }

  // If the file could be opened..
  if (_M_file != INVALID_HANDLE_VALUE) {
    // Do not queue completion packets to the I/O completion port when
    // I/O operations complete immediately.
    static constexpr const UCHAR flags = FILE_SKIP_COMPLETION_PORT_ON_SUCCESS;
    if (::SetFileCompletionNotificationModes(_M_file, flags)) {
      // Create I/O completion object.
      _M_io = ::CreateThreadpoolIo(_M_file,
                                   io_completion_callback,
                                   this,
                                   callbackenv);

      // If the I/O completion object could be created...
      if (_M_io) {
        // Clear overlapped structure.
        memset(&_M_overlapped, 0, sizeof(OVERLAPPED));

        return true;
      }
    }
  }

  return false;
}

bool file::open() const
{
  return (_M_file != INVALID_HANDLE_VALUE);
}

void file::close()
{
  // Cancel pending callbacks.
  cancel();

  if (_M_io) {
    // Release I/O completion object.
    ::CloseThreadpoolIo(_M_io);
    _M_io = nullptr;
  }

  if (_M_file != INVALID_HANDLE_VALUE) {
    // Close file.
    ::CloseHandle(_M_file);
    _M_file = INVALID_HANDLE_VALUE;
  }
}

void file::read(void* buf, size_t len)
{
  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  DWORD count;
  if (::ReadFile(_M_file, buf, len, &count, &_M_overlapped)) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    _M_complete(*this, 0, count, _M_user);
  } else {
    // Get error code.
    const DWORD error = ::GetLastError();

    if (error != ERROR_IO_PENDING) {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      _M_complete(*this, error, count, _M_user);
    }
  }
}

void file::write(const void* buf, size_t len)
{
  // Notify the thread pool that an I/O operation might begin.
  ::StartThreadpoolIo(_M_io);

  // Write at the end of the file.
  _M_overlapped.Offset = 0xffffffff;
  _M_overlapped.OffsetHigh = 0xffffffff;

  DWORD count;
  if (::WriteFile(_M_file, buf, len, &count, &_M_overlapped)) {
    // Cancel notification.
    ::CancelThreadpoolIo(_M_io);

    _M_complete(*this, 0, count, _M_user);
  } else {
    // Get error code.
    const DWORD error = ::GetLastError();

    if (error != ERROR_IO_PENDING) {
      // Cancel notification.
      ::CancelThreadpoolIo(_M_io);

      _M_complete(*this, error, count, _M_user);
    }
  }
}

void file::cancel()
{
  if (_M_file != INVALID_HANDLE_VALUE) {
    ::CancelIoEx(_M_file, &_M_overlapped);
  }
}

void CALLBACK file::io_completion_callback(PTP_CALLBACK_INSTANCE instance,
                                           void* context,
                                           void* overlapped,
                                           ULONG result,
                                           ULONG_PTR transferred,
                                           PTP_IO io)
{
  file* const f = static_cast<file*>(context);
  f->_M_complete(*f, result, transferred, f->_M_user);
}

} // namespace async
} // namespace filesystem