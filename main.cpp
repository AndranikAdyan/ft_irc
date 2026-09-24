#include "Server.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>

volatile sig_atomic_t g_stop = 0;

static void stopHandler(int) {
    g_stop = 1;
}

static bool parsePort(const char *value, unsigned short &port) {
    char *end = 0;
    long number = std::strtol(value, &end, 10);
    if (*value == '\0' || *end != '\0' || number < 1 || number > 65535)
        return false;
    port = static_cast<unsigned short>(number);
    return true;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }
    unsigned short port;
    if (!parsePort(argv[1], port) || std::strlen(argv[2]) == 0) {
        std::cerr << "Invalid port or empty password" << std::endl;
        return 1;
    }
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = stopHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, 0);
    sigaction(SIGTERM, &action, 0);
    signal(SIGPIPE, SIG_IGN);

    Server server(port, argv[2]);
    if (!server.start())
        return 1;
    server.run();
    return 0;
}
