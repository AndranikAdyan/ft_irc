*This project has been created as part of the 42 curriculum by <login>.*

# Description

`ft_irc` is a C++98 IRC server. Its goal is to provide a small TCP/IP IRC service that can be used by a real IRC client. The server accepts multiple clients through one non-blocking `poll()` loop and implements authentication, registration, channels, messaging, topics, and channel operator commands.

The implementation uses standard C++98 containers and a per-client input buffer. Socket reads and writes are performed only after the relevant descriptor is reported by `poll()`, and all client output is queued to keep slow clients from blocking the server.

# Instructions

Build with:

```sh
make
```

Run with a listening port and connection password:

```sh
./ircserv <port> <password>
```

Clients must send `PASS`, `NICK`, and `USER` before using IRC commands. The server supports `JOIN`, `PART`, `PRIVMSG`, `NOTICE`, `TOPIC`, `INVITE`, `KICK`, `MODE`, `PING`, and `QUIT`.

For HexChat, create a network entry with server `127.0.0.1`, the selected port, and the server password. For Irssi, connect with:

```text
/connect -password secret 127.0.0.1 6667
```

Replace `secret` and `6667` with the password and port used to start `ircserv`.

# Resources

- RFC 2812, Internet Relay Chat: Client Protocol: https://www.rfc-editor.org/rfc/rfc2812
- `poll(2)`, `socket(2)`, and `fcntl(2)` system documentation
- The 42 ft_irc subject file in this repository

AI was used for specific implementation and review tasks: structuring the C++98 `Server` and `Client` classes, implementing the single `poll()`-driven socket loop in `IrcServer.cpp`, implementing per-client command aggregation in `Client.hpp`, adding IRC registration/channel/operator command handling, preparing the Makefile and README, and checking the code against the subject's non-blocking I/O and C++98 constraints. The project authors remain responsible for testing and reviewing the submitted code.
