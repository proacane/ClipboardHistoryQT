#pragma once

#include "history_repository.h"
#include "image_store.h"
#include "../import/legacy_importer.h"

#include <QImage>
#include <QString>

class QThread;

namespace ClipboardHistory::Core {

struct HistoryCapture {
    QString kind;
    QString content;
    QImage image;
};

enum class HistoryServiceError {
    None,
    WrongThread,
    NotInitialized,
    InvalidEntry,
    ImageStoreFailure,
    RepositoryFailure,
    LegacyImportFailure
};

struct HistoryServiceStatus {
    HistoryServiceError error = HistoryServiceError::None;
    RepositoryError repositoryError = RepositoryError::None;
    ImageStoreError imageStoreError = ImageStoreError::None;

    explicit operator bool() const { return error == HistoryServiceError::None; }
};

struct HistoryRecordResult {
    HistoryServiceStatus status;
    qint64 id = 0;
    bool incremented = false;
};

struct HistoryCopyResult {
    HistoryServiceStatus status;
    QString kind;
    QString content;
    QImage image;
    bool sourceMissing = false;
};

struct HistoryMutationResult {
    HistoryServiceStatus status;
    bool cleanupPending = false;
    qint64 removedCount = 0;
};

struct HistoryLegacyImportResult {
    HistoryServiceStatus status;
    LegacyImportReport scan;
    qint64 importedRows = 0;
    qint64 alreadyImportedRows = 0;
    bool cleanupPending = false;
};

class HistoryService final {
public:
    HistoryService(QString databaseFilePath, QString imagesDirectory);

    HistoryServiceStatus initialize();
    HistoryRecordResult record(const HistoryCapture& capture);
    HistoryPage page(qint64 offset, qint64 limit);
    HistoryPreviewPage previewPage(qint64 offset, qint64 limit,
                                   const QString &search = {}, const QString &kind = {},
                                   bool includeTotalCount = false);
    HistoryCopyResult loadEntry(qint64 id);
    HistoryCopyResult copyEntry(qint64 id);
    HistoryMutationResult setFavorite(qint64 id, bool favorite);
    HistoryMutationResult remove(qint64 id);
    HistoryMutationResult clear();
    HistoryMutationResult removeExpired(qint64 cutoffMsUtc);
    HistoryMutationResult enforceRetention(qint64 cutoffMsUtc, qint64 maximumEntries);
    HistoryLegacyImportResult importLegacy(const QString &databaseFilePath,
                                           const QString &imagesDirectory);

private:
    QThread *m_ownerThread;
    HistoryRepository m_repository;
    ImageStore m_imageStore;
    bool m_initialized = false;
};

} // namespace ClipboardHistory::Core
