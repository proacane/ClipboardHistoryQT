#include "history_task_queue.h"

#include <utility>

namespace ClipboardHistory::Core {

HistoryTaskQueue::HistoryTaskQueue(QString databaseFilePath, QString imagesDirectory,
                                   QObject *parent)
    : QObject(parent),
      m_worker(new HistoryWorker(std::move(databaseFilePath), std::move(imagesDirectory)))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, this, &HistoryTaskQueue::initializeRequested);
    connect(this, &HistoryTaskQueue::initializeRequested, m_worker, &HistoryWorker::initialize,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::recordRequested, m_worker, &HistoryWorker::record,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::pageRequested, m_worker, &HistoryWorker::requestPage,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::entryLoadRequested, m_worker, &HistoryWorker::loadEntry,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::copyRequested, m_worker, &HistoryWorker::copyEntry,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::favoriteRequested, m_worker, &HistoryWorker::setFavorite,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::removeRequested, m_worker, &HistoryWorker::remove,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::clearRequested, m_worker, &HistoryWorker::clear,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::maximumEntriesRequested, m_worker, &HistoryWorker::setMaximumEntries, Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::retentionHoursRequested, m_worker, &HistoryWorker::setRetentionHours, Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::retentionCompleted, this, &HistoryTaskQueue::retentionCompleted, Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::legacyImportRequested, m_worker, &HistoryWorker::importLegacy,
            Qt::QueuedConnection);
    connect(this, &HistoryTaskQueue::shutdownRequested, m_worker, &HistoryWorker::shutdown,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::initialized, this, &HistoryTaskQueue::onInitialized,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::recordCompleted, this, &HistoryTaskQueue::onRecordCompleted,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::pageReady, this, &HistoryTaskQueue::pageReady,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::entryLoaded, this, &HistoryTaskQueue::entryLoaded,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::copyReady, this, &HistoryTaskQueue::copyReady,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::mutationCompleted, this, &HistoryTaskQueue::mutationCompleted,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::legacyImportCompleted, this, &HistoryTaskQueue::onLegacyImportCompleted,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::stopped, this, &HistoryTaskQueue::onStopped,
            Qt::QueuedConnection);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

HistoryTaskQueue::~HistoryTaskQueue()
{
    shutdown();
    m_thread.wait();
}

HistoryTaskQueue::SubmitResult HistoryTaskQueue::submit(const HistoryCapture &capture)
{
    if (m_stopping)
        return SubmitResult::Stopping;
    if (!m_ready)
        return SubmitResult::NotReady;
    const qsizetype payloadBytes = capture.image.isNull()
        ? capture.content.size() * qsizetype(sizeof(QChar))
        : capture.image.sizeInBytes();
    if (m_outstandingTasks >= maxOutstandingTasks()
        || payloadBytes > maxOutstandingBytes()
        || m_outstandingBytes > maxOutstandingBytes() - payloadBytes)
        return SubmitResult::QueueFull;

    ++m_outstandingTasks;
    m_outstandingBytes += payloadBytes;
    m_pendingPayloadBytes.enqueue(payloadBytes);
    emit recordRequested(capture);
    return SubmitResult::Accepted;
}

bool HistoryTaskQueue::requestPage(qint64 offset, qint64 limit, QString search, QString kind,
                                   bool includeTotalCount)
{
    if (!m_ready || m_stopping || offset < 0 || limit <= 0 || limit > 200)
        return false;
    emit pageRequested(offset, limit, search, kind, includeTotalCount);
    return true;
}

bool HistoryTaskQueue::requestCopy(qint64 id)
{
    if (!m_ready || m_stopping || id <= 0)
        return false;
    emit copyRequested(id);
    return true;
}

bool HistoryTaskQueue::loadEntry(qint64 id)
{
    if (!m_ready || m_stopping || id <= 0)
        return false;
    emit entryLoadRequested(id);
    return true;
}

bool HistoryTaskQueue::setFavorite(qint64 id, bool favorite)
{
    if (!m_ready || m_stopping || id <= 0)
        return false;
    emit favoriteRequested(id, favorite);
    return true;
}

bool HistoryTaskQueue::remove(qint64 id)
{
    if (!m_ready || m_stopping || id <= 0)
        return false;
    emit removeRequested(id);
    return true;
}

bool HistoryTaskQueue::clear()
{
    if (!m_ready || m_stopping)
        return false;
    emit clearRequested();
    return true;
}

bool HistoryTaskQueue::setMaximumEntries(int maximum)
{
    if (m_stopping || maximum < 0) return false;
    m_maximumEntries = maximum;
    if (m_ready) emit maximumEntriesRequested(maximum);
    return true;
}

bool HistoryTaskQueue::setRetentionHours(int hours)
{
    if (m_stopping || hours < 0 || hours > maxRetentionHours()) return false;
    m_retentionHours = hours;
    if (m_ready) emit retentionHoursRequested(hours);
    return true;
}

bool HistoryTaskQueue::importLegacy(QString databaseFilePath, QString imagesDirectory)
{
    if (!m_ready || m_stopping || m_legacyImportPending
        || databaseFilePath.isEmpty() || imagesDirectory.isEmpty())
        return false;
    m_legacyImportPending = true;
    emit legacyImportRequested(databaseFilePath, imagesDirectory);
    return true;
}

void HistoryTaskQueue::shutdown()
{
    if (m_stopping)
        return;
    m_stopping = true;
    m_ready = false;
    emit shutdownRequested();
}

void HistoryTaskQueue::onInitialized(const HistoryServiceStatus &status)
{
    m_ready = static_cast<bool>(status) && !m_stopping;
    if (m_ready && m_retentionHours > 0) emit retentionHoursRequested(m_retentionHours);
    if (m_ready && m_maximumEntries > 0) emit maximumEntriesRequested(m_maximumEntries);
    emit initialized(status);
}

void HistoryTaskQueue::onRecordCompleted(const HistoryRecordResult &result)
{
    --m_outstandingTasks;
    m_outstandingBytes -= m_pendingPayloadBytes.dequeue();
    emit recordCompleted(result);
}

void HistoryTaskQueue::onLegacyImportCompleted(const HistoryLegacyImportResult &result)
{
    m_legacyImportPending = false;
    emit legacyImportCompleted(result);
}

void HistoryTaskQueue::onStopped()
{
    emit stopped();
}

} // namespace ClipboardHistory::Core
