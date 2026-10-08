#include "IrcServer.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static const std::size_t MAX_LINE = 512;
static const std::size_t MAX_OUTPUT = 65536;
extern volatile sig_atomic_t g_stop;

IrcServer::IrcServer(unsigned short port, const std::string &password)
    : _listenFd(-1), _port(port), _password(password), _creationDate(),
      _running(false) {
    std::time_t now = std::time(0);
    const char *date = std::ctime(&now);
    if (date != 0)
        _creationDate = date;
    if (!_creationDate.empty() && _creationDate[_creationDate.size() - 1] == '\n')
        _creationDate.erase(_creationDate.size() - 1);
}

IrcServer::~IrcServer() {
    stop();
}

bool IrcServer::setupSocket() {
    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0)
        return false;
    int reuse = 1;
    if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        return false;
    if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
        return false;
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(_port);
    if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0)
        return false;
    if (listen(_listenFd, 64) < 0)
        return false;
    return true;
}

bool IrcServer::start() {
    if (!setupSocket()) {
        if (_listenFd >= 0)
            close(_listenFd);
        _listenFd = -1;
        return false;
    }
    _running = true;
    std::cerr << "[INFO] Listening on 0.0.0.0:" << _port << std::endl;
    return true;
}

void IrcServer::stop() {
    if (!_running && _listenFd < 0)
        return;
    std::map<int, Client>::iterator it = _clients.begin();
    while (it != _clients.end()) {
        close(it->first);
        ++it;
    }
    _clients.clear();
    if (_listenFd >= 0)
        close(_listenFd);
    _listenFd = -1;
    _running = false;
}

void IrcServer::run() {
    while (_running) {
        if (g_stop)
            break;
        std::vector<struct pollfd> events;
        events.reserve(_clients.size() + 1);
        struct pollfd listener;
        listener.fd = _listenFd;
        listener.events = POLLIN;
        listener.revents = 0;
        events.push_back(listener);
        std::map<int, Client>::const_iterator it = _clients.begin();
        while (it != _clients.end()) {
            struct pollfd event;
            event.fd = it->first;
            event.events = POLLIN;
            if (!it->second.output.empty() && it->second.outputOffset < it->second.output.size())
                event.events |= POLLOUT;
            event.revents = 0;
            events.push_back(event);
            ++it;
        }
        int ready = poll(&events[0], events.size(), -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (events[0].revents & POLLIN)
            acceptClient();
        for (std::size_t i = 1; i < events.size(); ++i) {
            int fd = events[i].fd;
            if (!hasClient(fd))
                continue;
            short flags = events[i].revents;
            if (flags & (POLLERR | POLLHUP | POLLNVAL)) {
                removeClient(fd, "connection closed");
                continue;
            }
            if ((flags & POLLIN) && hasClient(fd))
                handleReadable(fd);
            if ((flags & POLLOUT) && hasClient(fd))
                handleWritable(fd);
        }
        std::map<int, Client>::iterator closing = _clients.begin();
        while (closing != _clients.end()) {
            if (closing->second.closeRequested) {
                int fd = closing->first;
                ++closing;
                removeClient(fd, "output buffer exceeded limit");
            } else {
                ++closing;
            }
        }
    }
    stop();
}

void IrcServer::acceptClient() {
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = accept(_listenFd, reinterpret_cast<struct sockaddr *>(&address), &length);
    if (fd < 0)
        return;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return;
    }
    _clients.insert(std::make_pair(fd, Client(fd)));
}

void IrcServer::handleReadable(int fd) {
    char buffer[4096];
    int received = recv(fd, buffer, sizeof(buffer), 0);
    if (received <= 0) {
        removeClient(fd, "connection closed");
        return;
    }
    Client &client = _clients.find(fd)->second;
    client.appendData(buffer, static_cast<std::size_t>(received));
    processInput(client);
    if (client.bufferSize() > 65536)
        client.discardIncompleteCommand();
}

void IrcServer::handleWritable(int fd) {
    Client &client = _clients.find(fd)->second;
    if (client.outputOffset >= client.output.size()) {
        client.output.clear();
        client.outputOffset = 0;
        return;
    }
    std::size_t remaining = client.output.size() - client.outputOffset;
    int sent = send(fd, client.output.data() + client.outputOffset, remaining, 0);
    if (sent <= 0) {
        removeClient(fd, "connection closed");
        return;
    }
    client.outputOffset += static_cast<std::size_t>(sent);
    if (client.outputOffset == client.output.size()) {
        client.output.clear();
        client.outputOffset = 0;
    }
}

void IrcServer::processInput(Client &client) {
    std::vector<std::string> commands = client.extractCommands();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].size() > MAX_LINE)
            continue;
        processLine(client.fd, commands[i]);
        if (!hasClient(client.fd))
            return;
    }
}

std::vector<std::string> IrcServer::splitWords(const std::string &line, std::string &trailing) {
    std::vector<std::string> words;
    trailing.clear();
    std::size_t pos = 0;
    while (pos < line.size() && line[pos] == ' ')
        ++pos;
    while (pos < line.size()) {
        if (line[pos] == ':') {
            trailing = line.substr(pos + 1);
            break;
        }
        std::size_t space = line.find(' ', pos);
        if (space == std::string::npos) {
            words.push_back(line.substr(pos));
            break;
        }
        words.push_back(line.substr(pos, space - pos));
        pos = space + 1;
        while (pos < line.size() && line[pos] == ' ')
            ++pos;
    }
    return words;
}

void IrcServer::processLine(int fd, const std::string &line) {
    std::string trailing;
    std::vector<std::string> words = splitWords(line, trailing);
    if (words.empty())
        return;
    std::string command = upper(words[0]);
    words.erase(words.begin());
    handleCommand(fd, command, words, trailing);
}

void IrcServer::handleCommand(int fd, const std::string &command,
                              const std::vector<std::string> &params,
                              const std::string &trailing) {
    if (command == "PASS" || command == "NICK" || command == "USER") {
        handleRegistration(fd, command, params, trailing);
        return;
    }
    if (command == "CAP") {
        if (!params.empty() && upper(params[0]) == "LS")
            queue(fd, ":ircserv CAP * LS :\r\n");
        return;
    }
    Client &client = _clients.find(fd)->second;
    if (!client.registered) {
        numeric(fd, "451", "You have not registered");
        return;
    }
    if (command == "PING") {
        queue(fd, ":ircserv PONG ircserv :" + (trailing.empty() ? (params.empty() ? "ircserv" : params[0]) : trailing) + "\r\n");
    } else if (command == "JOIN") handleJoin(fd, params, trailing);
    else if (command == "PART") handlePart(fd, params, trailing);
    else if (command == "PRIVMSG") handlePrivmsg(fd, params, trailing);
    else if (command == "NOTICE") handlePrivmsg(fd, params, trailing);
    else if (command == "KICK") handleKick(fd, params, trailing);
    else if (command == "INVITE") handleInvite(fd, params);
    else if (command == "TOPIC") handleTopic(fd, params, trailing);
    else if (command == "MODE") handleMode(fd, params);
    else if (command == "QUIT") removeClient(fd, trailing.empty() ? "Client Quit" : trailing);
    else numeric(fd, "421", command + " :Unknown command");
}

void IrcServer::handleRegistration(int fd, const std::string &command,
                                   const std::vector<std::string> &params,
                                   const std::string &trailing) {
    Client &client = _clients.find(fd)->second;
    if (command == "PASS") {
        if (params.empty() || params[0] != _password) numeric(fd, "464", ":Password incorrect");
        else client.passwordAccepted = true;
    } else if (command == "NICK") {
        if (params.empty()) { numeric(fd, "431", ":No nickname given"); return; }
        int existing = findNickname(params[0]);
        if (existing >= 0 && existing != fd) { numeric(fd, "433", params[0] + " :Nickname is already in use"); return; }
        client.nickname = params[0];
    } else {
        if (params.size() != 3 || trailing.empty()) { numeric(fd, "461", "USER :Not enough parameters"); return; }
        client.username = params[0];
        client.realname = trailing;
    }
    if (client.passwordAccepted && !client.nickname.empty() && !client.username.empty() && !client.registered) {
        client.registered = true;
        queue(fd, ":ircserv 001 " + client.nickname + " :Welcome to the IRC server!\r\n");
        queue(fd, ":ircserv 002 " + client.nickname + " :Your host is ircserv\r\n");
        queue(fd, ":ircserv 003 " + client.nickname + " :This server was created " + _creationDate + "\r\n");
        std::cerr << "[INFO] Client " << fd << " successfully registered as "
                  << client.username << std::endl;
    }
}

void IrcServer::handleJoin(int fd, const std::vector<std::string> &params,
                           const std::string &trailing) {
    if (params.empty()) {
        if (!trailing.empty())
            numeric(fd, "403", trailing + " :Cannot join channel");
        else
            numeric(fd, "461", "JOIN :Not enough parameters");
        return;
    }
    std::string name = params[0];
    if (!validChannelName(name)) { numeric(fd, "403", name + " :Cannot join channel"); return; }
    Client &client = _clients.find(fd)->second;
    Channel &channel = _channels.insert(std::make_pair(name, Channel(name))).first->second;
    if (channel.members.find(fd) != channel.members.end()) return;
    if (channel.inviteOnly && channel.invited.find(fd) == channel.invited.end()) { numeric(fd, "473", name + " :Invite-only channel"); return; }
    if (channel.hasKey && (params.size() < 2 || params[1] != channel.key)) { numeric(fd, "475", name + " :Bad channel key"); return; }
    if (channel.hasLimit && channel.members.size() >= channel.limit) { numeric(fd, "471", name + " :Channel is full"); return; }
    bool first = channel.members.empty();
    channel.members.insert(fd);
    if (first) channel.operators.insert(fd);
    channel.invited.erase(fd);
    client.channels.insert(name);
    std::cerr << "[INFO] User " << client.username << " added to " << name << std::endl;
    std::string message = ":" + prefix(client) + " JOIN " + name + "\r\n";
    broadcast(channel, -1, message);
    if (!channel.topic.empty()) numeric(fd, "332", name + " :" + channel.topic);
    std::string memberNames;
    std::set<int>::const_iterator member = channel.members.begin();
    while (member != channel.members.end()) {
        std::map<int, Client>::const_iterator memberClient = _clients.find(*member);
        if (memberClient != _clients.end()) {
            if (!memberNames.empty())
                memberNames += " ";
            if (channel.operators.find(*member) != channel.operators.end())
                memberNames += "@";
            memberNames += memberClient->second.nickname;
        }
        ++member;
    }
    numeric(fd, "353", "= " + name + " :" + memberNames);
    numeric(fd, "366", name + " :End of /NAMES list");
}

void IrcServer::handlePart(int fd, const std::vector<std::string> &params, const std::string &trailing) {
    if (params.empty()) { numeric(fd, "461", "PART :Not enough parameters"); return; }
    std::string reason = trailing.empty() ? _clients.find(fd)->second.nickname : trailing;
    removeFromChannel(fd, params[0], reason, true);
}

void IrcServer::handlePrivmsg(int fd, const std::vector<std::string> &params, const std::string &trailing) {
    if (params.empty() || trailing.empty()) { numeric(fd, "412", ":No text to send"); return; }
    Client &client = _clients.find(fd)->second;
    std::string message = ":" + prefix(client) + " PRIVMSG " + params[0] + " :" + trailing + "\r\n";
    if (params[0][0] == '#') {
        std::map<std::string, Channel>::iterator it = _channels.find(params[0]);
        if (it == _channels.end() || it->second.members.find(fd) == it->second.members.end()) { numeric(fd, "442", params[0] + " :You're not on that channel"); return; }
        broadcast(it->second, fd, message);
    } else {
        int target = findNickname(params[0]);
        if (target < 0) { numeric(fd, "401", params[0] + " :No such nick"); return; }
        queue(target, message);
    }
}

void IrcServer::handleKick(int fd, const std::vector<std::string> &params, const std::string &trailing) {
    if (params.size() < 2) { numeric(fd, "461", "KICK :Not enough parameters"); return; }
    std::map<std::string, Channel>::iterator it = _channels.find(params[0]);
    if (it == _channels.end() || !isOperator(it->second, fd)) { numeric(fd, "482", params[0] + " :You're not channel operator"); return; }
    int target = findNickname(params[1]);
    if (target < 0 || it->second.members.find(target) == it->second.members.end()) { numeric(fd, "441", params[1] + " " + params[0] + " :They aren't on that channel"); return; }
    Client &client = _clients.find(fd)->second;
    std::string message = ":" + prefix(client) + " KICK " + params[0] + " " + params[1] + " :" + (trailing.empty() ? params[1] : trailing) + "\r\n";
    broadcast(it->second, -1, message);
    removeFromChannel(target, params[0], trailing.empty() ? "Kicked" : trailing, false);
}

void IrcServer::handleInvite(int fd, const std::vector<std::string> &params) {
    if (params.size() < 2) { numeric(fd, "461", "INVITE :Not enough parameters"); return; }
    std::map<std::string, Channel>::iterator it = _channels.find(params[1]);
    int target = findNickname(params[0]);
    if (target < 0) { numeric(fd, "401", params[0] + " :No such nick"); return; }
    if (it == _channels.end()) { numeric(fd, "403", params[1] + " :No such channel"); return; }
    if (it->second.members.find(fd) == it->second.members.end() || !isOperator(it->second, fd)) { numeric(fd, "482", params[1] + " :You're not channel operator"); return; }
    if (it->second.members.find(target) != it->second.members.end()) { numeric(fd, "443", params[0] + " " + params[1] + " :User is already on channel"); return; }
    it->second.invited.insert(target);
    queue(target, ":" + prefix(_clients.find(fd)->second) + " INVITE " + params[0] + " " + params[1] + "\r\n");
    queue(fd, ":ircserv 341 " + _clients.find(fd)->second.nickname + " " + params[0] + " " + params[1] + "\r\n");
}

void IrcServer::handleTopic(int fd, const std::vector<std::string> &params, const std::string &trailing) {
    if (params.empty()) { numeric(fd, "461", "TOPIC :Not enough parameters"); return; }
    std::map<std::string, Channel>::iterator it = _channels.find(params[0]);
    if (it == _channels.end()) { numeric(fd, "403", params[0] + " :No such channel"); return; }
    if (it->second.members.find(fd) == it->second.members.end()) { numeric(fd, "442", params[0] + " :You're not on that channel"); return; }
    if (trailing.empty()) {
        if (it->second.topic.empty()) numeric(fd, "331", params[0] + " :No topic is set");
        else numeric(fd, "332", params[0] + " :" + it->second.topic);
        return;
    }
    if (it->second.topicOperatorsOnly && !isOperator(it->second, fd)) { numeric(fd, "482", params[0] + " :You're not channel operator"); return; }
    it->second.topic = trailing;
    broadcast(it->second, -1, ":" + prefix(_clients.find(fd)->second) + " TOPIC " + params[0] + " :" + trailing + "\r\n");
}

void IrcServer::handleMode(int fd, const std::vector<std::string> &params) {
    if (params.empty()) { numeric(fd, "461", "MODE :Not enough parameters"); return; }
    std::map<std::string, Channel>::iterator it = _channels.find(params[0]);
    if (it == _channels.end()) { numeric(fd, "403", params[0] + " :No such channel"); return; }
    if (params.size() == 1) {
        std::string modes = "+";
        if (it->second.inviteOnly) modes += "i";
        if (it->second.topicOperatorsOnly) modes += "t";
        if (it->second.hasKey) modes += "k";
        if (it->second.hasLimit) modes += "l";
        numeric(fd, "324", params[0] + " :" + modes);
        return;
    }
    if (!isOperator(it->second, fd)) { numeric(fd, "482", params[0] + " :You're not channel operator"); return; }
    bool adding = true;
    std::size_t arg = 2;
    std::string mode = params[1];

        bool checkingAdding = true;
        std::size_t checkingArg = 2;
        for (std::size_t i = 0; i < mode.size(); ++i) {
            char flag = mode[i];
            if (flag == '+') { checkingAdding = true; continue; }
            if (flag == '-') { checkingAdding = false; continue; }
            if (flag != 'i' && flag != 't' && flag != 'k' && flag != 'l' && flag != 'o') {
                numeric(fd, "472", std::string(1, flag) + " :is unknown mode char to me");
                return;
            }
            if (((flag == 'k' || flag == 'l') && checkingAdding) || flag == 'o') {
                if (checkingArg >= params.size()) {
                    numeric(fd, "461", "MODE :Not enough parameters");
                    return;
                }
                if (flag == 'l' && (params[checkingArg].empty() || params[checkingArg] == "0")) {
                    numeric(fd, "461", "MODE :Invalid user limit");
                    return;
                }
                if (flag == 'l') {
                    for (std::size_t digit = 0; digit < params[checkingArg].size(); ++digit) {
                        if (params[checkingArg][digit] < '0' || params[checkingArg][digit] > '9') {
                            numeric(fd, "461", "MODE :Invalid user limit");
                            return;
                        }
                    }
                    unsigned long limit = std::strtoul(params[checkingArg].c_str(), 0, 10);
                    if (limit > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max())) {
                        numeric(fd, "461", "MODE :Invalid user limit");
                        return;
                    }
                }
                if (flag == 'o') {
                    int target = findNickname(params[checkingArg]);
                    if (target < 0) {
                        numeric(fd, "401", params[checkingArg] + " :No such nick");
                        return;
                    }
                    if (it->second.members.find(target) == it->second.members.end()) {
                        numeric(fd, "441", params[checkingArg] + " " + params[0] + " :They aren't on that channel");
                        return;
                    }
                }
                ++checkingArg;
            }
        }

    for (std::size_t i = 0; i < mode.size(); ++i) {
        char flag = mode[i];
        if (flag == '+') { adding = true; continue; }
        if (flag == '-') { adding = false; continue; }
        if (flag == 'i') it->second.inviteOnly = adding;
        else if (flag == 't') it->second.topicOperatorsOnly = adding;
        else if (flag == 'k' && adding && arg < params.size()) { it->second.hasKey = true; it->second.key = params[arg++]; }
        else if (flag == 'k' && adding) { numeric(fd, "461", "MODE :Not enough parameters"); return; }
        else if (flag == 'k' && !adding) { it->second.hasKey = false; it->second.key.clear(); }
        else if (flag == 'l' && adding && arg < params.size()) { it->second.hasLimit = true; it->second.limit = static_cast<std::size_t>(std::strtoul(params[arg++].c_str(), 0, 10)); }
        else if (flag == 'l' && adding) { numeric(fd, "461", "MODE :Not enough parameters"); return; }
        else if (flag == 'l' && !adding) it->second.hasLimit = false;
        else if (flag == 'o' && arg < params.size()) {
            std::string nickname = params[arg++];
            int target = findNickname(nickname);
            if (target < 0) { numeric(fd, "401", nickname + " :No such nick"); return; }
            if (it->second.members.find(target) == it->second.members.end()) { numeric(fd, "441", nickname + " " + params[0] + " :They aren't on that channel"); return; }
            if (adding) it->second.operators.insert(target);
            else it->second.operators.erase(target);
        }
        else if (flag == 'o') { numeric(fd, "461", "MODE :Not enough parameters"); return; }
        else { numeric(fd, "472", std::string(1, flag) + " :is unknown mode char to me"); return; }
    }
    std::string modeMessage = ":" + prefix(_clients.find(fd)->second) + " MODE " + params[0] + " " + params[1];
    for (std::size_t i = 2; i < params.size(); ++i)
        modeMessage += " " + params[i];
    broadcast(it->second, -1, modeMessage + "\r\n");
}

void IrcServer::queue(int fd, const std::string &message) {
    if (!hasClient(fd))
        return;
    Client &client = _clients.find(fd)->second;
    if (message.size() > MAX_OUTPUT || client.output.size() > MAX_OUTPUT - message.size()) {
        client.closeRequested = true;
        return;
    }
    client.output.append(message);
}

void IrcServer::numeric(int fd, const std::string &code, const std::string &message) {
    std::string nick = hasClient(fd) && !_clients.find(fd)->second.nickname.empty() ? _clients.find(fd)->second.nickname : "*";
    std::string formatted = message;
    if (!formatted.empty() && formatted[0] != ':' && formatted.find(" :") == std::string::npos)
        formatted = ":" + formatted;
    queue(fd, ":ircserv " + code + " " + nick + " " + formatted + "\r\n");
}

void IrcServer::broadcast(const Channel &channel, int exceptFd, const std::string &message) {
    std::set<int>::const_iterator it = channel.members.begin();
    while (it != channel.members.end()) {
        if (*it != exceptFd) queue(*it, message);
        ++it;
    }
}

void IrcServer::removeClient(int fd, const std::string &reason) {
    std::map<int, Client>::iterator it = _clients.find(fd);
    if (it == _clients.end()) return;
    std::vector<std::string> names(it->second.channels.begin(), it->second.channels.end());
    for (std::size_t i = 0; i < names.size(); ++i)
        removeFromChannel(fd, names[i], reason, true);
    close(fd);
    _clients.erase(it);
}

void IrcServer::removeFromChannel(int fd, const std::string &channelName, const std::string &reason, bool announce) {
    std::map<std::string, Channel>::iterator it = _channels.find(channelName);
    if (it == _channels.end() || it->second.members.find(fd) == it->second.members.end()) return;
    if (announce && hasClient(fd)) broadcast(it->second, -1, ":" + prefix(_clients.find(fd)->second) + " PART " + channelName + " :" + reason + "\r\n");
    it->second.members.erase(fd);
    it->second.operators.erase(fd);
    if (hasClient(fd)) _clients.find(fd)->second.channels.erase(channelName);
    if (it->second.members.empty()) _channels.erase(it);
}

bool IrcServer::hasClient(int fd) const { return _clients.find(fd) != _clients.end(); }
bool IrcServer::validChannelName(const std::string &name) const { return name.size() > 1 && name[0] == '#' && name.find(' ') == std::string::npos; }
bool IrcServer::isOperator(const Channel &channel, int fd) const { return channel.operators.find(fd) != channel.operators.end(); }

int IrcServer::findNickname(const std::string &nickname) const {
    std::map<int, Client>::const_iterator it = _clients.begin();
    while (it != _clients.end()) { if (it->second.nickname == nickname) return it->first; ++it; }
    return -1;
}

std::string IrcServer::prefix(const Client &client) const {
    return client.nickname + "!" + client.username + "@localhost";
}

std::string IrcServer::upper(const std::string &value) {
    std::string result = value;
    for (std::size_t i = 0; i < result.size(); ++i)
        if (result[i] >= 'a' && result[i] <= 'z') result[i] = static_cast<char>(result[i] - 'a' + 'A');
    return result;
}
