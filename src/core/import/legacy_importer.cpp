#include "legacy_importer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

#include <utility>

namespace ClipboardHistory::Core {
namespace {

constexpr int MaxReportedIssues = 100;
constexpr qint64 MaxImagePixels = 16LL * 1024 * 1024;

bool hashFile(const QString &path, QByteArray &digest)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return false;
    digest = hash.result().toHex();
    return true;
}

bool insideDirectory(const QString &directory, const QString &filePath)
{
    const QString relative = QDir(directory).relativeFilePath(filePath);
    return relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"));
}

QDateTime timestampUtc(const QString &value)
{
    QDateTime timestamp = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (timestamp.isValid()) {
        timestamp.setTimeZone(QTimeZone::UTC);
        return timestamp;
    }
    timestamp = QDateTime::fromString(value, Qt::ISODate);
    return timestamp.isValid() ? timestamp.toUTC() : QDateTime();
}

bool validTimestamp(const QString &value)
{
    return !timestampUtc(value).isNull();
}

void addIssue(LegacyImportReport &report, qint64 rowId, QString code)
{
    if (report.issues.size() < MaxReportedIssues)
        report.issues.append({rowId, std::move(code)});
    else
        report.issuesTruncated = true;
}

} // namespace

LegacyImportReport LegacyImporter::inspect(const QString &databaseFilePath,
                                           const QString &imagesDirectory,
                                           const EntryHandler &onEntry)
{
    LegacyImportReport report;
    const QFileInfo databaseInfo(databaseFilePath);
    const QString databasePath = databaseInfo.canonicalFilePath();
    const QFileInfo imageDirectoryInfo(imagesDirectory);
    const QString imageRoot = imageDirectoryInfo.canonicalFilePath();
    if (databasePath.isEmpty() || !databaseInfo.isFile()
        || imageRoot.isEmpty() || !imageDirectoryInfo.isDir()) {
        report.error = LegacyImportError::InvalidSource;
        return report;
    }

    QByteArray initialHash;
    if (!hashFile(databasePath, initialHash)) {
        report.error = LegacyImportError::HashFailed;
        return report;
    }
    report.sourceSha256 = QString::fromLatin1(initialHash);

    const QString connectionName = QStringLiteral("LegacyImport_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            report.error = LegacyImportError::DatabaseOpenFailed;
        } else if (!QSqlQuery(database).exec(QStringLiteral("PRAGMA query_only = ON"))) {
            report.error = LegacyImportError::DatabaseOpenFailed;
        } else {
            QSqlQuery integrity(database);
            if (!integrity.exec(QStringLiteral("PRAGMA integrity_check"))
                || !integrity.next() || integrity.value(0).toString() != QStringLiteral("ok")) {
                report.error = LegacyImportError::CorruptDatabase;
            }

            bool hasRequiredColumns = false;
            if (report.error == LegacyImportError::None) {
                QSqlQuery columns(database);
                if (columns.exec(QStringLiteral("PRAGMA table_info(clipboard_history)"))) {
                    bool hasId = false;
                    bool hasType = false;
                    bool hasContent = false;
                    bool hasTimestamp = false;
                    while (columns.next()) {
                        const QString name = columns.value(1).toString();
                        hasId |= name == QStringLiteral("id");
                        hasType |= name == QStringLiteral("type");
                        hasContent |= name == QStringLiteral("content");
                        hasTimestamp |= name == QStringLiteral("timestamp");
                    }
                    hasRequiredColumns = hasId && hasType && hasContent && hasTimestamp;
                }
                if (!hasRequiredColumns)
                    report.error = LegacyImportError::UnsupportedSchema;
            }

            if (report.error == LegacyImportError::None) {
                QSqlQuery rows(database);
                if (!rows.exec(QStringLiteral(
                        "SELECT id, type, CASE WHEN type IN (2, 3) OR length(content) <= 8388608 "
                        "THEN content ELSE '' END, "
                        "timestamp, length(content) FROM clipboard_history ORDER BY id"))) {
                    report.error = LegacyImportError::UnsupportedSchema;
                } else {
                    while (rows.next()) {
                        const qint64 id = rows.value(0).toLongLong();
                        bool typeOk = false;
                        const int type = rows.value(1).toInt(&typeOk);
                        const QString content = rows.value(2).toString();
                        const QString timestamp = rows.value(3).toString();
                        const qint64 contentLength = rows.value(4).toLongLong();
                        ++report.scannedRows;

                        if (!typeOk || type < 1 || type > 3) {
                            ++report.unsupportedRows;
                            addIssue(report, id, QStringLiteral("unsupported_type"));
                            continue;
                        }
                        if (contentLength <= 0) {
                            ++report.unsupportedRows;
                            addIssue(report, id, QStringLiteral("empty_content"));
                            continue;
                        }
                        if (type == 1 && contentLength > 8388608) {
                            ++report.unsupportedRows;
                            addIssue(report, id, QStringLiteral("text_too_large"));
                            continue;
                        }
                        if (!validTimestamp(timestamp)) {
                            ++report.unsupportedRows;
                            addIssue(report, id, QStringLiteral("invalid_timestamp"));
                            continue;
                        }

                        QString kind;
                        QImage image;
                        if (type == 1) {
                            kind = QStringLiteral("text");
                        } else if (type == 2) {
                            const QFileInfo legacyImageInfo(content);
                            const QString resolvedImagePath = legacyImageInfo.canonicalFilePath();
                            if (resolvedImagePath.isEmpty() || !legacyImageInfo.isFile()
                                || !insideDirectory(imageRoot, resolvedImagePath)) {
                                ++report.unsupportedRows;
                                addIssue(report, id, QStringLiteral("image_outside_or_missing"));
                                continue;
                            }
                            QImageReader reader(resolvedImagePath);
                            const QSize size = reader.size();
                            if (!size.isValid()
                                || qint64(size.width()) * qint64(size.height()) > MaxImagePixels) {
                                ++report.unsupportedRows;
                                addIssue(report, id, QStringLiteral("invalid_image"));
                                continue;
                            }
                            image = reader.read();
                            if (image.isNull()) {
                                ++report.unsupportedRows;
                                addIssue(report, id, QStringLiteral("invalid_image"));
                                continue;
                            }
                            kind = QStringLiteral("image");
                        } else {
                            kind = QStringLiteral("path");
                        }

                        if (onEntry) {
                            LegacyImportEntry entry;
                            entry.sourceId = id;
                            entry.kind = kind;
                            entry.content = type == 2 ? QString() : content;
                            entry.image = std::move(image);
                            entry.createdAtMsUtc = timestampUtc(timestamp).toMSecsSinceEpoch();
                            if (!onEntry(report.sourceSha256, entry)) {
                                report.error = LegacyImportError::HandlerRejected;
                                addIssue(report, id, QStringLiteral("handler_rejected"));
                                break;
                            }
                        }
                        report.supportedRows[kind] = report.supportedRows.value(kind) + 1;
                    }
                    if (rows.lastError().isValid())
                        report.error = LegacyImportError::CorruptDatabase;
                }
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (report.error != LegacyImportError::None)
        return report;

    QByteArray finalHash;
    if (!hashFile(databasePath, finalHash)) {
        report.error = LegacyImportError::HashFailed;
    } else if (initialHash != finalHash) {
        report.error = LegacyImportError::SourceChanged;
    }
    return report;
}

} // namespace ClipboardHistory::Core
