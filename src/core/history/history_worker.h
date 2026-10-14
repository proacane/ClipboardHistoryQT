#pragma once

#include "history_service.h"

#include <QObject>
#include <QTimer>
#include <memory>

namespace ClipboardHistory::Core {

class HistoryWorker final : public QObject {
    Q_OBJECT

public:
    HistoryWorker(QString databaseFilePath, QString imagesDirectory);

public slots:
    void initialize();
    void record(const HistoryCapture &capture);
    void requestPage(qint64 offset, qint64 limit, const QString &search, const QString &kind,
                     bool includeTotalCount);
    void loadEntry(qint64 id);
    void copyEntry(qint64 id);
    void setFavorite(qint64 id, bool favorite);
    void remove(qint64 id);
    void clear();
    void setRetentionHours(int hours);
    void setMaximumEntries(int maximum);
    void importLegacy(const QString &databaseFilePath, const QString &imagesDirectory);
    void shutdown();

signals:
    void initialized(const ClipboardHistory::Core::HistoryServiceStatus &status);
    void recordCompleted(const ClipboardHistory::Core::HistoryRecordResult &result);
    void pageReady(const ClipboardHistory::Core::HistoryPreviewPage &page);
    void entryLoaded(qint64 id, const ClipboardHistory::Core::HistoryCopyResult &result);
    void copyReady(const ClipboardHistory::Core::HistoryCopyResult &result);
    void mutationCompleted(const ClipboardHistory::Core::HistoryMutationResult &result);
    void retentionCompleted(const ClipboardHistory::Core::HistoryMutationResult &result);
    void legacyImportCompleted(const ClipboardHistory::Core::HistoryLegacyImportResult &result);
    void stopped();

private:
    void scheduleRetention();
    void updateRetentionTimer();
    void removeExpired();
    QTimer m_retentionTimer;
    int m_retentionHours = 0;
    int m_maximumEntries = 0;
    bool m_retentionPending = false;
    QString m_databaseFilePath;
    QString m_imagesDirectory;
    std::unique_ptr<HistoryService> m_service;
};

} // namespace ClipboardHistory::Core

Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryCapture)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryServiceStatus)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryRecordResult)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryPreviewPage)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryCopyResult)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryMutationResult)
Q_DECLARE_METATYPE(ClipboardHistory::Core::HistoryLegacyImportResult)
