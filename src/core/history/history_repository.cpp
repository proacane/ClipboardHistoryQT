#include "history_repository.h"
#include <algorithm>

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <array>
#include <limits>
#include <utility>

namespace ClipboardHistory::Core {
namespace {

constexpr int SchemaVersion = 5;

RepositoryStatus failure(RepositoryError error)
{
    return {error};
}

QByteArray duplicateHash(const QString& kind, const QString& content, const QString& imageRef)
{
    QByteArray key;
    const std::array<QString, 3> fields{kind, content, imageRef};
    for (const QString& field : fields) {
        const QByteArray bytes = field.toUtf8();
        const quint64 length = static_cast<quint64>(bytes.size());
        for (int shift = 56; shift >= 0; shift -= 8)
            key.append(static_cast<char>((length >> shift) & 0xff));
        key.append(bytes);
    }
    return QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex();
}

bool backfillDuplicateHashes(QSqlDatabase& database)
{
    struct Row {
        qint64 id;
        QString kind;
        QString content;
        QString imageRef;
    };

    qint64 lastId = 0;
    for (;;) {
        QVector<Row> rows;
        {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "SELECT id, kind, content, image_ref FROM history_entries "
                "WHERE id > :last_id ORDER BY id LIMIT 256"));
            query.bindValue(QStringLiteral(":last_id"), lastId);
            if (!query.exec())
                return false;
            while (query.next()) {
                rows.append({query.value(0).toLongLong(), query.value(1).toString(),
                             query.value(2).toString(), query.value(3).toString()});
            }
            if (query.lastError().isValid())
                return false;
        }
        if (rows.isEmpty())
            return true;

        QSqlQuery update(database);
        update.prepare(QStringLiteral("UPDATE history_entries SET content_hash = :hash WHERE id = :id"));
        for (const Row& row : rows) {
            update.bindValue(QStringLiteral(":hash"), QString::fromLatin1(duplicateHash(row.kind, row.content, row.imageRef)));
            update.bindValue(QStringLiteral(":id"), row.id);
            if (!update.exec())
                return false;
            lastId = row.id;
        }
    }
}

} // namespace

HistoryRepository::HistoryRepository(QString databaseFilePath)
    : m_databaseFilePath(std::move(databaseFilePath)),
      m_connectionName(QStringLiteral("HistoryRepository_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))),
      m_ownerThread(QThread::currentThread())
{
}

HistoryRepository::~HistoryRepository()
{
    Q_ASSERT(QThread::currentThread() == m_ownerThread);
    closeConnection();
}

RepositoryStatus HistoryRepository::checkThread() const
{
    return QThread::currentThread() == m_ownerThread
        ? RepositoryStatus{}
        : failure(RepositoryError::WrongThread);
}

void HistoryRepository::closeConnection()
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
    }
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

RepositoryStatus HistoryRepository::initialize()
{
    if (const auto status = checkThread(); !status)
        return status;
    if (m_database.isValid())
        return failure(RepositoryError::AlreadyInitialized);
    if (m_databaseFilePath.isEmpty())
        return failure(RepositoryError::InvalidArgument);

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databaseFilePath);
    if (!m_database.open()) {
        closeConnection();
        return failure(RepositoryError::OpenFailed);
    }
    if (!m_database.transaction()) {
        closeConnection();
        return failure(RepositoryError::SqlError);
    }

    RepositoryError setupError = RepositoryError::None;
    {
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
            setupError = RepositoryError::SqlError;
        } else {
            const int schemaVersion = query.value(0).toInt();
            query.finish();
            if (schemaVersion < 0 || schemaVersion > SchemaVersion) {
                setupError = RepositoryError::UnsupportedSchemaVersion;
            } else if (schemaVersion == 0
                       && (!query.exec(QStringLiteral(
                               "CREATE TABLE history_entries ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               "kind TEXT NOT NULL,"
                               "content TEXT NOT NULL,"
                               "image_ref TEXT NOT NULL,"
                               "created_at_ms_utc INTEGER NOT NULL,"
                               "usage_count INTEGER NOT NULL DEFAULT 1,"
                               "content_hash TEXT NOT NULL,"
                               "favorite INTEGER NOT NULL DEFAULT 0)"))
                           || !query.exec(QStringLiteral(
                               "CREATE INDEX history_entries_page_idx "
                               "ON history_entries(created_at_ms_utc DESC, id DESC)"))
                           || !query.exec(QStringLiteral(
                               "CREATE INDEX history_entries_content_hash_idx ON history_entries(content_hash)"))
                           || !query.exec(QStringLiteral("PRAGMA user_version = 4")))) {
                setupError = RepositoryError::SqlError;
            } else if (schemaVersion == 1) {
                if (!query.exec(QStringLiteral(
                        "ALTER TABLE history_entries ADD COLUMN usage_count INTEGER NOT NULL DEFAULT 1"))
                    || !query.exec(QStringLiteral(
                        "ALTER TABLE history_entries ADD COLUMN content_hash TEXT NOT NULL DEFAULT ''"))
                    || !query.exec(QStringLiteral(
                        "CREATE INDEX history_entries_content_hash_idx ON history_entries(content_hash)"))
                    || !backfillDuplicateHashes(m_database)) {
                    setupError = RepositoryError::SqlError;
                }
            } else if (schemaVersion == 2) {
                if (!query.exec(QStringLiteral(
                        "ALTER TABLE history_entries ADD COLUMN content_hash TEXT NOT NULL DEFAULT ''"))
                    || !query.exec(QStringLiteral(
                        "CREATE INDEX history_entries_content_hash_idx ON history_entries(content_hash)"))
                    || !backfillDuplicateHashes(m_database)) {
                    setupError = RepositoryError::SqlError;
                }
            }
            if (setupError == RepositoryError::None && schemaVersion >= 1 && schemaVersion <= 3) {
                if (!query.exec(QStringLiteral(
                        "ALTER TABLE history_entries ADD COLUMN favorite INTEGER NOT NULL DEFAULT 0")))
                    setupError = RepositoryError::SqlError;
                else if (!query.exec(QStringLiteral("PRAGMA user_version = 4")))
                    setupError = RepositoryError::SqlError;
            }
            if (setupError == RepositoryError::None
                && (!query.exec(QStringLiteral(
                        "CREATE TABLE IF NOT EXISTS legacy_import_sources ("
                        "source_path TEXT PRIMARY KEY, source_sha256 TEXT NOT NULL)"))
                    || !query.exec(QStringLiteral(
                        "CREATE TABLE IF NOT EXISTS legacy_import_rows ("
                        "source_sha256 TEXT NOT NULL, source_row_id INTEGER NOT NULL, "
                        "history_id INTEGER NOT NULL, "
                        "PRIMARY KEY(source_sha256, source_row_id))"))
                    || !query.exec(QStringLiteral(
                        "CREATE INDEX IF NOT EXISTS history_entries_retention_idx "
                        "ON history_entries(favorite, created_at_ms_utc, id)"))
                    || !query.exec(QStringLiteral(
                        "CREATE INDEX IF NOT EXISTS history_entries_image_ref_idx ON history_entries(image_ref)"))
                    || !query.exec(QStringLiteral("PRAGMA user_version = 5")))) {
                setupError = RepositoryError::SqlError;
            }
        }
    }

    if (setupError != RepositoryError::None) {
        m_database.rollback();
        closeConnection();
        return failure(setupError);
    }
    if (!m_database.commit()) {
        m_database.rollback();
        closeConnection();
        return failure(RepositoryError::SqlError);
    }
    return {};
}

RepositoryStatus HistoryRepository::insert(const NewHistoryEntry& entry, qint64& id)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (entry.kind.isEmpty())
        return failure(RepositoryError::InvalidArgument);

    const QString content = entry.content.isNull() ? QStringLiteral("") : entry.content;
    const QString imageRef = entry.imageRef.isNull() ? QStringLiteral("") : entry.imageRef;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, usage_count, content_hash) "
        "VALUES(:kind, :content, :image_ref, :created_at_ms_utc, 1, :content_hash)"));
    query.bindValue(QStringLiteral(":kind"), entry.kind);
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":image_ref"), imageRef);
    query.bindValue(QStringLiteral(":created_at_ms_utc"), entry.createdAtMsUtc);
    query.bindValue(QStringLiteral(":content_hash"), QString::fromLatin1(duplicateHash(entry.kind, content, imageRef)));
    if (!query.exec())
        return failure(RepositoryError::SqlError);

    id = query.lastInsertId().toLongLong();
    return {};
}

RepositoryStatus HistoryRepository::recordOrIncrement(const NewHistoryEntry& entry, qint64& id, bool& incremented)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (entry.kind.isEmpty())
        return failure(RepositoryError::InvalidArgument);
    id = 0;
    incremented = false;

    const QString content = entry.content.isNull() ? QStringLiteral("") : entry.content;
    const QString imageRef = entry.imageRef.isNull() ? QStringLiteral("") : entry.imageRef;
    const QByteArray contentHash = duplicateHash(entry.kind, content, imageRef);
    if (!m_database.transaction())
        return failure(RepositoryError::SqlError);

    RepositoryError error = RepositoryError::None;
    qint64 recordedAtMsUtc = entry.createdAtMsUtc;
    {
        QSqlQuery latest(m_database);
        if (!latest.exec(QStringLiteral("SELECT MAX(created_at_ms_utc) FROM history_entries"))
            || !latest.next() || latest.lastError().isValid()) {
            error = RepositoryError::SqlError;
        } else if (!latest.value(0).isNull()) {
            const qint64 latestAtMsUtc = latest.value(0).toLongLong();
            // Keep recent-first pagination stable when event timestamps collide within one millisecond.
            if (recordedAtMsUtc <= latestAtMsUtc) {
                if (latestAtMsUtc == std::numeric_limits<qint64>::max())
                    error = RepositoryError::SqlError;
                else
                    recordedAtMsUtc = latestAtMsUtc + 1;
            }
        }
    }
    {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "SELECT id FROM history_entries "
            "WHERE content_hash = :content_hash AND kind = :kind "
            "AND content = :content AND image_ref = :image_ref "
            "ORDER BY created_at_ms_utc DESC, id DESC LIMIT 1"));
        query.bindValue(QStringLiteral(":content_hash"), QString::fromLatin1(contentHash));
        query.bindValue(QStringLiteral(":kind"), entry.kind);
        query.bindValue(QStringLiteral(":content"), content);
        query.bindValue(QStringLiteral(":image_ref"), imageRef);
        if (!query.exec()) {
            error = RepositoryError::SqlError;
        } else if (query.next()) {
            id = query.value(0).toLongLong();
            query.finish();

            QSqlQuery update(m_database);
            update.prepare(QStringLiteral(
                "UPDATE history_entries SET created_at_ms_utc = :created_at_ms_utc, "
                "usage_count = CASE WHEN usage_count < 9223372036854775807 "
                "THEN usage_count + 1 ELSE usage_count END WHERE id = :id"));
            update.bindValue(QStringLiteral(":created_at_ms_utc"), recordedAtMsUtc);
            update.bindValue(QStringLiteral(":id"), id);
            if (!update.exec() || update.numRowsAffected() != 1)
                error = RepositoryError::SqlError;
            else
                incremented = true;
        } else if (query.lastError().isValid()) {
            error = RepositoryError::SqlError;
        } else {
            query.finish();
            QSqlQuery insertQuery(m_database);
            insertQuery.prepare(QStringLiteral(
                "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, usage_count, content_hash) "
                "VALUES(:kind, :content, :image_ref, :created_at_ms_utc, 1, :content_hash)"));
            insertQuery.bindValue(QStringLiteral(":kind"), entry.kind);
            insertQuery.bindValue(QStringLiteral(":content"), content);
            insertQuery.bindValue(QStringLiteral(":image_ref"), imageRef);
            insertQuery.bindValue(QStringLiteral(":created_at_ms_utc"), recordedAtMsUtc);
            insertQuery.bindValue(QStringLiteral(":content_hash"), QString::fromLatin1(contentHash));
            if (!insertQuery.exec())
                error = RepositoryError::SqlError;
            else
                id = insertQuery.lastInsertId().toLongLong();
        }
    }

    if (error != RepositoryError::None || !m_database.commit()) {
        m_database.rollback();
        id = 0;
        incremented = false;
        return failure(RepositoryError::SqlError);
    }
    return {};
}

LegacyImportRecordResult HistoryRepository::importLegacyEntry(const QString &sourcePath,
                                                               const QString &sourceSha256,
                                                               qint64 sourceRowId,
                                                               const NewHistoryEntry &entry)
{
    const auto failed = [](RepositoryError error) {
        return LegacyImportRecordResult{{error}, 0, false};
    };
    if (const auto status = checkThread(); !status)
        return {status, 0, false};
    if (!m_database.isOpen())
        return failed(RepositoryError::NotInitialized);
    if (sourcePath.isEmpty() || sourceRowId <= 0 || sourceSha256.size() != 64
        || entry.kind.isEmpty())
        return failed(RepositoryError::InvalidArgument);
    if ((entry.kind == QStringLiteral("image")
         && (entry.imageRef.isEmpty() || !entry.content.isEmpty()))
        || (entry.kind != QStringLiteral("image") && !entry.imageRef.isEmpty()))
        return failed(RepositoryError::InvalidArgument);
    for (const QChar character : sourceSha256) {
        if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
              || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))))
            return failed(RepositoryError::InvalidArgument);
    }

    if (!m_database.transaction())
        return failed(RepositoryError::SqlError);
    const auto rollback = [this, &failed](RepositoryError error) {
        m_database.rollback();
        return failed(error);
    };

    {
        QSqlQuery source(m_database);
        source.prepare(QStringLiteral("SELECT source_sha256 FROM legacy_import_sources WHERE source_path = :path"));
        source.bindValue(QStringLiteral(":path"), sourcePath);
        if (!source.exec())
            return rollback(RepositoryError::SqlError);
        const bool sourceExists = source.next();
        const bool sourceQueryFailed = source.lastError().isValid();
        const QString registeredSha256 = sourceExists ? source.value(0).toString() : QString();
        source.finish();
        if (sourceQueryFailed)
            return rollback(RepositoryError::SqlError);
        if (sourceExists && registeredSha256 != sourceSha256)
            return rollback(RepositoryError::LegacySourceChanged);
        if (!sourceExists) {
            QSqlQuery registerSource(m_database);
            registerSource.prepare(QStringLiteral(
                "INSERT INTO legacy_import_sources(source_path, source_sha256) VALUES(:path, :sha256)"));
            registerSource.bindValue(QStringLiteral(":path"), sourcePath);
            registerSource.bindValue(QStringLiteral(":sha256"), sourceSha256);
            if (!registerSource.exec())
                return rollback(RepositoryError::SqlError);
        }
    }

    qint64 existingId = 0;
    {
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT history_id FROM legacy_import_rows "
            "WHERE source_sha256 = :sha256 AND source_row_id = :row_id"));
        existing.bindValue(QStringLiteral(":sha256"), sourceSha256);
        existing.bindValue(QStringLiteral(":row_id"), sourceRowId);
        if (!existing.exec())
            return rollback(RepositoryError::SqlError);
        if (existing.next())
            existingId = existing.value(0).toLongLong();
    }
    if (existingId > 0) {
        if (!m_database.commit())
            return rollback(RepositoryError::SqlError);
        return {{}, existingId, false};
    }

    const QString content = entry.content.isNull() ? QStringLiteral("") : entry.content;
    const QString imageRef = entry.imageRef.isNull() ? QStringLiteral("") : entry.imageRef;
    qint64 historyId = 0;
    {
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, usage_count, content_hash) "
            "VALUES(:kind, :content, :image_ref, :created_at_ms_utc, 1, :content_hash)"));
        insert.bindValue(QStringLiteral(":kind"), entry.kind);
        insert.bindValue(QStringLiteral(":content"), content);
        insert.bindValue(QStringLiteral(":image_ref"), imageRef);
        insert.bindValue(QStringLiteral(":created_at_ms_utc"), entry.createdAtMsUtc);
        insert.bindValue(QStringLiteral(":content_hash"), QString::fromLatin1(duplicateHash(entry.kind, content, imageRef)));
        if (!insert.exec())
            return rollback(RepositoryError::SqlError);
        historyId = insert.lastInsertId().toLongLong();
    }
    {
        QSqlQuery marker(m_database);
        marker.prepare(QStringLiteral(
            "INSERT INTO legacy_import_rows(source_sha256, source_row_id, history_id) "
            "VALUES(:sha256, :row_id, :history_id)"));
        marker.bindValue(QStringLiteral(":sha256"), sourceSha256);
        marker.bindValue(QStringLiteral(":row_id"), sourceRowId);
        marker.bindValue(QStringLiteral(":history_id"), historyId);
        if (!marker.exec())
            return rollback(RepositoryError::SqlError);
    }
    if (!m_database.commit())
        return rollback(RepositoryError::SqlError);
    return {{}, historyId, true};
}

RepositoryStatus HistoryRepository::hasImageReference(const QString &imageRef, bool &inUse)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (imageRef.isEmpty())
        return failure(RepositoryError::InvalidArgument);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM history_entries WHERE image_ref = :image_ref LIMIT 1"));
    query.bindValue(QStringLiteral(":image_ref"), imageRef);
    if (!query.exec())
        return failure(RepositoryError::SqlError);
    inUse = query.next();
    return query.lastError().isValid() ? failure(RepositoryError::SqlError) : RepositoryStatus{};
}

RepositoryStatus HistoryRepository::get(qint64 id, HistoryEntry& entry)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (id <= 0)
        return failure(RepositoryError::InvalidArgument);

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, kind, content, image_ref, created_at_ms_utc, usage_count, favorite "
        "FROM history_entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return failure(RepositoryError::SqlError);
    if (!query.next())
        return failure(query.lastError().isValid() ? RepositoryError::SqlError : RepositoryError::NotFound);

    entry.id = query.value(0).toLongLong();
    entry.kind = query.value(1).toString();
    entry.content = query.value(2).toString();
    entry.imageRef = query.value(3).toString();
    entry.createdAtMsUtc = query.value(4).toLongLong();
    entry.usageCount = query.value(5).toLongLong();
    entry.favorite = query.value(6).toBool();
    return {};
}

RepositoryStatus HistoryRepository::update(qint64 id, const NewHistoryEntry& entry)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (id <= 0 || entry.kind.isEmpty())
        return failure(RepositoryError::InvalidArgument);

    const QString content = entry.content.isNull() ? QStringLiteral("") : entry.content;
    const QString imageRef = entry.imageRef.isNull() ? QStringLiteral("") : entry.imageRef;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE history_entries SET kind = :kind, content = :content, "
        "image_ref = :image_ref, created_at_ms_utc = :created_at_ms_utc, content_hash = :content_hash WHERE id = :id"));
    query.bindValue(QStringLiteral(":kind"), entry.kind);
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":image_ref"), imageRef);
    query.bindValue(QStringLiteral(":created_at_ms_utc"), entry.createdAtMsUtc);
    query.bindValue(QStringLiteral(":content_hash"), QString::fromLatin1(duplicateHash(entry.kind, content, imageRef)));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return failure(RepositoryError::SqlError);
    return query.numRowsAffected() == 1 ? RepositoryStatus{} : failure(RepositoryError::NotFound);
}

HistoryPage HistoryRepository::page(qint64 offset, qint64 limit)
{
    HistoryPage result;
    if (const auto status = checkThread(); !status) {
        result.status = status;
        return result;
    }
    if (!m_database.isOpen()) {
        result.status = failure(RepositoryError::NotInitialized);
        return result;
    }
    if (offset < 0 || limit <= 0) {
        result.status = failure(RepositoryError::InvalidArgument);
        return result;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, kind, content, image_ref, created_at_ms_utc, usage_count, favorite "
        "FROM history_entries "
        "ORDER BY created_at_ms_utc DESC, id DESC LIMIT :limit OFFSET :offset"));
    query.bindValue(QStringLiteral(":limit"), limit);
    query.bindValue(QStringLiteral(":offset"), offset);
    if (!query.exec()) {
        result.status = failure(RepositoryError::SqlError);
        return result;
    }

    while (query.next()) {
        HistoryEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.kind = query.value(1).toString();
        entry.content = query.value(2).toString();
        entry.imageRef = query.value(3).toString();
        entry.createdAtMsUtc = query.value(4).toLongLong();
        entry.usageCount = query.value(5).toLongLong();
        entry.favorite = query.value(6).toBool();
        result.entries.append(std::move(entry));
    }
    if (query.lastError().isValid()) {
        result.entries.clear();
        result.status = failure(RepositoryError::SqlError);
    }
    return result;
}

HistoryPreviewPage HistoryRepository::previewPage(qint64 offset, qint64 limit,
                                                   const QString &search, const QString &kind,
                                                   bool includeTotalCount)
{
    HistoryPreviewPage result;
    if (const auto status = checkThread(); !status) {
        result.status = status;
        return result;
    }
    if (!m_database.isOpen()) {
        result.status = failure(RepositoryError::NotInitialized);
        return result;
    }
    if (offset < 0 || limit <= 0 || limit > 200) {
        result.status = failure(RepositoryError::InvalidArgument);
        return result;
    }

    if (includeTotalCount) {
        QSqlQuery countQuery(m_database);
        if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM history_entries"))
            || !countQuery.next()) {
            result.status = failure(RepositoryError::SqlError);
            return result;
        }
        result.totalCount = countQuery.value(0).toLongLong();
    }

    constexpr int previewLength = 512;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, kind, substr(content, 1, :previewLength + 1), image_ref, "
        "created_at_ms_utc, usage_count, length(content) > :previewLength, favorite, "
        "CASE WHEN kind = 'path' THEN content ELSE '' END "
        "FROM history_entries WHERE (:filterKind = 0 OR (:kind = 'favorite' AND favorite = 1) OR kind = :kind) "
        "AND (:filterSearch = 0 OR instr(lower(content), lower(:search)) > 0) "
        "ORDER BY created_at_ms_utc DESC, id DESC LIMIT :limit OFFSET :offset"));
    query.bindValue(QStringLiteral(":previewLength"), previewLength);
    query.bindValue(QStringLiteral(":filterSearch"), search.isEmpty() ? 0 : 1);
    query.bindValue(QStringLiteral(":filterKind"), kind.isEmpty() ? 0 : 1);
    query.bindValue(QStringLiteral(":search"), search);
    query.bindValue(QStringLiteral(":kind"), kind);
    query.bindValue(QStringLiteral(":limit"), limit);
    query.bindValue(QStringLiteral(":offset"), offset);
    if (!query.exec()) {
        result.status = failure(RepositoryError::SqlError);
        return result;
    }

    while (query.next()) {
        HistoryPreview entry;
        entry.id = query.value(0).toLongLong();
        entry.kind = query.value(1).toString();
        entry.content = query.value(2).toString();
        entry.imageRef = query.value(3).toString();
        entry.createdAtMsUtc = query.value(4).toLongLong();
        entry.usageCount = query.value(5).toLongLong();
        entry.truncated = query.value(6).toBool();
        entry.favorite = query.value(7).toBool();
        if (entry.kind == QStringLiteral("path"))
            entry.sourcePaths = query.value(8).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (entry.truncated)
            entry.content.truncate(previewLength);
        result.entries.append(std::move(entry));
    }
    if (query.lastError().isValid()) {
        result.entries.clear();
        result.status = failure(RepositoryError::SqlError);
    }
    return result;
}

RepositoryStatus HistoryRepository::setFavorite(qint64 id, bool favorite)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (id <= 0)
        return failure(RepositoryError::InvalidArgument);

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE history_entries SET favorite = :favorite WHERE id = :id"));
    query.bindValue(QStringLiteral(":favorite"), favorite ? 1 : 0);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return failure(RepositoryError::SqlError);
    return query.numRowsAffected() == 1 ? RepositoryStatus{} : failure(RepositoryError::NotFound);
}

RepositoryStatus HistoryRepository::remove(qint64 id, QString *orphanImageRef)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (id <= 0)
        return failure(RepositoryError::InvalidArgument);

    if (!m_database.transaction())
        return failure(RepositoryError::SqlError);
    QString imageRef;
    {
        QSqlQuery select(m_database);
        select.prepare(QStringLiteral("SELECT image_ref FROM history_entries WHERE id = :id"));
        select.bindValue(QStringLiteral(":id"), id);
        if (!select.exec() || !select.next()) {
            m_database.rollback();
            return failure(select.lastError().isValid() ? RepositoryError::SqlError : RepositoryError::NotFound);
        }
        imageRef = select.value(0).toString();
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM history_entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        m_database.rollback();
        return failure(RepositoryError::SqlError);
    }
    bool isOrphan = false;
    if (!imageRef.isEmpty()) {
        QSqlQuery references(m_database);
        references.prepare(QStringLiteral("SELECT 1 FROM history_entries WHERE image_ref = :image_ref LIMIT 1"));
        references.bindValue(QStringLiteral(":image_ref"), imageRef);
        if (!references.exec()) {
            m_database.rollback();
            return failure(RepositoryError::SqlError);
        }
        isOrphan = !references.next();
        references.finish();
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return failure(RepositoryError::SqlError);
    }
    if (orphanImageRef)
        *orphanImageRef = isOrphan ? imageRef : QString();
    return {};
}

RepositoryStatus HistoryRepository::clear(QStringList *orphanImageRefs)
{
    if (const auto status = checkThread(); !status)
        return status;
    if (!m_database.isOpen())
        return failure(RepositoryError::NotInitialized);
    if (!m_database.transaction())
        return failure(RepositoryError::SqlError);

    QStringList imageRefs;
    {
        QSqlQuery select(m_database);
        if (!select.exec(QStringLiteral("SELECT DISTINCT image_ref FROM history_entries WHERE image_ref <> ''"))) {
            m_database.rollback();
            return failure(RepositoryError::SqlError);
        }
        while (select.next())
            imageRefs.append(select.value(0).toString());
        if (select.lastError().isValid()) {
            m_database.rollback();
            return failure(RepositoryError::SqlError);
        }
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("DELETE FROM history_entries"))) {
        m_database.rollback();
        return failure(RepositoryError::SqlError);
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return failure(RepositoryError::SqlError);
    }
    if (orphanImageRefs)
        *orphanImageRefs = std::move(imageRefs);
    return {};
}

RepositoryStatus HistoryRepository::removeExpired(qint64 cutoffMsUtc, int limit,
                                                   qint64 &removedCount, QStringList &orphanImageRefs)
{
    return enforceRetention(cutoffMsUtc, 0, limit, removedCount, orphanImageRefs);
}

RepositoryStatus HistoryRepository::enforceRetention(qint64 cutoffMsUtc, qint64 maximumEntries,
                                                      int limit, qint64 &removedCount,
                                                      QStringList &orphanImageRefs)
{
    removedCount = 0;
    orphanImageRefs.clear();
    if (const auto status = checkThread(); !status) return status;
    if (!m_database.isOpen()) return failure(RepositoryError::NotInitialized);
    if (cutoffMsUtc < 0 || maximumEntries < 0 || (cutoffMsUtc == 0 && maximumEntries == 0)
        || limit <= 0 || limit > 500) return failure(RepositoryError::InvalidArgument);
    if (!m_database.transaction()) return failure(RepositoryError::SqlError);
    const auto rollback = [this] {
        m_database.rollback();
        return failure(RepositoryError::SqlError);
    };
    qint64 toDelete = 0;
    if (cutoffMsUtc > 0) {
        QSqlQuery count(m_database);
        count.prepare(QStringLiteral("SELECT COUNT(*) FROM (SELECT id FROM history_entries "
                                     "WHERE favorite = 0 AND created_at_ms_utc < :cutoff LIMIT :limit)"));
        count.bindValue(QStringLiteral(":cutoff"), cutoffMsUtc);
        count.bindValue(QStringLiteral(":limit"), limit);
        if (!count.exec() || !count.next()) return rollback();
        toDelete = count.value(0).toLongLong();
    }
    if (maximumEntries > 0) {
        QSqlQuery count(m_database);
        if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM history_entries")) || !count.next()) return rollback();
        toDelete = std::max(toDelete, count.value(0).toLongLong() - maximumEntries);
    }
    toDelete = std::min(qint64(limit), toDelete);
    QVector<qint64> ids;
    QStringList imageRefs;
    {
        QSqlQuery select(m_database);
        select.prepare(QStringLiteral(
            "SELECT id, image_ref FROM history_entries WHERE favorite = 0 "
            "ORDER BY created_at_ms_utc, id LIMIT :limit"));
        select.bindValue(QStringLiteral(":limit"), toDelete);
        if (!select.exec()) return rollback();
        while (select.next()) {
            ids.append(select.value(0).toLongLong());
            const auto ref = select.value(1).toString();
            if (!ref.isEmpty() && !imageRefs.contains(ref)) imageRefs.append(ref);
        }
        if (select.lastError().isValid()) return rollback();
    }
    if (!ids.isEmpty()) {
        QStringList placeholders;
        for (qsizetype i = 0; i < ids.size(); ++i) placeholders.append(QStringLiteral("?"));
        QSqlQuery remove(m_database);
        remove.prepare(QStringLiteral("DELETE FROM history_entries WHERE id IN (%1)").arg(placeholders.join(',')));
        for (qint64 id : ids) remove.addBindValue(id);
        if (!remove.exec() || remove.numRowsAffected() != ids.size()) return rollback();
    }
    QStringList orphans;
    for (const auto &ref : imageRefs) {
        bool inUse = true;
        if (!hasImageReference(ref, inUse)) return rollback();
        if (!inUse) orphans.append(ref);
    }
    if (!m_database.commit()) return rollback();
    removedCount = ids.size();
    orphanImageRefs = std::move(orphans);
    return {};
}

} // namespace ClipboardHistory::Core
