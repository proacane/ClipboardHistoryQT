#pragma once

#include <QFile>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

namespace ClipboardHistory::Core {

class PrivacyLogger final
{
public:
    enum class BackendError : quint8 {
        None,
        DataDirectoryUnavailable,
        OpenFailed,
        RotationFailed,
        WriteFailed,
        FlushFailed
    };

    PrivacyLogger();
    ~PrivacyLogger();

    PrivacyLogger(const PrivacyLogger &) = delete;
    PrivacyLogger &operator=(const PrivacyLogger &) = delete;

    // Codes are caller-defined numeric identifiers; text and paths are never accepted.
    void recordEvent(quint32 eventCode);
    void recordDuration(quint32 eventCode, qint64 milliseconds);
    void recordCount(quint32 counterCode, qint64 value);
    void recordError(qint32 errorCode);

    quint64 droppedRecords() const noexcept;
    BackendError backendError() const noexcept;

private:
    enum class RecordType : quint8 { Event, Duration, Count, Error };

    struct Record {
        RecordType type;
        qint64 code;
        qint64 value;
    };

    void enqueue(Record record);
    void run();
    bool rotate(QFile &file, const QString &path);

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Record> queue_;
    bool stopping_ = false;
    std::thread worker_;
    std::atomic<quint64> droppedRecords_{0};
    std::atomic<BackendError> backendError_{BackendError::None};
};

} // namespace ClipboardHistory::Core