#include "../../src/core/history/image_store.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using ClipboardHistory::Core::ImageStore;
using ClipboardHistory::Core::ImageStoreError;

class ImageStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void storesLoadsAndRemovesImage();
    void deduplicatesIdenticalImages();
    void refusesToOverwriteHashConflict();
    void rejectsInvalidInputsAndReferences();
    void rejectsNonDirectoryDestination();
    void rejectsCallsFromAnotherThread();
};

void ImageStoreTest::storesLoadsAndRemovesImage()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ImageStore store(tempDir.filePath(QStringLiteral("images")));
    QImage source(4, 3, QImage::Format_ARGB32);
    source.fill(QColor(12, 34, 56, 78));

    const auto stored = store.store(source);
    QVERIFY(stored);
    QCOMPARE(stored.reference.size(), 64);
    QVERIFY(QFile::exists(tempDir.filePath(QStringLiteral("images/%1.png").arg(stored.reference))));

    QImage loaded;
    QVERIFY(store.load(stored.reference, loaded));
    QCOMPARE(loaded.size(), source.size());
    QCOMPARE(loaded.pixelColor(0, 0), source.pixelColor(0, 0));
    QVERIFY(store.remove(stored.reference));
    QCOMPARE(store.load(stored.reference, loaded).error, ImageStoreError::NotFound);
    QCOMPARE(store.remove(stored.reference).error, ImageStoreError::NotFound);
}

void ImageStoreTest::deduplicatesIdenticalImages()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ImageStore store(tempDir.filePath(QStringLiteral("images")));
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);

    const auto first = store.store(image);
    const auto second = store.store(image);
    QVERIFY(first);
    QVERIFY(second);
    QCOMPARE(first.reference, second.reference);
    QCOMPARE(QDir(tempDir.filePath(QStringLiteral("images"))).entryList(QDir::Files).size(), 1);
}

void ImageStoreTest::refusesToOverwriteHashConflict()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ImageStore store(tempDir.filePath(QStringLiteral("images")));
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    const auto stored = store.store(image);
    QVERIFY(stored);

    QFile file(tempDir.filePath(QStringLiteral("images/%1.png").arg(stored.reference)));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("corrupt"), qint64(7));
    file.close();

    QCOMPARE(store.store(image).error, ImageStoreError::HashConflict);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("corrupt"));
}
void ImageStoreTest::rejectsInvalidInputsAndReferences()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ImageStore store(tempDir.filePath(QStringLiteral("images")));
    QCOMPARE(store.store(QImage()).error, ImageStoreError::InvalidImage);

    QImage image;
    QCOMPARE(store.load(QStringLiteral("../outside"), image).error, ImageStoreError::InvalidReference);
    QCOMPARE(store.remove(QStringLiteral("../outside")).error, ImageStoreError::InvalidReference);
    QCOMPARE(store.load(QString(64, QLatin1Char('0')), image).error, ImageStoreError::NotFound);
}

void ImageStoreTest::rejectsNonDirectoryDestination()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString blockedPath = tempDir.filePath(QStringLiteral("not-a-directory"));
    QFile blocker(blockedPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    ImageStore store(blockedPath);
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::white);
    QCOMPARE(store.store(image).error, ImageStoreError::DirectoryUnavailable);
}

void ImageStoreTest::rejectsCallsFromAnotherThread()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ImageStore store(tempDir.filePath(QStringLiteral("images")));
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::black);
    ImageStoreError error = ImageStoreError::None;
    QThread *worker = QThread::create([&store, &image, &error] {
        error = store.store(image).error;
    });
    worker->start();
    QVERIFY(worker->wait());
    delete worker;
    QCOMPARE(error, ImageStoreError::WrongThread);
}

QTEST_GUILESS_MAIN(ImageStoreTest)
#include "tst_image_store.moc"
