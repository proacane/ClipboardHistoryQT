#include "history_service.h"

#include <QDateTime>
#include <QFileInfo>
#include <QThread>

#include <utility>
#include <algorithm>

namespace ClipboardHistory::Core {
namespace {

HistoryServiceStatus failure(HistoryServiceError error)
{
    return {error};
}

bool sourceFilesMissing(const QStringList &paths)
{
    return paths.isEmpty() || std::any_of(paths.cbegin(), paths.cend(), [](const QString &path) {
        return !QFileInfo::exists(path);
    });
}

} // namespace

HistoryService::HistoryService(QString databaseFilePath, QString imagesDirectory)
    : m_ownerThread(QThread::currentThread()),
      m_repository(std::move(databaseFilePath)),
      m_imageStore(std::move(imagesDirectory))
{
}

HistoryServiceStatus HistoryService::initialize()
{
    if (QThread::currentThread() != m_ownerThread)
        return failure(HistoryServiceError::WrongThread);
    const RepositoryStatus status = m_repository.initialize();
    if (!status) {
        return {HistoryServiceError::RepositoryFailure, status.error};
    }
    m_initialized = true;
    return {};
}

HistoryRecordResult HistoryService::record(const HistoryCapture& capture)
{
    if (QThread::currentThread() != m_ownerThread)
        return {failure(HistoryServiceError::WrongThread)};
    if (!m_initialized)
        return {failure(HistoryServiceError::NotInitialized)};
    const bool isImage = capture.kind == QStringLiteral("image");
    if (capture.kind.isEmpty() || (isImage && capture.image.isNull()) || (!isImage && !capture.image.isNull()))
        return {failure(HistoryServiceError::InvalidEntry)};

    NewHistoryEntry entry;
    entry.kind = capture.kind;
    entry.content = capture.kind == QStringLiteral("image") ? QStringLiteral("") : capture.content;
    entry.createdAtMsUtc = QDateTime::currentMSecsSinceEpoch();

    if (capture.kind == QStringLiteral("image")) {
        const ImageStoreStatus imageStatus = m_imageStore.store(capture.image);
        if (!imageStatus) {
            return {{HistoryServiceError::ImageStoreFailure,
                     RepositoryError::None,
                     imageStatus.error}};
        }
        entry.imageRef = imageStatus.reference;
    }

    HistoryRecordResult result;
    const RepositoryStatus status = m_repository.recordOrIncrement(entry, result.id, result.incremented);
    if (!status)
        result.status = {HistoryServiceError::RepositoryFailure, status.error};
    return result;
}

HistoryPage HistoryService::page(qint64 offset, qint64 limit)
{
    if (QThread::currentThread() != m_ownerThread)
        return {{RepositoryError::WrongThread}, {}};
    return m_repository.page(offset, limit);
}

HistoryPreviewPage HistoryService::previewPage(qint64 offset, qint64 limit,
                                                const QString &search, const QString &kind,
                                                bool includeTotalCount)
{
    if (QThread::currentThread() != m_ownerThread)
        return {{RepositoryError::WrongThread}, {}};
    auto page = m_repository.previewPage(offset, limit, search, kind, includeTotalCount);
    for (auto &entry : page.entries) {
        if (entry.kind == QStringLiteral("path"))
            entry.sourceMissing = sourceFilesMissing(entry.sourcePaths);
    }
    return page;
}

HistoryCopyResult HistoryService::loadEntry(qint64 id)
{
    if (QThread::currentThread() != m_ownerThread)
        return {{HistoryServiceError::WrongThread}, {}, {}, {}};
    if (!m_initialized)
        return {failure(HistoryServiceError::NotInitialized), {}, {}, {}};

    HistoryEntry entry;
    const RepositoryStatus repositoryStatus = m_repository.get(id, entry);
    if (!repositoryStatus)
        return {{HistoryServiceError::RepositoryFailure, repositoryStatus.error}, {}, {}, {}};

    if (entry.kind == QStringLiteral("image")) {
        QImage image;
        const ImageStoreStatus imageStatus = m_imageStore.load(entry.imageRef, image);
        if (!imageStatus)
            return {{HistoryServiceError::ImageStoreFailure, RepositoryError::None, imageStatus.error}, {}, {}, {}};
        return {{}, entry.kind, {}, std::move(image)};
    }
    const bool missing = entry.kind == QStringLiteral("path")
                         && sourceFilesMissing(entry.content.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    return {{}, entry.kind, std::move(entry.content), {}, missing};
}

HistoryCopyResult HistoryService::copyEntry(qint64 id)
{
    return loadEntry(id);
}

HistoryMutationResult HistoryService::setFavorite(qint64 id, bool favorite)
{
    if (QThread::currentThread() != m_ownerThread)
        return {failure(HistoryServiceError::WrongThread)};
    if (!m_initialized)
        return {failure(HistoryServiceError::NotInitialized)};
    const RepositoryStatus status = m_repository.setFavorite(id, favorite);
    return {status ? HistoryServiceStatus{}
                   : HistoryServiceStatus{HistoryServiceError::RepositoryFailure, status.error}};
}

HistoryMutationResult HistoryService::remove(qint64 id)
{
    if (QThread::currentThread() != m_ownerThread)
        return {failure(HistoryServiceError::WrongThread)};
    if (!m_initialized)
        return {failure(HistoryServiceError::NotInitialized)};
    QString orphanImageRef;
    const RepositoryStatus status = m_repository.remove(id, &orphanImageRef);
    if (!status)
        return {{HistoryServiceError::RepositoryFailure, status.error}};
    if (orphanImageRef.isEmpty())
        return {};
    const ImageStoreStatus cleanup = m_imageStore.remove(orphanImageRef);
    return {{}, cleanup.error != ImageStoreError::None && cleanup.error != ImageStoreError::NotFound};
}

HistoryMutationResult HistoryService::clear()
{
    if (QThread::currentThread() != m_ownerThread)
        return {failure(HistoryServiceError::WrongThread)};
    if (!m_initialized)
        return {failure(HistoryServiceError::NotInitialized)};
    QStringList orphanImageRefs;
    const RepositoryStatus status = m_repository.clear(&orphanImageRefs);
    if (!status)
        return {{HistoryServiceError::RepositoryFailure, status.error}};
    bool cleanupPending = false;
    for (const QString &reference : orphanImageRefs) {
        const ImageStoreStatus cleanup = m_imageStore.remove(reference);
        cleanupPending |= cleanup.error != ImageStoreError::None
            && cleanup.error != ImageStoreError::NotFound;
    }
    return {{}, cleanupPending};
}

HistoryMutationResult HistoryService::removeExpired(qint64 cutoffMsUtc)
{
    return enforceRetention(cutoffMsUtc, 0);
}

HistoryMutationResult HistoryService::enforceRetention(qint64 cutoffMsUtc, qint64 maximumEntries)
{
    if (QThread::currentThread() != m_ownerThread) return {failure(HistoryServiceError::WrongThread)};
    if (!m_initialized) return {failure(HistoryServiceError::NotInitialized)};
    HistoryMutationResult result;
    QStringList orphans;
    const auto status = m_repository.enforceRetention(cutoffMsUtc, maximumEntries, 500, result.removedCount, orphans);
    if (!status) return {{HistoryServiceError::RepositoryFailure, status.error}};
    for (const auto &ref : orphans) {
        const auto cleanup = m_imageStore.remove(ref);
        result.cleanupPending |= cleanup.error != ImageStoreError::None && cleanup.error != ImageStoreError::NotFound;
    }
    return result;
}

HistoryLegacyImportResult HistoryService::importLegacy(const QString &databaseFilePath,
                                                       const QString &imagesDirectory)
{
    HistoryLegacyImportResult result;
    if (QThread::currentThread() != m_ownerThread) {
        result.status = failure(HistoryServiceError::WrongThread);
        return result;
    }
    if (!m_initialized) {
        result.status = failure(HistoryServiceError::NotInitialized);
        return result;
    }

    const QString sourcePath = QFileInfo(databaseFilePath).canonicalFilePath();
    HistoryServiceStatus rowFailure;
    const auto cleanupIfUnreferenced = [this, &result](const QString &imageRef) {
        if (imageRef.isEmpty())
            return;
        bool inUse = true;
        const RepositoryStatus referenceStatus = m_repository.hasImageReference(imageRef, inUse);
        if (!referenceStatus) {
            result.cleanupPending = true;
        } else if (!inUse) {
            const ImageStoreStatus cleanup = m_imageStore.remove(imageRef);
            result.cleanupPending |= cleanup.error != ImageStoreError::None
                && cleanup.error != ImageStoreError::NotFound;
        }
    };
    result.scan = LegacyImporter::inspect(
        databaseFilePath, imagesDirectory,
        [this, &result, &rowFailure, &sourcePath, &cleanupIfUnreferenced](
            const QString &sourceSha256, const LegacyImportEntry &legacy) {
            NewHistoryEntry entry;
            entry.kind = legacy.kind;
            entry.content = legacy.content;
            entry.createdAtMsUtc = legacy.createdAtMsUtc;

            if (legacy.kind == QStringLiteral("image")) {
                const ImageStoreStatus imageStatus = m_imageStore.store(legacy.image);
                if (!imageStatus) {
                    rowFailure = {HistoryServiceError::ImageStoreFailure,
                                  RepositoryError::None, imageStatus.error};
                    return false;
                }
                entry.imageRef = imageStatus.reference;
            }

            const LegacyImportRecordResult imported = m_repository.importLegacyEntry(
                sourcePath, sourceSha256, legacy.sourceId, entry);
            if (!imported.status) {
                rowFailure = {HistoryServiceError::RepositoryFailure, imported.status.error};
                cleanupIfUnreferenced(entry.imageRef);
                return false;
            }
            if (imported.inserted)
                ++result.importedRows;
            else {
                ++result.alreadyImportedRows;
                cleanupIfUnreferenced(entry.imageRef);
            }
            return true;
        });

    if (!rowFailure) {
        result.status = rowFailure;
    } else if (!result.scan) {
        result.status = {HistoryServiceError::LegacyImportFailure};
    }
    return result;
}

} // namespace ClipboardHistory::Core
