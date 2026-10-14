#include "history_model.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace ClipboardHistory::Core {
namespace {

constexpr qint64 PageSize = 50;

QString kindLabel(const QString &kind)
{
    if (kind == QStringLiteral("text")) return QStringLiteral("文本");
    if (kind == QStringLiteral("link")) return QStringLiteral("链接");
    if (kind == QStringLiteral("path")) return QStringLiteral("文件");
    if (kind == QStringLiteral("image")) return QStringLiteral("图片");
    return QStringLiteral("其他");
}

} // namespace

HistoryModel::HistoryModel(HistoryTaskQueue *queue, QString imagesDirectory, QObject *parent)
    : QAbstractListModel(parent), m_queue(queue), m_imagesDirectory(std::move(imagesDirectory))
{
    Q_ASSERT(m_queue);
    connect(m_queue, &HistoryTaskQueue::pageReady, this, &HistoryModel::onPageReady);
    connect(m_queue, &HistoryTaskQueue::recordCompleted, this,
            [this](const HistoryRecordResult &result) {
        if (result.status && !result.incremented && m_totalCount >= 0)
            setTotalCount(m_totalCount + 1);
    });
    connect(m_queue, &HistoryTaskQueue::copyReady, this, &HistoryModel::onCopyReady);
    connect(m_queue, &HistoryTaskQueue::entryLoaded, this, &HistoryModel::onEntryLoaded);
    connect(m_queue, &HistoryTaskQueue::mutationCompleted, this, &HistoryModel::onMutationCompleted);
    connect(m_queue, &HistoryTaskQueue::legacyImportCompleted,
            this, &HistoryModel::onLegacyImportCompleted);
    connect(m_queue, &HistoryTaskQueue::retentionCompleted, this, [this](const HistoryMutationResult &result) {
        if (!result.status) emit retentionFailed();
        else if (result.removedCount > 0) {
            m_totalCountNeedsRefresh = true;
            refresh();
            emit retentionCompleted(result.removedCount, result.cleanupPending);
        }
    });
    connect(&m_sourceWatcher, &QFileSystemWatcher::directoryChanged,
            this, &HistoryModel::onSourceDirectoryChanged);
}

void HistoryModel::setMaximumEntries(int maximum)
{
    if (maximum < 0) maximum = 0;
    if (m_maximumEntries == maximum || !m_queue || !m_queue->setMaximumEntries(maximum)) return;
    m_maximumEntries = maximum;
    emit maximumEntriesChanged();
}

void HistoryModel::setRetentionHours(int hours)
{
    if (hours < 0 || hours > HistoryTaskQueue::maxRetentionHours()) hours = 0;
    if (m_retentionHours == hours || !m_queue || !m_queue->setRetentionHours(hours)) return;
    m_retentionHours = hours;
    emit retentionHoursChanged();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const HistoryPreview &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole: return entry.id;
    case ClipTextRole: {
        if (entry.kind == QStringLiteral("image"))
            return QStringLiteral("图片内容");
        QString preview = entry.content;
        if (entry.truncated)
            preview.append(QChar(0x2026));
        return preview;
    }
    case KindRole: return kindLabel(entry.kind);
    case TimeRole: {
        const QDateTime local = QDateTime::fromMSecsSinceEpoch(entry.createdAtMsUtc, QTimeZone::UTC)
                                    .toLocalTime();
        const QString time = local.toString(QStringLiteral("HH:mm"));
        return local.date() == QDate::currentDate()
            ? QStringLiteral("今天 %1").arg(time)
            : local.toString(QStringLiteral("MM-dd HH:mm"));
    }
    case UsesRole:
        return entry.usageCount > 1 ? QStringLiteral("使用 %1 次").arg(entry.usageCount) : QString();
    case SymbolRole:
        return entry.kind == QStringLiteral("link") ? QStringLiteral("↗")
            : entry.kind == QStringLiteral("image") ? QStringLiteral("▧")
                                                    : QStringLiteral("▤");
    case FavoriteRole: return entry.favorite;
    case ExpandableRole: return entry.truncated;
    case ImageUrlRole:
        return entry.kind == QStringLiteral("image")
            ? QUrl::fromLocalFile(QDir(m_imagesDirectory).filePath(entry.imageRef + QStringLiteral(".png")))
            : QUrl();
    case SourceMissingRole: return m_missingSourceIds.contains(entry.id);
    default: return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {{IdRole, "entryId"}, {ClipTextRole, "clipText"}, {KindRole, "kind"},
            {TimeRole, "time"}, {UsesRole, "uses"}, {SymbolRole, "symbol"},
            {FavoriteRole, "favorite"}, {ExpandableRole, "expandable"}, {ImageUrlRole, "imageUrl"},
            {SourceMissingRole, "sourceMissing"}};
}

bool HistoryModel::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_hasMore && !m_loading;
}

void HistoryModel::fetchMore(const QModelIndex &parent)
{
    if (canFetchMore(parent))
        requestNextPage();
}

void HistoryModel::refresh()
{
    if (m_loading) {
        m_refreshPending = true;
        return;
    }

    beginResetModel();
    m_entries.clear();
    m_missingSourceIds.clear();
    m_sourcePaths.clear();
    m_dirtySourceChecks.clear();
    if (!m_sourceWatcher.directories().isEmpty())
        m_sourceWatcher.removePaths(m_sourceWatcher.directories());
    m_hasMore = true;
    setLoading(true);
    endResetModel();
    emit countChanged();
    setHasError(false);
    requestNextPage();
}

void HistoryModel::setFilter(const QString &search, const QString &kind)
{
    if (m_search == search && m_kind == kind)
        return;
    m_search = search;
    m_kind = kind;
    refresh();
}

void HistoryModel::setFavorite(int row, bool favorite)
{
    if (row < 0 || row >= m_entries.size() || !m_queue
        || !m_queue->setFavorite(m_entries.at(row).id, favorite))
        emit mutationFailed();
}

void HistoryModel::removeItem(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        emit mutationFailed();
        return;
    }
    removeItemById(m_entries.at(row).id);
}

void HistoryModel::removeItemById(qint64 id)
{
    if (id <= 0 || !m_queue || !m_queue->remove(id))
        emit mutationFailed();
}

void HistoryModel::clearHistory()
{
    if (!m_queue || !m_queue->clear())
        emit mutationFailed();
}

bool HistoryModel::importLegacyDatabase(const QString &databaseFilePath)
{
    const QFileInfo database(databaseFilePath);
    if (!database.isFile() || !m_queue
        || !m_queue->importLegacy(database.canonicalFilePath(),
                                  QDir(database.absolutePath()).filePath(QStringLiteral("images")))) {
        emit legacyImportFailed();
        return false;
    }
    return true;
}

void HistoryModel::copyItem(int row)
{
    if (row < 0 || row >= m_entries.size() || !m_queue) {
        emit copyFailed();
        return;
    }
    const qint64 entryId = m_entries.at(row).id;
    if (!m_queue->requestCopy(entryId)) {
        emit copyFailed();
        return;
    }
    m_pendingCopyIds.enqueue(entryId);
}

void HistoryModel::setSourceMissing(qint64 entryId, bool missing, const QStringList &paths)
{
    if (entryId <= 0)
        return;

    const bool changed = m_missingSourceIds.contains(entryId) != missing;
    if (!paths.isEmpty())
        m_sourcePaths.insert(entryId, paths);
    if (missing) {
        m_missingSourceIds.insert(entryId);
    } else {
        m_missingSourceIds.remove(entryId);
    }
    refreshSourceDirectoryWatches();

    if (changed) {
        const auto entry = std::find_if(m_entries.cbegin(), m_entries.cend(),
            [entryId](const HistoryPreview &candidate) { return candidate.id == entryId; });
        if (entry != m_entries.cend()) {
            const int row = static_cast<int>(std::distance(m_entries.cbegin(), entry));
            emit dataChanged(index(row, 0), index(row, 0), {SourceMissingRole});
        }
    }
}

void HistoryModel::refreshSourceDirectoryWatches()
{
    QSet<QString> wanted;
    for (const QStringList &paths : m_sourcePaths) {
        for (const QString &path : paths) {
            QString directory = QDir::cleanPath(QFileInfo(path).absolutePath());
            while (!QDir(directory).exists()) {
                const QString parent = QFileInfo(directory).absolutePath();
                if (parent == directory)
                    break;
                directory = parent;
            }
            if (QDir(directory).exists())
                wanted.insert(directory);
        }
    }

    for (const QString &directory : m_sourceWatcher.directories()) {
        if (!wanted.contains(QDir::cleanPath(directory)))
            m_sourceWatcher.removePath(directory);
    }
    QSet<QString> current;
    for (const QString &directory : m_sourceWatcher.directories())
        current.insert(QDir::cleanPath(directory));
    for (const QString &directory : wanted) {
        if (!current.contains(directory))
            m_sourceWatcher.addPath(directory);
    }
}

void HistoryModel::loadEntry(qint64 id)
{
    if (id <= 0 || !m_queue || !m_queue->loadEntry(id))
        emit entryContentLoadFailed(id);
}

void HistoryModel::onPageReady(const HistoryPreviewPage &page)
{
    if (!page.status) {
        m_hasMore = false;
        setHasError(true);
    } else {
        if (page.totalCount >= 0) {
            setTotalCount(page.totalCount);
            m_totalCountNeedsRefresh = false;
        }
        if (!page.entries.isEmpty()) {
            for (const HistoryPreview &entry : page.entries) {
                if (entry.kind != QStringLiteral("path"))
                    continue;
                setSourceMissing(entry.id, entry.sourceMissing, entry.sourcePaths);
            }
            const int first = m_entries.size();
            const int last = first + page.entries.size() - 1;
            beginInsertRows({}, first, last);
            for (const HistoryPreview &entry : page.entries)
                m_entries.append(entry);
            endInsertRows();
            emit countChanged();
            m_hasMore = page.entries.size() == PageSize;
        } else {
            m_hasMore = false;
        }
    }

    setLoading(false);

    if (m_refreshPending) {
        m_refreshPending = false;
        refresh();
    }
}

void HistoryModel::onCopyReady(const HistoryCopyResult &result)
{
    const qint64 entryId = m_pendingCopyIds.isEmpty() ? 0 : m_pendingCopyIds.dequeue();
    if (!result.status) {
        emit copyFailed();
    } else if (result.kind == QStringLiteral("path")) {
        const QStringList paths = result.content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (result.sourceMissing) {
            setSourceMissing(entryId, true, paths);
            emit copySourceMissing();
            return;
        }
        if (paths.isEmpty()) {
            emit copyFailed();
            return;
        }
        setSourceMissing(entryId, false, paths);
        emit copyContentReady(result.kind, result.content);
    } else if (result.kind == QStringLiteral("image")) {
        emit copyImageReady(result.image);
    } else {
        emit copyContentReady(result.kind, result.content);
    }
}

void HistoryModel::onSourceDirectoryChanged(const QString &path)
{
    const QString changedDirectory = QDir::cleanPath(path);
    const QString directoryPrefix = changedDirectory.endsWith(QLatin1Char('/'))
                                        ? changedDirectory : changedDirectory + QLatin1Char('/');
    for (auto it = m_sourcePaths.cbegin(); it != m_sourcePaths.cend(); ++it) {
        const bool watchesDirectory = std::any_of(it.value().cbegin(), it.value().cend(),
            [&changedDirectory, &directoryPrefix](const QString &sourcePath) {
                const QString directory = QDir::cleanPath(QFileInfo(sourcePath).absolutePath());
                return directory == changedDirectory
                       || directory.startsWith(directoryPrefix, Qt::CaseInsensitive);
            });
        if (watchesDirectory) {
            if (m_pendingSourceChecks.contains(it.key()))
                m_dirtySourceChecks.insert(it.key());
            else if (m_queue && m_queue->loadEntry(it.key()))
                m_pendingSourceChecks.insert(it.key());
        }
    }
    refreshSourceDirectoryWatches();
}

void HistoryModel::onEntryLoaded(qint64 id, const HistoryCopyResult &result)
{
    m_pendingSourceChecks.remove(id);
    if (m_dirtySourceChecks.remove(id) && m_sourcePaths.contains(id)
        && m_queue && m_queue->loadEntry(id))
        m_pendingSourceChecks.insert(id);
    if (result.status && result.kind == QStringLiteral("path")
        && std::any_of(m_entries.cbegin(), m_entries.cend(), [id](const HistoryPreview &entry) {
            return entry.id == id;
        }))
        setSourceMissing(id, result.sourceMissing,
                         result.content.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    if (!result.status || result.kind == QStringLiteral("image")) {
        emit entryContentLoadFailed(id);
        return;
    }
    emit entryContentLoaded(id, result.kind, result.content);
}

void HistoryModel::onMutationCompleted(const HistoryMutationResult &result)
{
    if (!result.status) {
        emit mutationFailed();
        return;
    }
    emit mutationCompleted(result.cleanupPending);
    m_totalCountNeedsRefresh = true;
    refresh();
}

void HistoryModel::onLegacyImportCompleted(const HistoryLegacyImportResult &result)
{
    if (!result.status) {
        emit legacyImportFailed();
        return;
    }
    emit legacyImportCompleted(result.importedRows, result.alreadyImportedRows,
                               result.scan.unsupportedRows, result.cleanupPending);
    m_totalCountNeedsRefresh = true;
    refresh();
}

void HistoryModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void HistoryModel::setHasError(bool hasError)
{
    if (m_hasError == hasError)
        return;
    m_hasError = hasError;
    emit hasErrorChanged();
}

void HistoryModel::setTotalCount(qint64 count)
{
    if (m_totalCount == count)
        return;
    m_totalCount = count;
    emit totalCountChanged();
}

void HistoryModel::requestNextPage()
{
    setLoading(true);
    const bool includeTotalCount = m_totalCount < 0 || m_totalCountNeedsRefresh;
    if (!m_queue || !m_queue->requestPage(m_entries.size(), PageSize, m_search, m_kind,
                                          includeTotalCount)) {
        setLoading(false);
        m_hasMore = false;
        setHasError(true);
    }
}

} // namespace ClipboardHistory::Core
