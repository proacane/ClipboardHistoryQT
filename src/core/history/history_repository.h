#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

class QThread;

namespace ClipboardHistory::Core {

enum class RepositoryError {
    None,
    WrongThread,
    InvalidArgument,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    OpenFailed,
    SqlError,
    LegacySourceChanged,
    UnsupportedSchemaVersion
};

struct RepositoryStatus {
    RepositoryError error = RepositoryError::None;

    explicit operator bool() const { return error == RepositoryError::None; }
};

struct NewHistoryEntry {
    QString kind;
    QString content;
    QString imageRef;
    qint64 createdAtMsUtc = 0;
};

struct HistoryEntry : NewHistoryEntry {
    qint64 id = 0;
    qint64 usageCount = 1;
    bool favorite = false;
};

struct HistoryPage {
    RepositoryStatus status;
    QVector<HistoryEntry> entries;
};

struct HistoryPreview : NewHistoryEntry {
    qint64 id = 0;
    qint64 usageCount = 1;
    bool truncated = false;
    bool favorite = false;
    QStringList sourcePaths;
    bool sourceMissing = false;
};

struct HistoryPreviewPage {
    RepositoryStatus status;
    QVector<HistoryPreview> entries;
    qint64 totalCount = -1;
};

struct LegacyImportRecordResult {
    RepositoryStatus status;
    qint64 historyId = 0;
    bool inserted = false;
};

class HistoryRepository final {
public:
    explicit HistoryRepository(QString databaseFilePath);
    ~HistoryRepository();

    HistoryRepository(const HistoryRepository&) = delete;
    HistoryRepository& operator=(const HistoryRepository&) = delete;

    RepositoryStatus initialize();
    RepositoryStatus insert(const NewHistoryEntry& entry, qint64& id);
    RepositoryStatus recordOrIncrement(const NewHistoryEntry& entry, qint64& id, bool& incremented);
    LegacyImportRecordResult importLegacyEntry(const QString &sourcePath,
                                               const QString &sourceSha256,
                                               qint64 sourceRowId,
                                               const NewHistoryEntry &entry);
    RepositoryStatus hasImageReference(const QString &imageRef, bool &inUse);
    RepositoryStatus get(qint64 id, HistoryEntry& entry);
    RepositoryStatus update(qint64 id, const NewHistoryEntry& entry);
    RepositoryStatus setFavorite(qint64 id, bool favorite);
    HistoryPage page(qint64 offset, qint64 limit);
    HistoryPreviewPage previewPage(qint64 offset, qint64 limit,
                                   const QString &search = {}, const QString &kind = {},
                                   bool includeTotalCount = false);
    RepositoryStatus remove(qint64 id, QString *orphanImageRef = nullptr);
    RepositoryStatus clear(QStringList *orphanImageRefs = nullptr);
    RepositoryStatus removeExpired(qint64 cutoffMsUtc, int limit, qint64 &removedCount,
                                   QStringList &orphanImageRefs);
    RepositoryStatus enforceRetention(qint64 cutoffMsUtc, qint64 maximumEntries, int limit,
                                      qint64 &removedCount, QStringList &orphanImageRefs);

private:
    RepositoryStatus checkThread() const;
    void closeConnection();

    QString m_databaseFilePath;
    QString m_connectionName;
    QThread* m_ownerThread;
    QSqlDatabase m_database;
};

} // namespace ClipboardHistory::Core
