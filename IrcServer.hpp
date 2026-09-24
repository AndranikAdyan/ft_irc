#ifndef IRC_SERVER_HPP
#define IRC_SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include <poll.h>

#include "Channel.hpp"
#include "Client.hpp"

class IrcServer {
public:
    IrcServer(unsigned short port, const std::string &password);
    ~IrcServer();

    bool start();
    void run();
    void stop();

private:
    IrcServer(const IrcServer &);
    IrcServer &operator=(const IrcServer &);

    int _listenFd;
    unsigned short _port;
    std::string _password;
    bool _running;
    std::map<int, Client> _clients;
    std::map<std::string, Channel> _channels;

    bool setupSocket();
    void acceptClient();
    void handleReadable(int fd);
    void handleWritable(int fd);
    void processInput(Client &client);
    void processLine(int fd, const std::string &line);
    void handleCommand(int fd, const std::string &command,
                       const std::vector<std::string> &params,
                       const std::string &trailing);
    void handleRegistration(int fd, const std::string &command,
                            const std::vector<std::string> &params,
                            const std::string &trailing);
    void handleJoin(int fd, const std::vector<std::string> &params);
    void handlePart(int fd, const std::vector<std::string> &params,
                    const std::string &trailing);
    void handlePrivmsg(int fd, const std::vector<std::string> &params,
                       const std::string &trailing);
    void handleKick(int fd, const std::vector<std::string> &params,
                    const std::string &trailing);
    void handleInvite(int fd, const std::vector<std::string> &params);
    void handleTopic(int fd, const std::vector<std::string> &params,
                     const std::string &trailing);
    void handleMode(int fd, const std::vector<std::string> &params);

    void queue(int fd, const std::string &message);
    void numeric(int fd, const std::string &code,
                 const std::string &message);
    void broadcast(const Channel &channel, int exceptFd,
                   const std::string &message);
    void removeClient(int fd, const std::string &reason);
    void removeFromChannel(int fd, const std::string &channelName,
                           const std::string &reason, bool announce);
    bool hasClient(int fd) const;
    bool validChannelName(const std::string &name) const;
    bool isOperator(const Channel &channel, int fd) const;
    int findNickname(const std::string &nickname) const;
    std::string prefix(const Client &client) const;
    static std::string upper(const std::string &value);
    static std::vector<std::string> splitWords(const std::string &line,
                                                std::string &trailing);
};

#endif
