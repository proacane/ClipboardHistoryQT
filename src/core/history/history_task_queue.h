#pragma once

#include "history_worker.h"

#include <QObject>
#include <QQueue>
#include <QThread>

namespace ClipboardHistory::Core {

class HistoryTaskQueue final : public QObject {
    Q_OBJECT

public:
    enum class SubmitResult { Accepted, NotReady, QueueFull, Stopping };
    Q_ENUM(SubmitResult)

    explicit HistoryTaskQueue(QString databaseFilePath, QString imagesDirectory,
                              QObject *parent = nullptr);
    ~HistoryTaskQueue() override;

    SubmitResult submit(const HistoryCapture &capture);
    bool requestPage(qint64 offset, qint64 limit, QString search = {}, QString kind = {},
                     bool includeTotalCount = false);
    bool loadEntry(qint64 id);
    bool requestCopy(qint64 id);
    bool setFavorite(qint64 id, bool favorite);
    bool remove(qint64 id);
    bool clear();
    bool setRetentionHours(int hours);
    bool setMaximumEntries(int maximum);
    static constexpr int maxRetentionHours() { return 24 * 3650; }
    bool importLegacy(QString databaseFilePath, QString imagesDirectory);
    void shutdown();
    static constexpr int maxOutstandingTasks() { return 32; }
    static constexpr qsizetype maxOutstandingBytes() { return 64 * 1024 * 1024; }

signals:
    void initializeRequested();
    void recordRequested(const ClipboardHistory::Core::HistoryCapture &capture);
    void pageRequested(qint64 offset, qint64 limit, const QString &search, const QString &kind,
                       bool includeTotalCount);
    void entryLoadRequested(qint64 id);
    void copyRequested(qint64 id);
    void favoriteRequested(qint64 id, bool favorite);
    void removeRequested(qint64 id);
    void clearRequested();
    void retentionHoursRequested(int hours);
    void maximumEntriesRequested(int maximum);
    void legacyImportRequested(const QString &databaseFilePath, const QString &imagesDirectory);
    void shutdownRequested();
    void initialized(const ClipboardHistory::Core::HistoryServiceStatus &status);
    void recordCompleted(const ClipboardHistory::Core::HistoryRecordResult &result);
    void pageReady(const ClipboardHistory::Core::HistoryPreviewPage &page);
    void entryLoaded(qint64 id, const ClipboardHistory::Core::HistoryCopyResult &result);
    void copyReady(const ClipboardHistory::Core::HistoryCopyResult &result);
    void mutationCompleted(const ClipboardHistory::Core::HistoryMutationResult &result);
    void retentionCompleted(const ClipboardHistory::Core::HistoryMutationResult &result);
    void legacyImportCompleted(const ClipboardHistory::Core::HistoryLegacyImportResult &result);
    void stopped();

private slots:
    void onInitialized(const ClipboardHistory::Core::HistoryServiceStatus &status);
    void onRecordCompleted(const ClipboardHistory::Core::HistoryRecordResult &result);
    void onLegacyImportCompleted(const ClipboardHistory::Core::HistoryLegacyImportResult &result);
    void onStopped();

private:
    QThread m_thread;
    HistoryWorker *m_worker = nullptr;
    int m_outstandingTasks = 0;
    qsizetype m_outstandingBytes = 0;
    QQueue<qsizetype> m_pendingPayloadBytes;
    int m_retentionHours = 0;
    int m_maximumEntries = 0;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_legacyImportPending = false;
};

} // namespace ClipboardHistory::Core
