#include "history_worker.h"

#include <QThread>
#include <QDateTime>

#include <utility>

namespace ClipboardHistory::Core {

HistoryWorker::HistoryWorker(QString databaseFilePath, QString imagesDirectory)
    : m_retentionTimer(this), m_databaseFilePath(std::move(databaseFilePath)),
      m_imagesDirectory(std::move(imagesDirectory))
{
    m_retentionTimer.setInterval(60000);
    connect(&m_retentionTimer, &QTimer::timeout, this, &HistoryWorker::scheduleRetention);
    qRegisterMetaType<HistoryCapture>();
    qRegisterMetaType<HistoryServiceStatus>();
    qRegisterMetaType<HistoryRecordResult>();
    qRegisterMetaType<HistoryPreviewPage>();
    qRegisterMetaType<HistoryCopyResult>();
    qRegisterMetaType<HistoryMutationResult>();
    qRegisterMetaType<HistoryLegacyImportResult>();
}

void HistoryWorker::initialize()
{
    m_service = std::make_unique<HistoryService>(m_databaseFilePath, m_imagesDirectory);
    emit initialized(m_service->initialize());
}

void HistoryWorker::record(const HistoryCapture &capture)
{
    if (!m_service) {
        emit recordCompleted({{HistoryServiceError::NotInitialized}});
        return;
    }
    const auto result = m_service->record(capture);
    emit recordCompleted(result);
    if (result.status && m_maximumEntries > 0) scheduleRetention();
}

void HistoryWorker::requestPage(qint64 offset, qint64 limit, const QString &search,
                                const QString &kind, bool includeTotalCount)
{
    emit pageReady(m_service ? m_service->previewPage(offset, limit, search, kind, includeTotalCount)
                             : HistoryPreviewPage{{RepositoryError::NotInitialized}, {}});
}

void HistoryWorker::copyEntry(qint64 id)
{
    emit copyReady(m_service ? m_service->copyEntry(id)
                             : HistoryCopyResult{{HistoryServiceError::NotInitialized}, {}, {}, {}});
}

void HistoryWorker::loadEntry(qint64 id)
{
    emit entryLoaded(id, m_service ? m_service->loadEntry(id)
                                   : HistoryCopyResult{{HistoryServiceError::NotInitialized}, {}, {}, {}});
}

void HistoryWorker::setFavorite(qint64 id, bool favorite)
{
    emit mutationCompleted(m_service ? m_service->setFavorite(id, favorite)
                                     : HistoryMutationResult{{HistoryServiceError::NotInitialized}});
    if (!favorite && m_maximumEntries > 0) scheduleRetention();
}

void HistoryWorker::remove(qint64 id)
{
    emit mutationCompleted(m_service ? m_service->remove(id)
                                     : HistoryMutationResult{{HistoryServiceError::NotInitialized}});
}

void HistoryWorker::clear()
{
    emit mutationCompleted(m_service ? m_service->clear()
                                     : HistoryMutationResult{{HistoryServiceError::NotInitialized}});
}

void HistoryWorker::importLegacy(const QString &databaseFilePath, const QString &imagesDirectory)
{
    emit legacyImportCompleted(m_service
        ? m_service->importLegacy(databaseFilePath, imagesDirectory)
        : HistoryLegacyImportResult{{HistoryServiceError::NotInitialized}});
    if (m_maximumEntries > 0) scheduleRetention();
}

void HistoryWorker::setRetentionHours(int hours)
{
    m_retentionHours = hours;
    updateRetentionTimer();
}

void HistoryWorker::setMaximumEntries(int maximum)
{
    m_maximumEntries = maximum;
    updateRetentionTimer();
}

void HistoryWorker::updateRetentionTimer()
{
    if (m_retentionHours == 0 && m_maximumEntries == 0) m_retentionTimer.stop();
    else {
        m_retentionTimer.start();
        scheduleRetention();
    }
}

void HistoryWorker::scheduleRetention()
{
    if ((m_retentionHours <= 0 && m_maximumEntries <= 0) || !m_service || m_retentionPending) return;
    m_retentionPending = true;
    QMetaObject::invokeMethod(this, &HistoryWorker::removeExpired, Qt::QueuedConnection);
}

void HistoryWorker::removeExpired()
{
    m_retentionPending = false;
    if ((m_retentionHours <= 0 && m_maximumEntries <= 0) || !m_service) return;
    const auto cutoff = m_retentionHours > 0
        ? QDateTime::currentMSecsSinceEpoch() - qint64(m_retentionHours) * 3600000 : 0;
    const auto result = m_service->enforceRetention(cutoff, m_maximumEntries);
    emit retentionCompleted(result);
    // Yield between batches so queued capture, search and settings changes can run.
    if (result.status && result.removedCount == 500) scheduleRetention();
}

void HistoryWorker::shutdown()
{
    m_retentionHours = m_maximumEntries = 0;
    m_retentionTimer.stop();
    m_service.reset();
    emit stopped();
    QThread::currentThread()->quit();
}

} // namespace ClipboardHistory::Core
