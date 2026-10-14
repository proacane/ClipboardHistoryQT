#include "clipboard_capture.h"

#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

namespace ClipboardHistory::Core {
namespace {

bool isSensitive(const QMimeData &mimeData)
{
    for (const QString &format : mimeData.formats()) {
        const QString lowerFormat = format.toLower();
        if (lowerFormat.contains(QStringLiteral("excludeclipboardcontentfrommonitorprocessing"))
            || lowerFormat.contains(QStringLiteral("x-kde-passwordmanagerhint"))) {
            return true;
        }

        if (lowerFormat.contains(QStringLiteral("canincludeinclipboardhistory"))) {
            const QByteArray value = mimeData.data(format);
            if (value.size() != qsizetype(sizeof(quint32))
                || qFromLittleEndian<quint32>(
                       reinterpret_cast<const uchar *>(value.constData())) != 1) {
                return true;
            }
        }
    }
    return false;
}

bool isWebUrl(const QUrl &url)
{
    return url.isValid()
        && (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
            || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
}

bool fitsSnapshotBudget(const QString &text)
{
    return text.size() <= ClipboardCapture::maxSnapshotBytes() / qsizetype(sizeof(QChar));
}

ClipboardCapture::Snapshot failure(ClipboardCapture::Error error)
{
    return {{}, error};
}

} // namespace

ClipboardCapture::ClipboardCapture(QClipboard *clipboard, HistoryTaskQueue *queue,
                                   QObject *parent)
    : QObject(parent),
      m_clipboard(clipboard),
      m_queue(queue)
{
    Q_ASSERT(m_clipboard);
    Q_ASSERT(m_queue);

    connect(m_clipboard, &QClipboard::dataChanged,
            this, &ClipboardCapture::onClipboardChanged);
    connect(m_queue, &HistoryTaskQueue::initialized,
            this, &ClipboardCapture::onQueueInitialized);
    connect(m_queue, &HistoryTaskQueue::recordCompleted,
            this, &ClipboardCapture::onRecordCompleted);
}

ClipboardCapture::Snapshot ClipboardCapture::snapshot(const QMimeData &mimeData)
{
    if (isSensitive(mimeData))
        return failure(Error::Sensitive);

    if (mimeData.hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData.imageData());
        if (image.isNull())
            return failure(Error::InvalidData);
        if (image.sizeInBytes() > maxSnapshotBytes())
            return failure(Error::TooLarge);
        return {{QStringLiteral("image"), {}, image}, Error::None};
    }

    if (mimeData.hasUrls()) {
        const QList<QUrl> urls = mimeData.urls();
        if (urls.isEmpty())
            return failure(Error::InvalidData);
        if (urls.size() > 256)
            return failure(Error::TooLarge);

        const bool allLocal = std::all_of(urls.cbegin(), urls.cend(),
                                          [](const QUrl &url) { return url.isLocalFile(); });
        const bool allWeb = std::all_of(urls.cbegin(), urls.cend(), isWebUrl);
        if (!allLocal && !allWeb)
            return failure(Error::Unsupported);

        QStringList entries;
        entries.reserve(urls.size());
        qsizetype totalUnits = 0;
        const qsizetype maxUnits = maxSnapshotBytes() / qsizetype(sizeof(QChar));
        for (const QUrl &url : urls) {
            const QString entry = allLocal ? QDir::toNativeSeparators(url.toLocalFile())
                                           : url.toString(QUrl::FullyEncoded);
            const qsizetype unitsWithSeparator = entry.size() + (entries.isEmpty() ? 0 : 1);
            if (unitsWithSeparator > maxUnits - totalUnits)
                return failure(Error::TooLarge);
            totalUnits += unitsWithSeparator;
            entries.append(entry);
        }

        const QString content = entries.join(QLatin1Char('\n'));
        return {{allLocal ? QStringLiteral("path") : QStringLiteral("link"), content, {}},
                Error::None};
    }

    if (mimeData.hasText()) {
        const QString content = mimeData.text();
        if (content.isEmpty())
            return failure(Error::Empty);
        if (!fitsSnapshotBudget(content))
            return failure(Error::TooLarge);

        const QUrl url(content.trimmed());
        if (content == content.trimmed() && isWebUrl(url))
            return {{QStringLiteral("link"), url.toString(QUrl::FullyEncoded), {}}, Error::None};
        return {{QStringLiteral("text"), content, {}}, Error::None};
    }

    return failure(mimeData.formats().isEmpty() ? Error::Empty : Error::Unsupported);
}

void ClipboardCapture::onClipboardChanged()
{
    captureCurrentClipboard();
}

void ClipboardCapture::onQueueInitialized(const HistoryServiceStatus &status)
{
    m_queueReady = static_cast<bool>(status);
    if (!status)
        emit initializationFailed(status);
    else
        captureCurrentClipboard();
}

void ClipboardCapture::onRecordCompleted(const HistoryRecordResult &result)
{
    if (!result.status)
        emit recordFailed(result);
}

void ClipboardCapture::captureCurrentClipboard()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_queueReady || m_clipboard->ownsClipboard())
        return;

    const QMimeData *mimeData = m_clipboard->mimeData(QClipboard::Clipboard);
    if (!mimeData)
        return;

    const Snapshot result = snapshot(*mimeData);
    if (!result) {
        if (result.error != Error::Empty)
            emit captureRejected(result.error);
        return;
    }

    const HistoryTaskQueue::SubmitResult submission = m_queue->submit(result.capture);
    if (submission != HistoryTaskQueue::SubmitResult::Accepted)
        emit submissionRejected(submission);
}

} // namespace ClipboardHistory::Core
