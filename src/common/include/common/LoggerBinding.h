#pragma once

#include <initializer_list>
#include <memory>
#include <vector>

namespace spdlog { class logger; }

namespace visionrt {
struct LoggerRegistration
{
    void (*attach)(const std::shared_ptr<spdlog::logger> &);
    void (*detach)();
};

class LoggerBinding
{
public:
    LoggerBinding(const std::shared_ptr<spdlog::logger> &logger,
                  std::initializer_list<LoggerRegistration> registrations)
        : registrations_(registrations)
    {
        for (const auto &registration : registrations_)
            registration.attach(logger);
    }

    ~LoggerBinding()
    {
        for (auto item = registrations_.rbegin(); item != registrations_.rend(); ++item)
            item->detach();
    }

    LoggerBinding(const LoggerBinding &) = delete;
    LoggerBinding &operator=(const LoggerBinding &) = delete;

private:
    std::vector<LoggerRegistration> registrations_;
};
}
