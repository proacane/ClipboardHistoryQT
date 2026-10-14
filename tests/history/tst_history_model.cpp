#include "../../src/core/history/history_model.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using ClipboardHistory::Core::HistoryModel;
using ClipboardHistory::Core::HistoryServiceStatus;
using ClipboardHistory::Core::HistoryTaskQueue;

namespace {
bool createSourceFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}
}

class HistoryModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void retentionRefreshesRowsAndTotalCount();
    void loadsBoundedPagesAndCopiesFullEntries();
    void reportsMissingSourceFilesAndLoadsExpandedContent();
    void exposesImagePreviewUrl();
    void favoritesDeletesAndClearsThroughModel();
    void totalCountIsIndependentOfPagingAndTracksMutations();
    void sourceDeletionAndRestorationUpdateWithoutRefresh();
    void sourceParentDeletionAndRestorationUpdateWithoutRefresh();
    void longFileListsUseCompleteSourcePaths();
    void driveRootDirectoryNotificationRechecksSource();
    void changesDuringPendingSourceCheckAreRechecked();
};

void HistoryModelTest::retentionRefreshesRowsAndTotalCount()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("history.sqlite3");
    {
        ClipboardHistory::Core::HistoryRepository repo(path); QVERIFY(repo.initialize());
        qint64 id = 0;
        QVERIFY(repo.insert({"text", "expired", {}, 1}, id));
        QVERIFY(repo.insert({"text", "favorite", {}, 1}, id)); QVERIFY(repo.setFavorite(id, true));
        QVERIFY(repo.insert({"text", "recent", {}, QDateTime::currentMSecsSinceEpoch()}, id));
    }
    HistoryTaskQueue queue(path, temp.filePath("images"));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    HistoryModel model(&queue, temp.filePath("images"));
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    tester.setUseFetchMore(false);
    QTRY_COMPARE(initialized.count(), 1);
    model.refresh();
    QTRY_COMPARE(model.totalCount(), 3);
    QSignalSpy completed(&model, &HistoryModel::retentionCompleted);
    model.setRetentionHours(1);
    QTRY_COMPARE(completed.count(), 1);
    QTRY_COMPARE(model.totalCount(), 2);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(completed[0][0].toLongLong(), 1);
    model.setRetentionHours(0);
    QVERIFY(!model.hasError());
}

void HistoryModelTest::driveRootDirectoryNotificationRechecksSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(createSourceFile(source));
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy sourceRequests(&queue, &HistoryTaskQueue::entryLoadRequested);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    QCOMPARE(queue.submit({QStringLiteral("path"), source, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(QFile::remove(source));
#ifdef Q_OS_WIN
    const QString root = QDir::fromNativeSeparators(directory.path()).left(3);
#else
    const QString root = QStringLiteral("/");
#endif
    QVERIFY(root.endsWith(QLatin1Char('/')));
    QVERIFY(QMetaObject::invokeMethod(&model, "onSourceDirectoryChanged", Qt::DirectConnection,
                                     Q_ARG(QString, root)));
    QCOMPARE(sourceRequests.count(), 1);
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    queue.shutdown();
}

void HistoryModelTest::changesDuringPendingSourceCheckAreRechecked()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(createSourceFile(source));
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy sourceRequests(&queue, &HistoryTaskQueue::entryLoadRequested);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    QCOMPARE(queue.submit({QStringLiteral("path"), source, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    const qint64 id = model.data(model.index(0, 0), HistoryModel::IdRole).toLongLong();
    QVERIFY(id > 0);
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());

    // Simulate delayed completion deterministically; the other tests cover actual worker I/O.
    QVERIFY(disconnect(&queue, &HistoryTaskQueue::entryLoaded, &model, nullptr));
    QVERIFY(QFile::remove(source));
    QVERIFY(QMetaObject::invokeMethod(&model, "onSourceDirectoryChanged", Qt::DirectConnection,
                                     Q_ARG(QString, directory.path())));
    QCOMPARE(sourceRequests.count(), 1);
    QVERIFY(createSourceFile(source));
    QVERIFY(QMetaObject::invokeMethod(&model, "onSourceDirectoryChanged", Qt::DirectConnection,
                                     Q_ARG(QString, directory.path())));
    QCOMPARE(sourceRequests.count(), 1);

    const ClipboardHistory::Core::HistoryCopyResult staleResult{
        {}, QStringLiteral("path"), source, {}, true};
    QVERIFY(QMetaObject::invokeMethod(&model, "onEntryLoaded", Qt::DirectConnection,
                                     Q_ARG(qint64, id),
                                     Q_ARG(ClipboardHistory::Core::HistoryCopyResult, staleResult)));
    QCOMPARE(sourceRequests.count(), 2);
    const ClipboardHistory::Core::HistoryCopyResult latestResult{
        {}, QStringLiteral("path"), source, {}, false};
    QVERIFY(QMetaObject::invokeMethod(&model, "onEntryLoaded", Qt::DirectConnection,
                                     Q_ARG(qint64, id),
                                     Q_ARG(ClipboardHistory::Core::HistoryCopyResult, latestResult)));
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    queue.shutdown();
}

void HistoryModelTest::totalCountIsIndependentOfPagingAndTracksMutations()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    for (int i = 0; i < 120; ++i) {
        QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("record-%1").arg(i), {}}),
                 HistoryTaskQueue::SubmitResult::Accepted);
        if ((i + 1) % 30 == 0)
            QTRY_COMPARE(recorded.count(), i + 1);
    }
    QTRY_COMPARE(recorded.count(), 120);

    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 50);
    QCOMPARE(model.totalCount(), qint64(120));
    QVERIFY(model.canFetchMore({}));
    model.fetchMore({});
    QTRY_COMPARE(model.rowCount(), 100);
    QCOMPARE(model.totalCount(), qint64(120));
    model.fetchMore({});
    QTRY_COMPARE(model.rowCount(), 120);
    QCOMPARE(model.totalCount(), qint64(120));
    QVERIFY(!model.canFetchMore({}));

    const ClipboardHistory::Core::HistoryCapture newEntry{
        QStringLiteral("text"), QStringLiteral("new-record"), {}};
    QCOMPARE(queue.submit(newEntry), HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 121);
    QTRY_COMPARE(model.totalCount(), qint64(121));
    const qint64 newId = qvariant_cast<ClipboardHistory::Core::HistoryRecordResult>(
        recorded.last().at(0)).id;
    QVERIFY(newId > 0);
    QCOMPARE(queue.submit(newEntry), HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 122);
    QVERIFY(qvariant_cast<ClipboardHistory::Core::HistoryRecordResult>(
        recorded.last().at(0)).incremented);
    QCOMPARE(model.totalCount(), qint64(121));

    model.removeItemById(newId);
    QTRY_COMPARE(model.totalCount(), qint64(120));
    model.clearHistory();
    QTRY_COMPARE(model.totalCount(), qint64(0));
    QTRY_COMPARE(model.rowCount(), 0);
    queue.shutdown();
}

void HistoryModelTest::sourceDeletionAndRestorationUpdateWithoutRefresh()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(createSourceFile(source));
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    QCOMPARE(queue.submit({QStringLiteral("path"), source, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(QFile::remove(source));
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(createSourceFile(source));
    QTRY_VERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(QFile::remove(source));
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    queue.shutdown();
}

void HistoryModelTest::sourceParentDeletionAndRestorationUpdateWithoutRefresh()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString parent = directory.filePath(QStringLiteral("source-parent/nested"));
    QVERIFY(QDir().mkpath(parent));
    const QString source = QDir(parent).filePath(QStringLiteral("source.txt"));
    QVERIFY(createSourceFile(source));
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    QCOMPARE(queue.submit({QStringLiteral("path"), source, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(QDir(directory.filePath(QStringLiteral("source-parent"))).removeRecursively());
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(QDir().mkpath(parent));
    QVERIFY(createSourceFile(source));
    QTRY_VERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    queue.shutdown();
}

void HistoryModelTest::longFileListsUseCompleteSourcePaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (int i = 0; i < 12; ++i) {
        const QString path = directory.filePath(QStringLiteral("source-%1-abcdefghijklmnopqrstuvwxyz.txt").arg(i));
        QVERIFY(createSourceFile(path));
        paths.append(path);
    }
    const QString content = paths.join(QLatin1Char('\n'));
    QVERIFY(content.size() > 512);
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().at(0)));
    QCOMPARE(queue.submit({QStringLiteral("path"), content, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    HistoryModel model(&queue);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    const QString lastPath = paths.last();
    QVERIFY(QFile::remove(lastPath));
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    QVERIFY(createSourceFile(lastPath));
    QTRY_VERIFY(!model.data(model.index(0, 0), HistoryModel::SourceMissingRole).toBool());
    queue.shutdown();
}

void HistoryModelTest::loadsBoundedPagesAndCopiesFullEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy pageRequests(&queue, &HistoryTaskQueue::pageRequested);
    QSignalSpy copied(&queue, &HistoryTaskQueue::copyReady);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.takeFirst().at(0)));

    HistoryModel model(&queue, directory.filePath(QStringLiteral("images")));
    QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QString longText(10000, QLatin1Char('x'));
    QCOMPARE(queue.submit({QStringLiteral("text"), longText, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);
    model.refresh();
    QTRY_VERIFY(model.rowCount() >= 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(pageRequests.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::ClipTextRole).toString().size(), 513);
    QVERIFY(model.data(model.index(0, 0), HistoryModel::ExpandableRole).toBool());

    const qint64 id = model.data(model.index(0, 0), HistoryModel::IdRole).toLongLong();
    QSignalSpy contentLoaded(&model, &HistoryModel::entryContentLoaded);
    model.loadEntry(id);
    QTRY_COMPARE(contentLoaded.count(), 1);
    QCOMPARE(contentLoaded.first().at(0).toLongLong(), id);
    QCOMPARE(contentLoaded.first().at(2).toString(), longText);

    model.copyItem(0);
    QTRY_COMPARE(copied.count(), 1);
    const auto copiedText = qvariant_cast<ClipboardHistory::Core::HistoryCopyResult>(
        copied.takeFirst().at(0));
    QVERIFY(copiedText.status);
    QCOMPARE(copiedText.kind, QStringLiteral("text"));
    QCOMPARE(copiedText.content, longText);
    QVERIFY(copiedText.image.isNull());

    queue.shutdown();
}

void HistoryModelTest::reportsMissingSourceFilesAndLoadsExpandedContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.takeFirst().at(0)));

    const QString missingPath = directory.filePath(QStringLiteral("removed-source.txt"));
    QCOMPARE(queue.submit({QStringLiteral("path"), missingPath, {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);

    HistoryModel model(&queue, directory.filePath(QStringLiteral("images")));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::KindRole).toString(), QStringLiteral("文件"));

    QSignalSpy missing(&model, &HistoryModel::copySourceMissing);
    QSignalSpy copied(&model, &HistoryModel::copyContentReady);
    model.copyItem(0);
    QTRY_COMPARE(missing.count(), 1);
    QCOMPARE(copied.count(), 0);
    queue.shutdown();
}

void HistoryModelTest::exposesImagePreviewUrl()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagesDirectory = directory.filePath(QStringLiteral("images"));
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")), imagesDirectory);
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.takeFirst().at(0)));

    QImage image(32, 18, QImage::Format_ARGB32);
    image.fill(Qt::cyan);
    QCOMPARE(queue.submit({QStringLiteral("image"), {}, image}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 1);

    HistoryModel model(&queue, imagesDirectory);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    const QUrl imageUrl = model.data(model.index(0, 0), HistoryModel::ImageUrlRole).toUrl();
    QVERIFY(imageUrl.isLocalFile());
    QVERIFY(QFileInfo::exists(imageUrl.toLocalFile()));
    queue.shutdown();
}

void HistoryModelTest::favoritesDeletesAndClearsThroughModel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    HistoryTaskQueue queue(directory.filePath(QStringLiteral("history.sqlite3")),
                           directory.filePath(QStringLiteral("images")));
    QSignalSpy initialized(&queue, &HistoryTaskQueue::initialized);
    QSignalSpy recorded(&queue, &HistoryTaskQueue::recordCompleted);
    QSignalSpy mutations(&queue, &HistoryTaskQueue::mutationCompleted);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.takeFirst().at(0)));

    HistoryModel model(&queue);
    QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("first"), {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("second"), {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 2);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);

    model.setFavorite(0, true);
    QTRY_COMPARE(mutations.count(), 1);
    QTRY_VERIFY(model.data(model.index(0, 0), HistoryModel::FavoriteRole).toBool());
    const qint64 stableId = model.data(model.index(0, 0), HistoryModel::IdRole).toLongLong();
    QCOMPARE(queue.submit({QStringLiteral("text"), QStringLiteral("newer"), {}}),
             HistoryTaskQueue::SubmitResult::Accepted);
    QTRY_COMPARE(recorded.count(), 3);
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(0, 0), HistoryModel::IdRole).toLongLong() != stableId);
    model.removeItemById(stableId);
    QTRY_COMPARE(mutations.count(), 2);
    QTRY_COMPARE(model.rowCount(), 2);
    for (int row = 0; row < model.rowCount(); ++row)
        QVERIFY(model.data(model.index(row, 0), HistoryModel::IdRole).toLongLong() != stableId);

    model.clearHistory();
    QTRY_COMPARE(mutations.count(), 3);
    QTRY_COMPARE(model.rowCount(), 0);
    queue.shutdown();
}

QTEST_GUILESS_MAIN(HistoryModelTest)
#include "tst_history_model.moc"
