#include "PrivacyLogger.h"

#include "../settings/app_paths.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QIODevice>

namespace ClipboardHistory::Core {
namespace {

constexpr std::size_t MaxQueuedRecords = 1024;
constexpr qint64 MaxFileBytes = 1024 * 1024;
constexpr int RotatedFileCount = 2;

} // namespace

PrivacyLogger::PrivacyLogger()
{
    worker_ = std::thread(&PrivacyLogger::run, this);
}

PrivacyLogger::~PrivacyLogger()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

void PrivacyLogger::recordEvent(quint32 eventCode)
{
    enqueue({RecordType::Event, eventCode, 0});
}

void PrivacyLogger::recordDuration(quint32 eventCode, qint64 milliseconds)
{
    enqueue({RecordType::Duration, eventCode, milliseconds});
}

void PrivacyLogger::recordCount(quint32 counterCode, qint64 value)
{
    enqueue({RecordType::Count, counterCode, value});
}

void PrivacyLogger::recordError(qint32 errorCode)
{
    enqueue({RecordType::Error, errorCode, 0});
}

quint64 PrivacyLogger::droppedRecords() const noexcept
{
    return droppedRecords_.load(std::memory_order_relaxed);
}

PrivacyLogger::BackendError PrivacyLogger::backendError() const noexcept
{
    return backendError_.load(std::memory_order_relaxed);
}

void PrivacyLogger::enqueue(Record record)
{
    if (!mutex_.try_lock()) {
        droppedRecords_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    try {
        if (stopping_ || queue_.size() >= MaxQueuedRecords) {
            droppedRecords_.fetch_add(1, std::memory_order_relaxed);
            mutex_.unlock();
            return;
        }
        queue_.push_back(record);
    } catch (...) {
        droppedRecords_.fetch_add(1, std::memory_order_relaxed);
        mutex_.unlock();
        return;
    }

    mutex_.unlock();
    condition_.notify_one();
}

bool PrivacyLogger::rotate(QFile &file, const QString &path)
{
    file.close();

    for (int index = RotatedFileCount; index >= 1; --index) {
        const QString source = index == 1 ? path : path + QStringLiteral(".%1").arg(index - 1);
        const QString destination = path + QStringLiteral(".%1").arg(index);

        if (QFile::exists(destination) && !QFile::remove(destination)) {
            backendError_.store(BackendError::RotationFailed, std::memory_order_relaxed);
            return false;
        }
        if (QFile::exists(source) && !QFile::rename(source, destination)) {
            backendError_.store(BackendError::RotationFailed, std::memory_order_relaxed);
            return false;
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        backendError_.store(BackendError::OpenFailed, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void PrivacyLogger::run()
{
    const QString dataDirectory = AppPaths::dataDirectory();
    const QString logDirectory = QDir(dataDirectory).filePath(QStringLiteral("logs"));
    if (dataDirectory.isEmpty() || !QDir().mkpath(logDirectory)) {
        backendError_.store(BackendError::DataDirectoryUnavailable, std::memory_order_relaxed);
        return;
    }

    const QString logPath = QDir(logDirectory).filePath(QStringLiteral("events.log"));
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        backendError_.store(BackendError::OpenFailed, std::memory_order_relaxed);
        return;
    }

    for (;;) {
        std::deque<Record> batch;
        bool stopping = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            batch.swap(queue_);
            stopping = stopping_;
        }

        for (const Record &record : batch) {
            QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toLatin1();
            line += '\t';
            switch (record.type) {
            case RecordType::Event: line += 'E'; break;
            case RecordType::Duration: line += 'D'; break;
            case RecordType::Count: line += 'C'; break;
            case RecordType::Error: line += 'X'; break;
            }
            line += '\t';
            line += QByteArray::number(record.code);
            if (record.type == RecordType::Duration || record.type == RecordType::Count) {
                line += '\t';
                line += QByteArray::number(record.value);
            }
            line += '\n';

            if (file.size() + line.size() > MaxFileBytes && !rotate(file, logPath)) {
                droppedRecords_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!file.isOpen() || file.write(line) != line.size()) {
                backendError_.store(BackendError::WriteFailed, std::memory_order_relaxed);
                droppedRecords_.fetch_add(1, std::memory_order_relaxed);
                file.close();
            }
        }

        if (stopping) {
            if (file.isOpen() && !file.flush())
                backendError_.store(BackendError::FlushFailed, std::memory_order_relaxed);
            return;
        }
    }
}

} // namespace ClipboardHistory::Core
