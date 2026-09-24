#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <set>
#include <string>

struct Channel {
    std::string name;
    std::string topic;
    bool inviteOnly;
    bool topicOperatorsOnly;
    bool hasKey;
    std::string key;
    bool hasLimit;
    std::size_t limit;
    std::set<int> members;
    std::set<int> operators;
    std::set<int> invited;

    Channel() : inviteOnly(false), topicOperatorsOnly(false), hasKey(false),
                 hasLimit(false), limit(0) {}
    explicit Channel(const std::string &channelName)
        : name(channelName), inviteOnly(false), topicOperatorsOnly(false),
          hasKey(false), hasLimit(false), limit(0) {}
};

#endif
