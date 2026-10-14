#include "../../src/core/import/legacy_importer.h"
#include "../../src/core/history/history_service.h"
#include "../../src/core/history/history_task_queue.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace ClipboardHistory::Core;

class LegacyImporterTest final : public QObject {
    Q_OBJECT

private slots:
    void scansLegacyRowsWithoutChangingSource();
    void rejectsImageOutsideLegacyImagesDirectory();
    void rejectsUnknownSchema();
    void importsIdempotentlyAndPreservesTimestamp();
    void retriesPartialImportAndImportsPreviouslyMissingImage();
    void rejectsChangedSourceAfterPartialImport();
    void cleansImageWhenImportTransactionFails();
    void upgradesV4TargetSchema();
    void importsThroughWorkerQueue();

private:
    static bool createLegacyDatabase(const QString &path,
                                     const QStringList &rows,
                                     const QString &schema = QStringLiteral(
                                         "CREATE TABLE clipboard_history ("
                                         "id INTEGER PRIMARY KEY, type INTEGER, content TEXT UNIQUE, "
                                         "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)"));
    static QByteArray hash(const QString &path);
};

bool LegacyImporterTest::createLegacyDatabase(const QString &path, const QStringList &rows,
                                              const QString &schema)
{
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (!database.open())
            return false;
        QSqlQuery query(database);
        if (!query.exec(schema))
            return false;
        for (const QString &row : rows) {
            if (!query.exec(row))
                return false;
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

QByteArray LegacyImporterTest::hash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    return digest.addData(&file) ? digest.result() : QByteArray();
}

void LegacyImporterTest::scansLegacyRowsWithoutChangingSource()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(images));
    const QString imagePath = QDir(images).filePath(QStringLiteral("image_abc.png"));
    QVERIFY(QImage(12, 8, QImage::Format_ARGB32).save(imagePath));

    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'hello', '2025-01-02 03:04:05')"),
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(2, '%1', '2025-01-02 03:04:05')").arg(imagePath),
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(3, 'C:/notes.txt', '2025-01-02 03:04:05')")
    }));
    const QByteArray sourceHash = hash(databasePath);

    QVector<LegacyImportEntry> entries;
    const LegacyImportReport first = LegacyImporter::inspect(
        databasePath, images, [&entries](const QString &, const LegacyImportEntry &entry) {
            entries.append(entry);
            return true;
        });
    const LegacyImportReport second = LegacyImporter::inspect(databasePath, images);
    QVERIFY(first);
    QVERIFY(second);
    QCOMPARE(first.scannedRows, qint64(3));
    QCOMPARE(first.supportedRows.value(QStringLiteral("text")), qint64(1));
    QCOMPARE(first.supportedRows.value(QStringLiteral("image")), qint64(1));
    QCOMPARE(first.supportedRows.value(QStringLiteral("path")), qint64(1));
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).sourceId, qint64(1));
    QCOMPARE(entries.at(0).content, QStringLiteral("hello"));
    QCOMPARE(entries.at(0).createdAtMsUtc,
             QDateTime::fromString(QStringLiteral("2025-01-02T03:04:05Z"), Qt::ISODate).toMSecsSinceEpoch());
    QVERIFY(!entries.at(1).image.isNull());
    QCOMPARE(entries.at(2).kind, QStringLiteral("path"));
    QCOMPARE(entries.at(2).content, QStringLiteral("C:/notes.txt"));
    QCOMPARE(first.sourceSha256, second.sourceSha256);
    QCOMPARE(hash(databasePath), sourceHash);
}

void LegacyImporterTest::rejectsImageOutsideLegacyImagesDirectory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(images));
    const QString outsidePath = root.filePath(QStringLiteral("outside.png"));
    QVERIFY(QImage(4, 4, QImage::Format_ARGB32).save(outsidePath));
    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(2, '%1', '2025-01-02 03:04:05')").arg(outsidePath)
    }));

    const LegacyImportReport report = LegacyImporter::inspect(databasePath, images);
    QVERIFY(report);
    QCOMPARE(report.unsupportedRows, qint64(1));
    QCOMPARE(report.issues.size(), 1);
    QCOMPARE(report.issues.constFirst().code, QStringLiteral("image_outside_or_missing"));
}

void LegacyImporterTest::rejectsUnknownSchema()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(images));
    const QString databasePath = root.filePath(QStringLiteral("bad.db"));
    QVERIFY(createLegacyDatabase(databasePath, {}, QStringLiteral("CREATE TABLE other(value TEXT)")));

    const LegacyImportReport report = LegacyImporter::inspect(databasePath, images);
    QCOMPARE(report.error, LegacyImportError::UnsupportedSchema);
}

void LegacyImporterTest::importsIdempotentlyAndPreservesTimestamp()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(images));
    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'unique legacy text', '2025-01-02 03:04:05')")
    }));

    HistoryService service(root.filePath(QStringLiteral("history.sqlite3")),
                           root.filePath(QStringLiteral("new-images")));
    QVERIFY(service.initialize());
    const auto first = service.importLegacy(databasePath, images);
    QVERIFY(first.status);
    QCOMPARE(first.importedRows, qint64(1));
    QCOMPARE(first.alreadyImportedRows, qint64(0));

    const auto second = service.importLegacy(databasePath, images);
    QVERIFY(second.status);
    QCOMPARE(second.importedRows, qint64(0));
    QCOMPARE(second.alreadyImportedRows, qint64(1));
    const auto page = service.page(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), 1);
    QCOMPARE(page.entries.first().content, QStringLiteral("unique legacy text"));
    QCOMPARE(page.entries.first().usageCount, qint64(1));
    QCOMPARE(page.entries.first().createdAtMsUtc,
             QDateTime::fromString(QStringLiteral("2025-01-02T03:04:05Z"), Qt::ISODate).toMSecsSinceEpoch());
}

void LegacyImporterTest::retriesPartialImportAndImportsPreviouslyMissingImage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(images));
    const QString imagePath = QDir(images).filePath(QStringLiteral("image_missing.png"));
    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'partial retry text', '2025-01-02 03:04:05')"),
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(2, '%1', '2025-01-02 03:04:06')").arg(imagePath)
    }));

    const QString newImages = root.filePath(QStringLiteral("new-images"));
    HistoryService service(root.filePath(QStringLiteral("history.sqlite3")), newImages);
    QVERIFY(service.initialize());
    const auto partial = service.importLegacy(databasePath, images);
    QVERIFY(partial.status);
    QCOMPARE(partial.importedRows, qint64(1));
    QCOMPARE(partial.scan.unsupportedRows, qint64(1));
    QVERIFY(QImage(8, 8, QImage::Format_ARGB32).save(imagePath));

    const auto resumed = service.importLegacy(databasePath, images);
    QVERIFY(resumed.status);
    QCOMPARE(resumed.importedRows, qint64(1));
    QCOMPARE(resumed.alreadyImportedRows, qint64(1));
    const auto page = service.page(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), 2);
    QCOMPARE(page.entries.first().kind, QStringLiteral("image"));
    QVERIFY(QFile::exists(newImages + QLatin1Char('/') + page.entries.first().imageRef + QStringLiteral(".png")));
}

void LegacyImporterTest::rejectsChangedSourceAfterPartialImport()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(images));
    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'before change', '2025-01-02 03:04:05')")
    }));

    HistoryService service(root.filePath(QStringLiteral("history.sqlite3")),
                           root.filePath(QStringLiteral("new-images")));
    QVERIFY(service.initialize());
    const auto first = service.importLegacy(databasePath, images);
    QVERIFY(first.status);
    {
        const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(databasePath);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'after change', '2025-01-02 03:04:06')")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    const auto changed = service.importLegacy(databasePath, images);
    QCOMPARE(changed.status.error, HistoryServiceError::RepositoryFailure);
    QCOMPARE(changed.status.repositoryError, RepositoryError::LegacySourceChanged);
    QCOMPARE(service.page(0, 10).entries.size(), 1);
}

void LegacyImporterTest::cleansImageWhenImportTransactionFails()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString legacyImages = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(legacyImages));
    const QString imagePath = QDir(legacyImages).filePath(QStringLiteral("image_transaction.png"));
    QVERIFY(QImage(8, 8, QImage::Format_ARGB32).save(imagePath));
    const QString sourcePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(sourcePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(2, '%1', '2025-01-02 03:04:05')").arg(imagePath)
    }));

    const QString targetPath = root.filePath(QStringLiteral("history.sqlite3"));
    const QString newImages = root.filePath(QStringLiteral("new-images"));
    HistoryService service(targetPath, newImages);
    QVERIFY(service.initialize());
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(targetPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TRIGGER fail_legacy_marker BEFORE INSERT ON legacy_import_rows "
            "BEGIN SELECT RAISE(ABORT, 'test failure'); END")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    const auto result = service.importLegacy(sourcePath, legacyImages);
    QCOMPARE(result.status.error, HistoryServiceError::RepositoryFailure);
    QCOMPARE(result.status.repositoryError, RepositoryError::SqlError);
    QVERIFY(!result.cleanupPending);
    QCOMPARE(service.page(0, 10).entries.size(), 0);
    const QDir storedImages(newImages);
    QCOMPARE(storedImages.entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 0);

    const QString retryConnection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), retryConnection);
        database.setDatabaseName(targetPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER fail_legacy_marker")));
        database.close();
    }
    QSqlDatabase::removeDatabase(retryConnection);
    const auto retry = service.importLegacy(sourcePath, legacyImages);
    QVERIFY(retry.status);
    QCOMPARE(retry.importedRows, qint64(1));
}

void LegacyImporterTest::upgradesV4TargetSchema()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString targetPath = root.filePath(QStringLiteral("history.sqlite3"));
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(targetPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE history_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
            "content TEXT NOT NULL, image_ref TEXT NOT NULL, created_at_ms_utc INTEGER NOT NULL, "
            "usage_count INTEGER NOT NULL DEFAULT 1, content_hash TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 4")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    const QString legacyImages = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(legacyImages));
    const QString sourcePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(sourcePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'v4 target import', '2025-01-02 03:04:05')")
    }));
    HistoryService service(targetPath, root.filePath(QStringLiteral("new-images")));
    QVERIFY(service.initialize());
    const auto imported = service.importLegacy(sourcePath, legacyImages);
    QVERIFY(imported.status);
    QCOMPARE(imported.importedRows, qint64(1));
    QCOMPARE(service.page(0, 10).entries.size(), 1);
}

void LegacyImporterTest::importsThroughWorkerQueue()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString images = root.filePath(QStringLiteral("legacy-images"));
    QVERIFY(QDir().mkpath(images));
    const QString databasePath = root.filePath(QStringLiteral("clipboard_history.db"));
    QVERIFY(createLegacyDatabase(databasePath, {
        QStringLiteral("INSERT INTO clipboard_history(type, content, timestamp) VALUES(1, 'queued import', '2025-01-02 03:04:05')")
    }));

    HistoryTaskQueue queue(root.filePath(QStringLiteral("history.sqlite3")),
                           root.filePath(QStringLiteral("new-images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QTRY_COMPARE_WITH_TIMEOUT(initialized.count(), 1, 5000);
    QVERIFY(static_cast<bool>(qvariant_cast<HistoryServiceStatus>(initialized.takeFirst().at(0))));
    QSignalSpy completed(&queue, &HistoryTaskQueue::legacyImportCompleted);
    QVERIFY(queue.importLegacy(databasePath, images));
    QVERIFY(!queue.importLegacy(databasePath, images));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    const auto result = qvariant_cast<HistoryLegacyImportResult>(completed.takeFirst().at(0));
    QVERIFY(result.status);
    QCOMPARE(result.importedRows, qint64(1));
    queue.shutdown();
}

QTEST_GUILESS_MAIN(LegacyImporterTest)
#include "tst_legacy_importer.moc"
