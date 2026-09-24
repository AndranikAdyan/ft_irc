#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <set>
#include <string>
#include <vector>

class Client {
public:
    int fd;
    bool passwordAccepted;
    bool registered;
    bool welcomed;
    bool closeRequested;
    std::string nickname;
    std::string username;
    std::string realname;
    std::string output;
    std::size_t outputOffset;
    std::set<std::string> channels;

    explicit Client(int descriptor)
        : fd(descriptor), passwordAccepted(false), registered(false),
          welcomed(false), closeRequested(false), outputOffset(0) {}

    void appendData(const char *data, std::size_t length) {
      _buffer.append(data, length);
    }

    std::vector<std::string> extractCommands() {
      std::vector<std::string> commands;
      std::size_t end;
      while ((end = _buffer.find('\n')) != std::string::npos) {
        std::string command = _buffer.substr(0, end);
        _buffer.erase(0, end + 1);
        if (!command.empty() && command[command.size() - 1] == '\r')
          command.erase(command.size() - 1);
        commands.push_back(command);
      }
      return commands;
    }

    std::size_t bufferSize() const {
      return _buffer.size();
    }

    void discardIncompleteCommand() {
      _buffer.clear();
    }

  private:
    std::string _buffer;
};

#endif
