#pragma once

#include <visionrt/contracts/Value.h>

#include <QImage>
#include <QObject>
#include <QString>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace visionrt {

struct DisplayFrame
{
    QImage  image;
    QString warning;
    QString error;
    int     batchIndex = 0; // Position in the submitted display batch, not a DAG batch index.
    quint64 generation = 0;
    quint64 requestId  = 0;
};

// GUI-owned scheduler: one conversion and one replaceable latest request.
class DisplayConverter final : public QObject
{
    Q_OBJECT
public:
    explicit DisplayConverter(QObject *parent = nullptr);
    void requestBatch(DataRef<Image> images, QString colorOrder, quint64 requestId, quint64 generation);
    ~DisplayConverter() override;
    void                invalidate(quint64 generation);
    void                waitForReads();
    static DisplayFrame convert(const cv::Mat &image, const QString &colorOrder = {});

    bool isBusy() const noexcept
    {
        return busy_;
    }

    bool hasPending() const noexcept
    {
        return pending_.has_value();
    }

signals:
    void batchConverted(const std::vector<visionrt::DisplayFrame> &frames);
    // Emitted after the worker dropped every reference to the submitted batch. Management flows use
    // it to continue asynchronously instead of blocking the GUI thread.
    void readersIdle();

private:
    struct Request
    {
        DataRef<Image>          images;
        QString                 colorOrder;
        quint64                 requestId;
        quint64                 generation;
        quint64                 serial;
    };

    void dispatch(Request request);
    void run(std::stop_token stop);
    void complete(std::vector<DisplayFrame> frames, quint64 serial);

    quint64                     generation_      = 0;
    quint64                     latestRequestId_ = 0;
    quint64                     serial_          = 0;
    quint64                     activeSerial_    = 0;
    bool                        hasRequest_      = false;
    bool                        busy_            = false;
    bool                        reading_         = false;
    std::optional<Request>      pending_;
    std::mutex                  mutex_;
    std::condition_variable_any changed_;
    std::optional<Request>      work_;
    std::jthread                worker_;
};

} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::DisplayFrame)
