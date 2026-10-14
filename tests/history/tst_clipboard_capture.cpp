#include "../../src/core/history/clipboard_capture.h"

#include <QDir>
#include <QImage>
#include <QMimeData>
#include <QUrl>
#include <QtTest>

using ClipboardHistory::Core::ClipboardCapture;

class ClipboardCaptureTest final : public QObject
{
    Q_OBJECT

private slots:
    void capturesPlainTextAndPrefersPlainTextInRichMime();
    void capturesImagesBeforeOtherRepresentations();
    void captures4kImagesWithinSnapshotLimit();
    void classifiesLocalAndWebUrls();
    void classifiesPlainTextUrlAsLink();
    void rejectsMixedUnsupportedAndEmptyData();
    void honorsSensitiveClipboardMarkers();
    void rejectsOversizedSnapshots();
};

void ClipboardCaptureTest::capturesPlainTextAndPrefersPlainTextInRichMime()
{
    QMimeData mimeData;
    mimeData.setText(QStringLiteral("plain text"));
    mimeData.setHtml(QStringLiteral("<b>rich text</b>"));

    const auto result = ClipboardCapture::snapshot(mimeData);
    QVERIFY(result);
    QCOMPARE(result.capture.kind, QStringLiteral("text"));
    QCOMPARE(result.capture.content, QStringLiteral("plain text"));
    QVERIFY(result.capture.image.isNull());
}

void ClipboardCaptureTest::capturesImagesBeforeOtherRepresentations()
{
    QMimeData mimeData;
    QImage image(4, 3, QImage::Format_ARGB32);
    image.fill(QColor(12, 34, 56, 78));
    mimeData.setImageData(image);
    mimeData.setText(QStringLiteral("an image description"));

    const auto result = ClipboardCapture::snapshot(mimeData);
    QVERIFY(result);
    QCOMPARE(result.capture.kind, QStringLiteral("image"));
    QVERIFY(result.capture.content.isEmpty());
    QCOMPARE(result.capture.image.size(), image.size());
    QCOMPARE(result.capture.image.pixelColor(0, 0), image.pixelColor(0, 0));
}

void ClipboardCaptureTest::captures4kImagesWithinSnapshotLimit()
{
    QMimeData mimeData;
    QImage image(3840, 2160, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    mimeData.setImageData(image);

    const auto result = ClipboardCapture::snapshot(mimeData);
    QVERIFY(result);
    QCOMPARE(result.capture.image.size(), image.size());
}

void ClipboardCaptureTest::classifiesLocalAndWebUrls()
{
    QMimeData localMime;
    const QUrl first = QUrl::fromLocalFile(QStringLiteral("C:/notes/one.txt"));
    const QUrl second = QUrl::fromLocalFile(QStringLiteral("C:/notes/two.txt"));
    localMime.setUrls({first, second});
    localMime.setText(QStringLiteral("these are file paths"));

    const auto local = ClipboardCapture::snapshot(localMime);
    QVERIFY(local);
    QCOMPARE(local.capture.kind, QStringLiteral("path"));
    QCOMPARE(local.capture.content,
             QDir::toNativeSeparators(first.toLocalFile()) + QLatin1Char('\n')
                 + QDir::toNativeSeparators(second.toLocalFile()));

    QMimeData webMime;
    webMime.setUrls({QUrl(QStringLiteral("https://example.test/a")),
                     QUrl(QStringLiteral("http://example.test/b"))});
    const auto web = ClipboardCapture::snapshot(webMime);
    QVERIFY(web);
    QCOMPARE(web.capture.kind, QStringLiteral("link"));
    QCOMPARE(web.capture.content,
             QStringLiteral("https://example.test/a\nhttp://example.test/b"));

    QMimeData tooManyUrls;
    QList<QUrl> urls;
    for (int i = 0; i < 257; ++i)
        urls.append(QUrl(QStringLiteral("https://example.test/%1").arg(i)));
    tooManyUrls.setUrls(urls);
    QCOMPARE(ClipboardCapture::snapshot(tooManyUrls).error, ClipboardCapture::Error::TooLarge);
}

void ClipboardCaptureTest::classifiesPlainTextUrlAsLink()
{
    QMimeData mimeData;
    mimeData.setText(QStringLiteral("https://example.test/path"));
    const auto result = ClipboardCapture::snapshot(mimeData);
    QVERIFY(result);
    QCOMPARE(result.capture.kind, QStringLiteral("link"));
    QCOMPARE(result.capture.content, QStringLiteral("https://example.test/path"));
}

void ClipboardCaptureTest::rejectsMixedUnsupportedAndEmptyData()
{
    QMimeData mixed;
    mixed.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/notes.txt")),
                   QUrl(QStringLiteral("https://example.test"))});
    QCOMPARE(ClipboardCapture::snapshot(mixed).error, ClipboardCapture::Error::Unsupported);

    QMimeData nonWebUrl;
    nonWebUrl.setUrls({QUrl(QStringLiteral("mailto:user@example.test"))});
    QCOMPARE(ClipboardCapture::snapshot(nonWebUrl).error, ClipboardCapture::Error::Unsupported);

    QMimeData unsupported;
    unsupported.setData(QStringLiteral("application/x-private-object"), QByteArray("payload"));
    QCOMPARE(ClipboardCapture::snapshot(unsupported).error, ClipboardCapture::Error::Unsupported);

    QMimeData empty;
    QCOMPARE(ClipboardCapture::snapshot(empty).error, ClipboardCapture::Error::Empty);
}

void ClipboardCaptureTest::honorsSensitiveClipboardMarkers()
{
    QMimeData excluded;
    excluded.setText(QStringLiteral("must not be stored"));
    excluded.setData(
        QStringLiteral("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\""),
        QByteArray(1, '\0'));
    QCOMPARE(ClipboardCapture::snapshot(excluded).error, ClipboardCapture::Error::Sensitive);

    QMimeData denied;
    denied.setText(QStringLiteral("must not be stored"));
    denied.setData(
        QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""),
        QByteArray(4, '\0'));
    QCOMPARE(ClipboardCapture::snapshot(denied).error, ClipboardCapture::Error::Sensitive);

    QMimeData malformed;
    malformed.setText(QStringLiteral("fail closed"));
    malformed.setData(
        QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""),
        QByteArray(2, '\1'));
    QCOMPARE(ClipboardCapture::snapshot(malformed).error, ClipboardCapture::Error::Sensitive);

    QMimeData allowed;
    allowed.setText(QStringLiteral("allowed"));
    QByteArray oneDword(4, '\0');
    oneDword[0] = 1;
    allowed.setData(
        QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""),
        oneDword);
    const auto result = ClipboardCapture::snapshot(allowed);
    QVERIFY(result);
    QCOMPARE(result.capture.content, QStringLiteral("allowed"));

    QMimeData kdeHint;
    kdeHint.setText(QStringLiteral("must not be stored"));
    kdeHint.setData(QStringLiteral("application/x-kde-passwordManagerHint"), QByteArray(1, '\1'));
    QCOMPARE(ClipboardCapture::snapshot(kdeHint).error, ClipboardCapture::Error::Sensitive);
}

void ClipboardCaptureTest::rejectsOversizedSnapshots()
{
    QMimeData largeText;
    largeText.setText(QString(ClipboardCapture::maxSnapshotBytes() / 2 + 1, QLatin1Char('x')));
    QCOMPARE(ClipboardCapture::snapshot(largeText).error, ClipboardCapture::Error::TooLarge);

    QMimeData largeImage;
    QImage image(4200, 4200, QImage::Format_ARGB32);
    QVERIFY(!image.isNull());
    largeImage.setImageData(image);
    QCOMPARE(ClipboardCapture::snapshot(largeImage).error, ClipboardCapture::Error::TooLarge);
}

QTEST_GUILESS_MAIN(ClipboardCaptureTest)
#include "tst_clipboard_capture.moc"
