#include "../../src/core/history/history_service.h"
#include "../../src/core/history/history_repository.h"

#include <QFile>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using ClipboardHistory::Core::HistoryCapture;
using ClipboardHistory::Core::ImageStoreError;
using ClipboardHistory::Core::HistoryService;
using ClipboardHistory::Core::HistoryServiceError;
using ClipboardHistory::Core::HistoryRepository;
using ClipboardHistory::Core::ImageStore;

class HistoryServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void retentionPreservesSharedImagesAndSourceFiles();
    void mergesExactDuplicatesAndKeepsDistinctContent();
    void storesAndDeduplicatesImages();
    void rejectsInvalidAndUninitializedCalls();
    void reportsImageWriteFailureWithoutRecording();
    void deletesOnlyUnreferencedImagesAndClearsHistory();
    void reportsCleanupFailureWithoutRestoringDeletedRows();
    void rejectsCallsFromAnotherThread();
};

void HistoryServiceTest::retentionPreservesSharedImagesAndSourceFiles()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    const auto images = temp.filePath("images");
    ImageStore store(images);
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const auto shared = store.store(image);
    image.fill(Qt::blue);
    const auto unique = store.store(image);
    QVERIFY(shared); QVERIFY(unique);
    const auto source = temp.filePath("original.txt");
    QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("source"); file.close();
    qint64 id = 0;
    {
        HistoryRepository repo(path);
        QVERIFY(repo.initialize());
        QVERIFY(repo.insert({"image", {}, shared.reference, 1}, id));
        QVERIFY(repo.insert({"image", {}, shared.reference, 2}, id));
        QVERIFY(repo.setFavorite(id, true));
        QVERIFY(repo.insert({"image", {}, unique.reference, 1}, id));
        QVERIFY(repo.insert({"path", source, {}, 1}, id));
        QVERIFY(repo.insert({"text", "boundary", {}, 1000}, id));
    }
    HistoryService service(path, images);
    QVERIFY(service.initialize());
    const auto result = service.removeExpired(1000);
    QVERIFY(result.status);
    QCOMPARE(result.removedCount, 3);
    QVERIFY(!result.cleanupPending);
    QCOMPARE(service.page(0, 10).entries.size(), 2);
    QVERIFY(QFile::exists(source));
    QVERIFY(QFile::exists(images + '/' + shared.reference + ".png"));
    QVERIFY(!QFile::exists(images + '/' + unique.reference + ".png"));
    const auto restored = store.store(image); QVERIFY(restored);
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        QVERIFY(repo.insert({"image", {}, unique.reference, 1}, id));
        QVERIFY(repo.insert({"path", source, {}, 1}, id));
    }
    const auto limited = service.enforceRetention(0, 2);
    QVERIFY(limited.status); QCOMPARE(limited.removedCount, 2);
    QVERIFY(!limited.cleanupPending);
    QVERIFY(QFile::exists(source));
    QVERIFY(QFile::exists(images + '/' + shared.reference + ".png"));
    QVERIFY(!QFile::exists(images + '/' + unique.reference + ".png"));
    // A failed file cleanup must not resurrect the already committed row.
    const QString ref(64, 'b');
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        QVERIFY(repo.insert({"image", {}, ref, 1}, id));
    }
    QVERIFY(QDir().mkpath(images + '/' + ref + ".png"));
    const auto failedCleanup = service.removeExpired(1000);
    QVERIFY(failedCleanup.status);
    QVERIFY(failedCleanup.cleanupPending);
    QCOMPARE(failedCleanup.removedCount, 1);
    QCOMPARE(service.page(0, 10).entries.size(), 2);
}

void HistoryServiceTest::mergesExactDuplicatesAndKeepsDistinctContent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    HistoryService service(tempDir.filePath(QStringLiteral("history.sqlite3")),
                           tempDir.filePath(QStringLiteral("images")));
    QVERIFY(service.initialize());

    const auto first = service.record({QStringLiteral("text"), QStringLiteral("same"), {}});
    QVERIFY(first.status);
    QVERIFY(!first.incremented);
    QTest::qWait(2);
    const auto duplicate = service.record({QStringLiteral("text"), QStringLiteral("same"), {}});
    QVERIFY(duplicate.status);
    QVERIFY(duplicate.incremented);
    QCOMPARE(duplicate.id, first.id);

    const auto distinct = service.record({QStringLiteral("text"), QStringLiteral("different"), {}});
    QVERIFY(distinct.status);
    QVERIFY(!distinct.incremented);
    QVERIFY(distinct.id != first.id);

    const auto page = service.page(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), 2);
    QCOMPARE(page.entries.at(0).id, distinct.id);
    QCOMPARE(page.entries.at(1).id, first.id);
    QCOMPARE(page.entries.at(1).usageCount, qint64(2));
}

void HistoryServiceTest::storesAndDeduplicatesImages()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString imagesDirectory = tempDir.filePath(QStringLiteral("images"));
    HistoryService service(tempDir.filePath(QStringLiteral("history.sqlite3")), imagesDirectory);
    QVERIFY(service.initialize());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#3678c8")));
    const auto first = service.record({QStringLiteral("image"), {}, image});
    QVERIFY(first.status);
    QVERIFY(!first.incremented);
    const auto duplicate = service.record({QStringLiteral("image"), {}, image});
    QVERIFY(duplicate.status);
    QVERIFY(duplicate.incremented);
    QCOMPARE(duplicate.id, first.id);

    const auto page = service.page(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), 1);
    QCOMPARE(page.entries.first().usageCount, qint64(2));
    QVERIFY(!page.entries.first().imageRef.isEmpty());
    QVERIFY(QFile::exists(imagesDirectory + QLatin1Char('/') + page.entries.first().imageRef + QStringLiteral(".png")));
}

void HistoryServiceTest::rejectsInvalidAndUninitializedCalls()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    HistoryService service(tempDir.filePath(QStringLiteral("history.sqlite3")),
                           tempDir.filePath(QStringLiteral("images")));
    QCOMPARE(service.record({QStringLiteral("text"), QStringLiteral("ignored"), {}}).status.error,
             HistoryServiceError::NotInitialized);
    QCOMPARE(service.removeExpired(1000).status.error, HistoryServiceError::NotInitialized);
    QVERIFY(service.initialize());
    QCOMPARE(service.removeExpired(0).status.repositoryError, ClipboardHistory::Core::RepositoryError::InvalidArgument);
    QCOMPARE(service.record({{}, QStringLiteral("ignored"), {}}).status.error,
             HistoryServiceError::InvalidEntry);
    QCOMPARE(service.record({QStringLiteral("image"), {}, {}}).status.error,
             HistoryServiceError::InvalidEntry);
    QCOMPARE(service.page(0, 10).entries.size(), 0);
}

void HistoryServiceTest::reportsImageWriteFailureWithoutRecording()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString notDirectory = tempDir.filePath(QStringLiteral("not-a-directory"));
    QFile blocker(notDirectory);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    HistoryService service(tempDir.filePath(QStringLiteral("history.sqlite3")), notDirectory);
    QVERIFY(service.initialize());
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const auto result = service.record({QStringLiteral("image"), {}, image});
    QCOMPARE(result.status.error, HistoryServiceError::ImageStoreFailure);
    QCOMPARE(result.status.imageStoreError, ImageStoreError::DirectoryUnavailable);
    QCOMPARE(service.page(0, 10).entries.size(), 0);
}

void HistoryServiceTest::deletesOnlyUnreferencedImagesAndClearsHistory()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString databasePath = tempDir.filePath(QStringLiteral("history.sqlite3"));
    const QString imagesDirectory = tempDir.filePath(QStringLiteral("images"));
    ImageStore store(imagesDirectory);
    QImage sharedImage(2, 2, QImage::Format_ARGB32);
    sharedImage.fill(Qt::red);
    QImage uniqueImage(2, 2, QImage::Format_ARGB32);
    uniqueImage.fill(Qt::blue);
    const auto shared = store.store(sharedImage);
    const auto unique = store.store(uniqueImage);
    QVERIFY(shared);
    QVERIFY(unique);

    qint64 sharedFirst = 0;
    qint64 sharedSecond = 0;
    qint64 uniqueId = 0;
    qint64 textId = 0;
    {
        HistoryRepository seed(databasePath);
        QVERIFY(seed.initialize());
        QVERIFY(seed.insert({QStringLiteral("image"), {}, shared.reference, 3}, sharedFirst));
        QVERIFY(seed.insert({QStringLiteral("image"), {}, shared.reference, 2}, sharedSecond));
        QVERIFY(seed.insert({QStringLiteral("image"), {}, unique.reference, 1}, uniqueId));
        QVERIFY(seed.insert({QStringLiteral("text"), QStringLiteral("keep before clear"), {}, 0}, textId));
        QVERIFY(uniqueId > 0);
        QVERIFY(textId > 0);
    }

    HistoryService service(databasePath, imagesDirectory);
    QVERIFY(service.initialize());
    const auto firstDelete = service.remove(sharedFirst);
    QVERIFY(firstDelete.status);
    QVERIFY(!firstDelete.cleanupPending);
    QVERIFY(QFile::exists(imagesDirectory + QLatin1Char('/') + shared.reference + QStringLiteral(".png")));
    const auto secondDelete = service.remove(sharedSecond);
    QVERIFY(secondDelete.status);
    QVERIFY(!secondDelete.cleanupPending);
    QVERIFY(!QFile::exists(imagesDirectory + QLatin1Char('/') + shared.reference + QStringLiteral(".png")));

    const auto clearResult = service.clear();
    QVERIFY(clearResult.status);
    QVERIFY(!clearResult.cleanupPending);
    QVERIFY(!QFile::exists(imagesDirectory + QLatin1Char('/') + unique.reference + QStringLiteral(".png")));
    QCOMPARE(service.page(0, 10).entries.size(), 0);
}

void HistoryServiceTest::reportsCleanupFailureWithoutRestoringDeletedRows()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString databasePath = tempDir.filePath(QStringLiteral("history.sqlite3"));
    const QString imagesDirectory = tempDir.filePath(QStringLiteral("images"));
    const QString reference(64, QLatin1Char('a'));
    qint64 id = 0;
    {
        HistoryRepository seed(databasePath);
        QVERIFY(seed.initialize());
        QVERIFY(seed.insert({QStringLiteral("image"), {}, reference, 1}, id));
    }
    QVERIFY(QDir().mkpath(imagesDirectory + QLatin1Char('/') + reference + QStringLiteral(".png")));

    HistoryService service(databasePath, imagesDirectory);
    QVERIFY(service.initialize());
    const auto result = service.remove(id);
    QVERIFY(result.status);
    QVERIFY(result.cleanupPending);
    QCOMPARE(service.page(0, 10).entries.size(), 0);
    QVERIFY(QFile::exists(imagesDirectory + QLatin1Char('/') + reference + QStringLiteral(".png")));
}

void HistoryServiceTest::rejectsCallsFromAnotherThread()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    HistoryService service(tempDir.filePath(QStringLiteral("history.sqlite3")),
                           tempDir.filePath(QStringLiteral("images")));
    QVERIFY(service.initialize());

    HistoryServiceError error = HistoryServiceError::None;
    HistoryServiceError retentionError = HistoryServiceError::None;
    QThread *worker = QThread::create([&service, &error, &retentionError] {
        retentionError = service.removeExpired(1000).status.error;
        error = service.record({QStringLiteral("text"), QStringLiteral("wrong thread"), {}}).status.error;
    });
    worker->start();
    QVERIFY(worker->wait());
    delete worker;
    QCOMPARE(error, HistoryServiceError::WrongThread);
    QCOMPARE(retentionError, HistoryServiceError::WrongThread);
    QCOMPARE(service.page(0, 10).entries.size(), 0);
}

QTEST_GUILESS_MAIN(HistoryServiceTest)
#include "tst_history_service.moc"
