# async
Windows IOCP classes and test programs.

The address for the test programs has the format `<ip-address>:<port>` (Unix sockets are also supported).


## `tcp-proxy.exe`
`tcp-proxy.exe` is a protocol agnostic TCP proxy. Whenever it receives a new connection, it opens a new connection to the remote host and transfers data from one socket to the another.

```
Usage: tcp-proxy.exe <local-address> <remote-address>
```


## `tcp-receiver.exe`
`tcp-receiver.exe` listens on the given address and port and saves the received data on files in a temporary directory. When a file has reached 32 MiB of size or after 5 minutes, the file is closed and moved to the final directory.

```
Usage: tcp-receiver.exe <address> <temp-dir> <final-dir>
```


## `test-connector.exe`
`test-connector.exe` opens several connections to a host and sends data in a loop.

```
Usage: test-connector.exe [OPTIONS] --address <address> (--file <filename> | --data <number-bytes>)

Options:
  --help
  --number-connections <number-connections>
  --number-transfers-per-connection <number-transfers-per-connection>
  --number-loops <number-loops>

Valid values:
  <number-connections> ::= 1 .. 4096 (default: 4)
  <number-transfers-per-connection> ::= 1 .. 1000000 (default: 1)
  <number-loops> ::= 1 .. 1000000 (default: 1)
  <number-bytes> ::= 1 .. 67108864
```