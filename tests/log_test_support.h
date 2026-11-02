#pragma once

// Shared fixtures for the log contract tests. The log contract is split by responsibility
// (text/allocation, aggregation, lifecycle); these helpers own the temporary session and the
// producer threads so each test file only states its scenario.

#include "log/LogModel.h"
#include "log/LogService.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>

#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace log_test
{

inline void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

inline QString fileText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

inline QString rowLine(const visionrt::LogModel &model, int row)
{
    return model.data(model.index(row, 0), visionrt::LogModel::LineRole).toString();
}

inline bool anyRow(const visionrt::LogModel &model, const std::function<bool(const QString &)> &match)
{
    for (int row = 0; row < model.rowCount(); ++row)
        if (match(rowLine(model, row)))
            return true;
    return false;
}

inline void probeHandler(QtMsgType, const QMessageLogContext &, const QString &) {}

// The file sink prefixes the local date onto the configured base name.
inline QString datedLog(const QString &directory)
{
    return QDir(directory).filePath(QStringLiteral("visionrt_")
                                    + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
                                    + QStringLiteral(".log"));
}

inline visionrt::LogConfig testConfig()
{
    visionrt::LogConfig config;
    config.queueCapacity     = 1024;
    config.uiCapacity        = 32;
    config.maxMessageBytes   = 64;
    config.flushSeconds      = 1;
    config.uiBatchIntervalMs = 20;
    config.uiBatchMaximum    = 8;
    return config;
}

// Owns the process-wide service: every test file starts it through this fixture and shutdown is
// guaranteed even when a requirement throws.
class ServiceSession
{
public:
    explicit ServiceSession(visionrt::LogConfig config = testConfig())
        : config_(config)
    {
    }

    ~ServiceSession() { service_.shutdown(); }

    ServiceSession(const ServiceSession &)            = delete;
    ServiceSession &operator=(const ServiceSession &) = delete;

    void start()
    {
        require(service_.start(config_), "the log service refused to start");
    }

    void start(const QString &filePath)
    {
        require(service_.start(config_, filePath), "the log service refused to start");
    }

    void restart(visionrt::LogConfig config)
    {
        service_.shutdown();
        config_ = config;
        start();
    }

    void restart(visionrt::LogConfig config, const QString &filePath)
    {
        service_.shutdown();
        config_ = config;
        start(filePath);
    }

    void stop() { service_.shutdown(); }

    visionrt::LogService &service() { return service_; }
    visionrt::LogConfig  &config() { return config_; }

private:
    visionrt::LogConfig   config_;
    visionrt::LogService &service_ = visionrt::LogService::instance();
};

// Owns the producer threads of a lifecycle scenario. Exceptions are collected per thread and
// rethrown by join(), so nothing is swallowed and no thread is leaked on an early return.
class ProducerThreads
{
public:
    ProducerThreads()                               = default;
    ProducerThreads(const ProducerThreads &)            = delete;
    ProducerThreads &operator=(const ProducerThreads &) = delete;
    ~ProducerThreads() { joinQuietly(); }

    void spawn(const std::function<void()> &body)
    {
        threads_.emplace_back(
            [body, this]
            {
                try
                {
                    body();
                }
                catch (...)
                {
                    failure_ = std::current_exception();
                }
            });
    }

    void join()
    {
        joinQuietly();
        if (failure_)
            std::rethrow_exception(failure_);
    }

private:
    void joinQuietly()
    {
        for (auto &thread : threads_)
            if (thread.joinable())
                thread.join();
        threads_.clear();
    }

    std::vector<std::thread> threads_;
    std::exception_ptr       failure_;
};

} // namespace log_test
