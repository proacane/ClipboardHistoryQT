#include "../../src/core/history/history_task_queue.h"
#include "../../src/core/history/history_repository.h"

#include <QFile>
#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using ClipboardHistory::Core::HistoryRepository;
using ClipboardHistory::Core::HistoryServiceError;
using ClipboardHistory::Core::HistoryTaskQueue;
using ClipboardHistory::Core::RepositoryError;

class HistoryTaskQueueTest final : public QObject
{
    Q_OBJECT

private slots:
    void quantityLimitResumesAndEnforcesAfterCaptureAndCanBeDisabled();
    void appliesRetentionConfiguredBeforeInitialization();
    void retentionIsDisabledByDefaultAndRenewedEntriesSurvive();
    void retentionTimerRepeatsAndCanBeDisabled();
    void recordsInOrderAndDrainsBeforeShutdown();
    void releasesPayloadBudgetAfterCompletion();
    void rejectsSubmissionsWhenInitializationFails();
};

void HistoryTaskQueueTest::quantityLimitResumesAndEnforcesAfterCaptureAndCanBeDisabled()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        qint64 id = 0;
        for (int i = 0; i < 503; ++i) QVERIFY(repo.insert({"text", QString::number(i), {}, i + 1}, id));
        QVERIFY(repo.setFavorite(1, true));
    }
    HistoryTaskQueue queue(path, temp.filePath("images"));
    QSignalSpy retention(&queue, &HistoryTaskQueue::retentionCompleted);
    QVERIFY(queue.setMaximumEntries(2));
    QTRY_VERIFY(retention.count() >= 2);
    qint64 deleted = 0;
    for (const auto &args : retention) deleted += qvariant_cast<ClipboardHistory::Core::HistoryMutationResult>(args[0]).removedCount;
    QCOMPARE(deleted, 501);
    HistoryRepository repo(path); QVERIFY(repo.initialize());
    QCOMPARE(repo.page(0, 10).entries.size(), 2);
    retention.clear();
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QCOMPARE(queue.submit({"text", "newest", {}}), HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    QTRY_COMPARE(retention.count(), 1);
    QCOMPARE(qvariant_cast<ClipboardHistory::Core::HistoryMutationResult>(retention[0][0]).removedCount, 1);
    QCOMPARE(repo.page(0, 10).entries.size(), 2);
    QCOMPARE(repo.page(0, 10).entries[0].content, QStringLiteral("newest"));
    QVERIFY(!queue.setMaximumEntries(-1));
    QVERIFY(queue.setMaximumEntries(0));
    retention.clear();
    QCOMPARE(queue.submit({"text", "unlimited", {}}), HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 2);
    QTest::qWait(30); QCOMPARE(retention.count(), 0);
    QCOMPARE(repo.page(0, 10).entries.size(), 3);
}

void HistoryTaskQueueTest::appliesRetentionConfiguredBeforeInitialization()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        qint64 id = 0;
        QVERIFY(repo.insert({"text", "expired-at-startup", {}, 1}, id));
    }
    HistoryTaskQueue queue(path, temp.filePath("images"));
    QSignalSpy retention(&queue, &HistoryTaskQueue::retentionCompleted);
    QVERIFY(queue.setRetentionHours(1));
    QTRY_COMPARE(retention.count(), 1);
    const auto result = qvariant_cast<ClipboardHistory::Core::HistoryMutationResult>(retention[0][0]);
    QVERIFY(result.status); QCOMPARE(result.removedCount, 1);
}

void HistoryTaskQueueTest::retentionIsDisabledByDefaultAndRenewedEntriesSurvive()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        qint64 id = 0;
        for (int i = 0; i < 501; ++i)
            QVERIFY(repo.insert({"text", QString::number(i), {}, 1}, id));
        QVERIFY(repo.insert({"text", "favorite", {}, 1}, id)); QVERIFY(repo.setFavorite(id, true));
        QVERIFY(repo.insert({"text", "renewed", {}, 1}, id));
    }
    HistoryTaskQueue queue(path, temp.filePath("images"));
    QSignalSpy ready(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy retention(&queue, &HistoryTaskQueue::retentionCompleted);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(ready.count(), 1);
    QTest::qWait(30);
    QCOMPARE(retention.count(), 0);
    QCOMPARE(queue.submit({"text", "renewed", {}}), HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    QVERIFY(queue.setRetentionHours(1));
    QTRY_VERIFY(retention.count() >= 2);
    qint64 deleted = 0;
    for (const auto &args : retention) {
        const auto result = qvariant_cast<ClipboardHistory::Core::HistoryMutationResult>(args[0]);
        QVERIFY(result.status); deleted += result.removedCount;
        QVERIFY(result.removedCount <= 500);
    }
    QCOMPARE(deleted, 501);
    QVERIFY(!queue.setRetentionHours(-1));
    QVERIFY(!queue.setRetentionHours(HistoryTaskQueue::maxRetentionHours() + 1));
    QVERIFY(queue.setRetentionHours(0));
    queue.shutdown();
    QVERIFY(!queue.setRetentionHours(1));
    QSignalSpy stopped(&queue, &HistoryTaskQueue::stopped);
    QTRY_COMPARE(stopped.count(), 1);
    HistoryRepository repo(path); QVERIFY(repo.initialize());
    QCOMPARE(repo.page(0, 10).entries.size(), 2);
}

void HistoryTaskQueueTest::retentionTimerRepeatsAndCanBeDisabled()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    ClipboardHistory::Core::HistoryWorker worker(path, temp.filePath("images"));
    worker.initialize();
    auto *timer = worker.findChild<QTimer *>();
    QVERIFY(timer); QVERIFY(!timer->isActive()); QCOMPARE(timer->interval(), 60000);
    QSignalSpy retention(&worker, &ClipboardHistory::Core::HistoryWorker::retentionCompleted);
    worker.setRetentionHours(1);
    QVERIFY(timer->isActive());
    QTRY_COMPARE(retention.count(), 1);
    qint64 id = 0;
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        QVERIFY(repo.insert({"text", "expire-on-next-tick", {}, 1}, id));
    }
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    QTRY_COMPARE(retention.count(), 2);
    QCOMPARE(qvariant_cast<ClipboardHistory::Core::HistoryMutationResult>(retention[1][0]).removedCount, 1);
    worker.setRetentionHours(0);
    QVERIFY(!timer->isActive());
    {
        HistoryRepository repo(path); QVERIFY(repo.initialize());
        QVERIFY(repo.insert({"text", "keep-after-disable", {}, 1}, id));
    }
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    QTest::qWait(30);
    QCOMPARE(retention.count(), 2);
    HistoryRepository repo(path); QVERIFY(repo.initialize());
    QCOMPARE(repo.page(0, 10).entries.size(), 1);
}

void HistoryTaskQueueTest::recordsInOrderAndDrainsBeforeShutdown()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString databasePath = tempDir.filePath(QStringLiteral("history.sqlite3"));
    HistoryTaskQueue queue(databasePath, tempDir.filePath(QStringLiteral("images")));
    QSignalSpy initializedSpy(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recordSpy(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy stoppedSpy(&queue, &HistoryTaskQueue::stopped);

    QTRY_COMPARE(initializedSpy.count(), 1);
    QVERIFY(qvariant_cast<ClipboardHistory::Core::HistoryServiceStatus>(
        initializedSpy.takeFirst().at(0)));

    QImage oversizedImage(4200, 4200, QImage::Format_ARGB32);
    QVERIFY(!oversizedImage.isNull());
    QCOMPARE(queue.submit({QStringLiteral("image"), {}, oversizedImage}),
             HistoryTaskQueue::SubmitResult::QueueFull);

    const QStringList contents{QStringLiteral("first"), QStringLiteral("second"),
                               QStringLiteral("third")};
    for (const QString &content : contents) {
        QCOMPARE(queue.submit({QStringLiteral("text"), content, {}}),
                 HistoryTaskQueue::SubmitResult::Accepted);
    }

    queue.shutdown();
    QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("late"), {}}),
             HistoryTaskQueue::SubmitResult::Stopping);
    QTRY_COMPARE(stoppedSpy.count(), 1);
    QCOMPARE(recordSpy.count(), contents.size());

    HistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());
    const auto page = repository.page(0, 10);
    QVERIFY(page.status);
    QCOMPARE(page.entries.size(), contents.size());
    for (qsizetype i = 0; i < contents.size(); ++i)
        QCOMPARE(page.entries.at(contents.size() - i - 1).content, contents.at(i));
}

void HistoryTaskQueueTest::releasesPayloadBudgetAfterCompletion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    HistoryTaskQueue queue(tempDir.filePath(QStringLiteral("history.sqlite3")),
                           tempDir.filePath(QStringLiteral("images")));
    QSignalSpy initializedSpy(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recordSpy(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy stoppedSpy(&queue, &HistoryTaskQueue::stopped);

    QTRY_COMPARE(initializedSpy.count(), 1);
    QVERIFY(qvariant_cast<ClipboardHistory::Core::HistoryServiceStatus>(
        initializedSpy.takeFirst().at(0)));

    QImage image(3840, 2160, QImage::Format_ARGB32);
    QVERIFY(!image.isNull());
    image.fill(Qt::green);
    QCOMPARE(queue.submit({QStringLiteral("image"), {}, image}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recordSpy.count(), 1);
    QCOMPARE(queue.submit({QStringLiteral("image"), {}, image}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recordSpy.count(), 2);

    queue.shutdown();
    QTRY_COMPARE(stoppedSpy.count(), 1);
}

void HistoryTaskQueueTest::rejectsSubmissionsWhenInitializationFails()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString blockerPath = tempDir.filePath(QStringLiteral("blocker"));
    QFile blocker(blockerPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    HistoryTaskQueue queue(blockerPath + QStringLiteral("/history.sqlite3"),
                           tempDir.filePath(QStringLiteral("images")));
    QSignalSpy initializedSpy(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy stoppedSpy(&queue, &HistoryTaskQueue::stopped);

    QTRY_COMPARE(initializedSpy.count(), 1);
    const auto status = qvariant_cast<ClipboardHistory::Core::HistoryServiceStatus>(
        initializedSpy.takeFirst().at(0));
    QCOMPARE(status.error, HistoryServiceError::RepositoryFailure);
    QCOMPARE(status.repositoryError, RepositoryError::OpenFailed);
    QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("ignored"), {}}),
             HistoryTaskQueue::SubmitResult::NotReady);

    queue.shutdown();
    QTRY_COMPARE(stoppedSpy.count(), 1);
}

QTEST_GUILESS_MAIN(HistoryTaskQueueTest)
#include "tst_history_task_queue.moc"
