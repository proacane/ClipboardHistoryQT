#pragma once

#include "history_task_queue.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace ClipboardHistory::Core {

class HistoryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int maximumEntries READ maximumEntries WRITE setMaximumEntries NOTIFY maximumEntriesChanged)
    Q_PROPERTY(int retentionHours READ retentionHours WRITE setRetentionHours NOTIFY retentionHoursChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(qint64 totalCount READ totalCount NOTIFY totalCountChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ClipTextRole,
        KindRole,
        TimeRole,
        UsesRole,
        SymbolRole,
        FavoriteRole,
        ExpandableRole,
        ImageUrlRole,
        SourceMissingRole
    };
    Q_ENUM(Role)

    explicit HistoryModel(HistoryTaskQueue *queue, QString imagesDirectory = {},
                          QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    bool loading() const { return m_loading; }
    bool hasError() const { return m_hasError; }
    qint64 totalCount() const { return m_totalCount; }
    int maximumEntries() const { return m_maximumEntries; }
    void setMaximumEntries(int maximum);
    int retentionHours() const { return m_retentionHours; }
    void setRetentionHours(int hours);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void copyItem(int row);
    Q_INVOKABLE void loadEntry(qint64 id);
    Q_INVOKABLE void setFilter(const QString &search, const QString &kind);
    Q_INVOKABLE void setFavorite(int row, bool favorite);
    Q_INVOKABLE void removeItem(int row);
    Q_INVOKABLE void removeItemById(qint64 id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE bool importLegacyDatabase(const QString &databaseFilePath);

signals:
    void maximumEntriesChanged();
    void retentionHoursChanged();
    void retentionCompleted(qint64 removedCount, bool cleanupPending);
    void retentionFailed();
    void loadingChanged();
    void hasErrorChanged();
    void copyContentReady(const QString &kind, const QString &content);
    void copyImageReady(const QImage &image);
    void copyFailed();
    void copySourceMissing();
    void entryContentLoaded(qint64 id, const QString &kind, const QString &content);
    void entryContentLoadFailed(qint64 id);
    void mutationFailed();
    void mutationCompleted(bool cleanupPending);
    void legacyImportCompleted(qint64 importedRows, qint64 alreadyImportedRows,
                                qint64 skippedRows, bool cleanupPending);
    void legacyImportFailed();
    void countChanged();
    void totalCountChanged();

private slots:
    void onPageReady(const ClipboardHistory::Core::HistoryPreviewPage &page);
    void onCopyReady(const ClipboardHistory::Core::HistoryCopyResult &result);
    void onEntryLoaded(qint64 id, const ClipboardHistory::Core::HistoryCopyResult &result);
    void onMutationCompleted(const ClipboardHistory::Core::HistoryMutationResult &result);
    void onLegacyImportCompleted(const ClipboardHistory::Core::HistoryLegacyImportResult &result);
    void onSourceDirectoryChanged(const QString &path);

private:
    void setLoading(bool loading);
    void setHasError(bool hasError);
    void setTotalCount(qint64 count);
    void setSourceMissing(qint64 entryId, bool missing, const QStringList &paths = {});
    void refreshSourceDirectoryWatches();
    void requestNextPage();

    QPointer<HistoryTaskQueue> m_queue;
    QString m_imagesDirectory;
    QVector<HistoryPreview> m_entries;
    QQueue<qint64> m_pendingCopyIds;
    QSet<qint64> m_missingSourceIds;
    QHash<qint64, QStringList> m_sourcePaths;
    QSet<qint64> m_pendingSourceChecks;
    QSet<qint64> m_dirtySourceChecks;
    QFileSystemWatcher m_sourceWatcher;
    int m_retentionHours = 0;
    int m_maximumEntries = 0;
    bool m_loading = false;
    bool m_hasError = false;
    bool m_hasMore = false;
    bool m_refreshPending = false;
    bool m_totalCountNeedsRefresh = true;
    qint64 m_totalCount = -1;
    QString m_search;
    QString m_kind;
};

} // namespace ClipboardHistory::Core
