#ifndef SERVER_HPP
#define SERVER_HPP

#include "IrcServer.hpp"

class Server : public IrcServer {
public:
    Server(unsigned short port, const std::string &password)
        : IrcServer(port, password) {}

private:
    Server(const Server &);
    Server &operator=(const Server &);
};

#endif
