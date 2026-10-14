#include "../../src/core/history/history_repository.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using ClipboardHistory::Core::HistoryEntry;
using ClipboardHistory::Core::HistoryRepository;
using ClipboardHistory::Core::NewHistoryEntry;
using ClipboardHistory::Core::RepositoryError;
using ClipboardHistory::Core::RepositoryStatus;

class HistoryRepositoryTest final : public QObject {
    Q_OBJECT

private slots:
    void quantityLimitKeepsNewestAndFavoritesAndCombinesWithTime();
    void retentionProtectsFavoritesAndCutoffAndBatches();
    void retentionRollsBackOnDeleteFailure();
    void initializesAndPersistsEntries();
    void pagesInStableOrder();
    void previewPagesBoundContentSize();
    void previewSearchAndKindFilters();
    void favoritesAndMutationTransactions();
    void incrementsExactDuplicateUsage();
    void migratesVersionOneUsageCount();
    void migratesVersionTwoContentHash();
    void migratesVersionThreeFavorites();
    void rejectsInvalidArguments();
    void rejectsCallsFromAnotherThread();
    void migratesVersionZeroSchema();
    void rejectsFutureSchema();
    void rejectsCorruptDatabase();

private:
    QTemporaryDir m_tempDir;
};

void HistoryRepositoryTest::quantityLimitKeepsNewestAndFavoritesAndCombinesWithTime()
{
    QTemporaryDir temp;
    HistoryRepository repo(temp.filePath("history.sqlite3"));
    QVERIFY(repo.initialize());
    qint64 id = 0;
    for (int i = 1; i <= 8; ++i) {
        QVERIFY(repo.insert({"text", QString::number(i), {}, i * 10}, id));
        if (i <= 2) QVERIFY(repo.setFavorite(id, true));
    }
    qint64 removed = 0; QStringList refs;
    QVERIFY(repo.enforceRetention(0, 10, 500, removed, refs)); QCOMPARE(removed, 0);
    QVERIFY(repo.enforceRetention(0, 5, 500, removed, refs)); QCOMPARE(removed, 3);
    auto page = repo.page(0, 10);
    QCOMPARE(page.entries.size(), 5);
    QCOMPARE(page.entries[0].content, QStringLiteral("8"));
    QCOMPARE(page.entries[2].content, QStringLiteral("6"));
    QVERIFY(repo.enforceRetention(65, 4, 500, removed, refs)); QCOMPARE(removed, 1);
    QVERIFY(repo.enforceRetention(900, 3, 500, removed, refs)); QCOMPARE(removed, 2);
    QVERIFY(repo.enforceRetention(0, 1, 500, removed, refs)); QCOMPARE(removed, 0);
    QCOMPARE(repo.page(0, 10).entries.size(), 2);
    QVERIFY(repo.page(0, 10).entries[0].favorite);
    QVERIFY(repo.page(0, 10).entries[1].favorite);
    QCOMPARE(repo.enforceRetention(0, -1, 500, removed, refs).error, RepositoryError::InvalidArgument);
    QCOMPARE(repo.enforceRetention(0, 0, 500, removed, refs).error, RepositoryError::InvalidArgument);
    QCOMPARE(repo.page(0, 10).entries.size(), 2);
}

void HistoryRepositoryTest::retentionProtectsFavoritesAndCutoffAndBatches()
{
    QTemporaryDir temp;
    HistoryRepository repo(temp.filePath("history.sqlite3"));
    QVERIFY(repo.initialize());
    qint64 id = 0, favoriteId = 0;
    for (int i = 0; i < 501; ++i)
        QVERIFY(repo.insert({"text", QString::number(i), {}, 1000}, id));
    QVERIFY(repo.insert({"text", "favorite", {}, 1}, favoriteId));
    QVERIFY(repo.setFavorite(favoriteId, true));
    QVERIFY(repo.insert({"text", "boundary", {}, 2000}, id));
    QVERIFY(repo.insert({"text", "future", {}, 3000}, id));
    qint64 removed = 0;
    QStringList orphans;
    QCOMPARE(repo.removeExpired(2000, 501, removed, orphans).error, RepositoryError::InvalidArgument);
    QCOMPARE(repo.removeExpired(0, 500, removed, orphans).error, RepositoryError::InvalidArgument);
    QVERIFY(repo.removeExpired(2000, 500, removed, orphans));
    QCOMPARE(removed, 500);
    QVERIFY(orphans.isEmpty());
    QVERIFY(repo.removeExpired(2000, 500, removed, orphans));
    QCOMPARE(removed, 1);
    QVERIFY(repo.removeExpired(2000, 500, removed, orphans));
    QCOMPARE(removed, 0);
    QCOMPARE(repo.page(0, 10).entries.size(), 3);
    HistoryEntry entry;
    QVERIFY(repo.get(favoriteId, entry));
    QVERIFY(entry.favorite);
}

void HistoryRepositoryTest::retentionRollsBackOnDeleteFailure()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    HistoryRepository repo(path);
    QVERIFY(repo.initialize());
    qint64 id = 0;
    QVERIFY(repo.insert({"image", {}, QString(64, 'a'), 1}, id));
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "retention-failure");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_retention BEFORE DELETE ON history_entries BEGIN SELECT RAISE(ABORT, 'injected'); END"));
    }
    QSqlDatabase::removeDatabase("retention-failure");
    qint64 removed = 99;
    QStringList orphans{"unchanged"};
    QCOMPARE(repo.removeExpired(2, 500, removed, orphans).error, RepositoryError::SqlError);
    QCOMPARE(removed, 0);
    QVERIFY(orphans.isEmpty());
    QCOMPARE(repo.page(0, 10).entries.size(), 1);
}

void HistoryRepositoryTest::initializesAndPersistsEntries()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("crud.sqlite3")));
    QVERIFY(repository.initialize());

    const NewHistoryEntry value{QStringLiteral("text"), QStringLiteral("sample"), {}, 42};
    qint64 id = 0;
    QVERIFY(repository.insert(value, id));
    QVERIFY(id > 0);

    HistoryEntry loaded;
    QVERIFY(repository.get(id, loaded));
    QCOMPARE(loaded.id, id);
    QCOMPARE(loaded.kind, value.kind);
    QCOMPARE(loaded.content, value.content);
    QCOMPARE(loaded.createdAtMsUtc, value.createdAtMsUtc);
    QCOMPARE(loaded.usageCount, qint64(1));
    const NewHistoryEntry changed{QStringLiteral("text"), QStringLiteral("updated"), {}, 43};
    QVERIFY(repository.update(id, changed));
    QVERIFY(repository.get(id, loaded));
    QCOMPARE(loaded.content, changed.content);
    qint64 duplicateId = 0;
    bool incremented = false;
    QVERIFY(repository.recordOrIncrement(changed, duplicateId, incremented));
    QVERIFY(incremented);
    QCOMPARE(duplicateId, id);
    QVERIFY(repository.remove(id));
    QCOMPARE(repository.get(id, loaded).error, RepositoryError::NotFound);
}

void HistoryRepositoryTest::pagesInStableOrder()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("pages.sqlite3")));
    QVERIFY(repository.initialize());

    qint64 firstId = 0;
    qint64 secondId = 0;
    qint64 olderId = 0;
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("first"), {}, 100}, firstId));
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("second"), {}, 100}, secondId));
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("older"), {}, 99}, olderId));

    const auto firstPage = repository.page(0, 2);
    QVERIFY(firstPage.status);
    QCOMPARE(firstPage.entries.size(), 2);
    QCOMPARE(firstPage.entries.at(0).id, secondId);
    QCOMPARE(firstPage.entries.at(1).id, firstId);

    const auto nextPage = repository.page(2, 2);
    QVERIFY(nextPage.status);
    QCOMPARE(nextPage.entries.size(), 1);
    QCOMPARE(nextPage.entries.first().id, olderId);
}

void HistoryRepositoryTest::previewPagesBoundContentSize()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("preview.sqlite3")));
    QVERIFY(repository.initialize());
    qint64 entryId = 0;
    QVERIFY(repository.insert({QStringLiteral("text"), QString(10000, QLatin1Char('x')), {}, 1}, entryId));

    const auto page = repository.previewPage(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), 1);
    QCOMPARE(page.entries.first().id, entryId);
    QCOMPARE(page.entries.first().content.size(), 512);
    QVERIFY(page.entries.first().truncated);
    QCOMPARE(page.entries.first().content, QString(512, QLatin1Char('x')));
}

void HistoryRepositoryTest::previewSearchAndKindFilters()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("filters.sqlite3")));
    QVERIFY(repository.initialize());
    qint64 id = 0;
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("100% ready"), {}, 1}, id));
    QVERIFY(repository.insert({QStringLiteral("link"), QStringLiteral("https://example.test"), {}, 2}, id));
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("1000 ready"), {}, 3}, id));

    const auto literalSearch = repository.previewPage(0, 10, QStringLiteral("100%"));
    QVERIFY(literalSearch.status);
    QCOMPARE(literalSearch.entries.size(), 1);
    QCOMPARE(literalSearch.entries.first().content, QStringLiteral("100% ready"));

    const auto kindSearch = repository.previewPage(0, 10, {}, QStringLiteral("link"));
    QVERIFY(kindSearch.status);
    QCOMPARE(kindSearch.entries.size(), 1);
    QCOMPARE(kindSearch.entries.first().kind, QStringLiteral("link"));
}

void HistoryRepositoryTest::favoritesAndMutationTransactions()
{
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("operations.sqlite3")));
    QVERIFY(repository.initialize());
    qint64 firstId = 0;
    qint64 sharedImageId = 0;
    qint64 secondId = 0;
    QVERIFY(repository.insert({QStringLiteral("image"), {}, QStringLiteral("shared-ref"), 3}, firstId));
    QVERIFY(repository.insert({QStringLiteral("image"), {}, QStringLiteral("shared-ref"), 2}, sharedImageId));
    QVERIFY(repository.insert({QStringLiteral("text"), QStringLiteral("keep"), {}, 1}, secondId));

    QVERIFY(repository.setFavorite(firstId, true));
    const auto page = repository.previewPage(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.first().id, firstId);
    QVERIFY(page.entries.first().favorite);
    const auto favorites = repository.previewPage(0, 10, {}, QStringLiteral("favorite"));
    QVERIFY(favorites.status);
    QCOMPARE(favorites.entries.size(), 1);
    QCOMPARE(favorites.entries.first().id, firstId);

    QString orphanImageRef = QStringLiteral("unchanged-on-error");
    QVERIFY(repository.remove(firstId, &orphanImageRef));
    QVERIFY(orphanImageRef.isEmpty());
    HistoryEntry remainingImage;
    QVERIFY(repository.get(sharedImageId, remainingImage));

    QStringList orphanRefs;
    QVERIFY(repository.clear(&orphanRefs));
    QCOMPARE(orphanRefs, QStringList{QStringLiteral("shared-ref")});
    QCOMPARE(repository.page(0, 10).entries.size(), 0);
    QVERIFY(repository.remove(secondId).error == RepositoryError::NotFound);
}

void HistoryRepositoryTest::incrementsExactDuplicateUsage()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("dedupe.sqlite3")));
    QVERIFY(repository.initialize());

    qint64 originalId = 0;
    bool incremented = true;
    const NewHistoryEntry original{QStringLiteral("text"), QStringLiteral("same"), {}, 100};
    QVERIFY(repository.recordOrIncrement(original, originalId, incremented));
    QVERIFY(!incremented);

    qint64 duplicateId = 0;
    QVERIFY(repository.recordOrIncrement({QStringLiteral("text"), QStringLiteral("same"), {}, 100},
                                         duplicateId, incremented));
    QVERIFY(incremented);
    QCOMPARE(duplicateId, originalId);
    const auto afterDuplicate = repository.page(0, 1);
    QVERIFY(afterDuplicate.status);
    QCOMPARE(afterDuplicate.entries.first().id, originalId);

    qint64 differentId = 0;
    QVERIFY(repository.recordOrIncrement({QStringLiteral("text"), QStringLiteral("different"), {}, 201},
                                         differentId, incremented));
    QVERIFY(!incremented);
    QVERIFY(differentId != originalId);

    HistoryEntry loaded;
    QVERIFY(repository.get(originalId, loaded));
    QCOMPARE(loaded.createdAtMsUtc, qint64(101));
    QCOMPARE(loaded.usageCount, qint64(2));
}

void HistoryRepositoryTest::migratesVersionOneUsageCount()
{
    QVERIFY(m_tempDir.isValid());
    const QString databasePath = m_tempDir.filePath(QStringLiteral("version-one.sqlite3"));
    const QString connectionName = QStringLiteral("version_one_test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        {
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE history_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
                "content TEXT NOT NULL, image_ref TEXT NOT NULL, created_at_ms_utc INTEGER NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc) "
                "VALUES('text', 'legacy', '', 42)")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
        }
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());
    HistoryEntry migrated;
    QVERIFY(repository.get(1, migrated));
    QCOMPARE(migrated.content, QStringLiteral("legacy"));
    QCOMPARE(migrated.usageCount, qint64(1));
    qint64 duplicateId = 0;
    bool incremented = false;
    QVERIFY(repository.recordOrIncrement({QStringLiteral("text"), QStringLiteral("legacy"), {}, 43},
                                         duplicateId, incremented));
    QVERIFY(incremented);
    QCOMPARE(duplicateId, migrated.id);
}

void HistoryRepositoryTest::rejectsInvalidArguments()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("invalid.sqlite3")));
    QVERIFY(repository.initialize());

    qint64 id = 0;
    HistoryEntry entry;
    QCOMPARE(repository.insert({{}, QStringLiteral("x"), {}, 0}, id).error,
             RepositoryError::InvalidArgument);
    QCOMPARE(repository.page(-1, 10).status.error, RepositoryError::InvalidArgument);
    QCOMPARE(repository.page(0, 0).status.error, RepositoryError::InvalidArgument);
    QCOMPARE(repository.get(0, entry).error, RepositoryError::InvalidArgument);
}

void HistoryRepositoryTest::rejectsCallsFromAnotherThread()
{
    QVERIFY(m_tempDir.isValid());
    HistoryRepository repository(m_tempDir.filePath(QStringLiteral("thread.sqlite3")));
    QVERIFY(repository.initialize());

    RepositoryStatus status;
    QThread* worker = QThread::create([&repository, &status] {
        status = repository.page(0, 1).status;
    });
    worker->start();
    QVERIFY(worker->wait());
    delete worker;
    QCOMPARE(status.error, RepositoryError::WrongThread);
}

void HistoryRepositoryTest::migratesVersionZeroSchema()
{
    QVERIFY(m_tempDir.isValid());
    const QString databasePath = m_tempDir.filePath(QStringLiteral("version-zero.sqlite3"));
    const QString connectionName = QStringLiteral("version_zero_test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        {
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 0")));
        }
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());
    const QString verifyConnection = QStringLiteral("version_zero_verify");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        {
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 5);
        }
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(verifyConnection);
}

void HistoryRepositoryTest::migratesVersionThreeFavorites()
{
    const QString databasePath = m_tempDir.filePath(QStringLiteral("version-three.sqlite3"));
    const QString connectionName = QStringLiteral("version_three_test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE history_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
            "content TEXT NOT NULL, image_ref TEXT NOT NULL, created_at_ms_utc INTEGER NOT NULL, "
            "usage_count INTEGER NOT NULL DEFAULT 1, content_hash TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, content_hash) "
            "VALUES('text', 'preserved', '', 7, 'hash')")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 3")));
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());
    HistoryEntry entry;
    QVERIFY(repository.get(1, entry));
    QCOMPARE(entry.content, QStringLiteral("preserved"));
    QVERIFY(!entry.favorite);
    QVERIFY(repository.setFavorite(1, true));
    QVERIFY(repository.get(1, entry));
    QVERIFY(entry.favorite);
}

void HistoryRepositoryTest::migratesVersionTwoContentHash()
{
    QVERIFY(m_tempDir.isValid());
    const QString databasePath = m_tempDir.filePath(QStringLiteral("version-two.sqlite3"));
    const QString connectionName = QStringLiteral("version_two_test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        {
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE history_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
                "content TEXT NOT NULL, image_ref TEXT NOT NULL, created_at_ms_utc INTEGER NOT NULL, "
                "usage_count INTEGER NOT NULL DEFAULT 1)")));
            QSqlQuery insert(database);
            insert.prepare(QStringLiteral(
                "INSERT INTO history_entries(kind, content, image_ref, created_at_ms_utc, usage_count) "
                "VALUES('text', :content, '', 42, 5)"));
            for (int index = 0; index < 300; ++index) {
                insert.bindValue(QStringLiteral(":content"), QStringLiteral("version two %1").arg(index));
                QVERIFY(insert.exec());
                insert.finish();
            }
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 2")));
        }
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());
    HistoryEntry migrated;
    QVERIFY(repository.get(1, migrated));
    QCOMPARE(migrated.content, QStringLiteral("version two 0"));
    QCOMPARE(migrated.usageCount, qint64(5));
    qint64 duplicateId = 0;
    bool incremented = false;
    QVERIFY(repository.recordOrIncrement({QStringLiteral("text"), QStringLiteral("version two 299"), {}, 43},
                                         duplicateId, incremented));
    QVERIFY(incremented);
    QCOMPARE(duplicateId, qint64(300));
    QVERIFY(repository.get(duplicateId, migrated));
    QCOMPARE(migrated.usageCount, qint64(6));
}

void HistoryRepositoryTest::rejectsFutureSchema()
{
    QVERIFY(m_tempDir.isValid());
    const QString databasePath = m_tempDir.filePath(QStringLiteral("future.sqlite3"));
    const QString connectionName = QStringLiteral("future_schema_test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        {
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 99")));
        }
        database.close();
        database = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HistoryRepository repository(databasePath);
    QCOMPARE(repository.initialize().error, RepositoryError::UnsupportedSchemaVersion);
}

void HistoryRepositoryTest::rejectsCorruptDatabase()
{
    QVERIFY(m_tempDir.isValid());
    const QString databasePath = m_tempDir.filePath(QStringLiteral("corrupt.sqlite3"));
    QFile file(databasePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not a sqlite database"), qint64(21));
    file.close();

    HistoryRepository repository(databasePath);
    const auto status = repository.initialize();
    QVERIFY(status.error == RepositoryError::OpenFailed || status.error == RepositoryError::SqlError);
}

QTEST_GUILESS_MAIN(HistoryRepositoryTest)
#include "tst_history_repository.moc"
