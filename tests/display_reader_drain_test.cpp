// Contract for the asynchronous reader drain (T-R32-05).
// The GUI thread must never wait for the display worker: it invalidates the pending read and is
// continued through a queued completion exactly once, while its own event loop keeps ticking.
#include "ui/DisplayConverter.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTimer>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);

    // Large float images force the normalising conversion path, which keeps the worker busy long
    // enough to observe a real overlap with the GUI event loop.
    std::vector<cv::Mat> images;
    for (int index = 0; index < 4; ++index)
        images.emplace_back(cv::Mat(1800, 1800, CV_64FC1, cv::Scalar(100.0 + index)));
    DataRef<Image> batch{Image(images)};

    DisplayConverter converter;
    QSignalSpy      idle(&converter, &DisplayConverter::readersIdle);
    QSignalSpy      converted(&converter, &DisplayConverter::batchConverted);
    converter.requestBatch(batch, QStringLiteral("Gray"), 1, 1);
    require(converter.isBusy(), "the display worker must start immediately");

    // This is exactly what AppController::Impl::beginDrain does before mutating runtime data.
    QElapsedTimer invalidateTimer;
    invalidateTimer.start();
    converter.invalidate(1);
    const auto invalidateMs = invalidateTimer.elapsed();
    require(invalidateMs < 50, "invalidating a pending read must not block the GUI thread");

    int   beats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&beats] { ++beats; });
    heartbeat.start();

    // Spin the real event loop until the worker reports that it released the batch.
    QElapsedTimer elapsed;
    elapsed.start();
    while (idle.isEmpty() && elapsed.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    heartbeat.stop();
    require(!idle.isEmpty(), "the display worker never reported an idle state");
    require(beats >= 3, "the GUI thread did not keep processing events while the reader was draining");
    require(converted.isEmpty(), "a discarded conversion must not reach the viewport");

    // The completion is delivered once: a stale batch can not release a later drain.
    const int reported = idle.count();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    require(idle.count() == reported, "the idle notification must not repeat for the same batch");
    require(!converter.isBusy(), "the converter must be idle after the drain");

    // A second request after the drain still works, which proves the invalidation was not a stop.
    QSignalSpy second(&converter, &DisplayConverter::batchConverted);
    converter.requestBatch(batch, QStringLiteral("Gray"), 2, 2);
    QElapsedTimer secondTimer;
    secondTimer.start();
    while (second.isEmpty() && secondTimer.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(!second.isEmpty(), "the converter must keep working after a drain");

    std::cout << "UI_DRAIN_PASS invalidate=" << invalidateMs << "ms beats=" << beats << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[UI drain error] " << error.what() << std::endl;
    return 1;
}
