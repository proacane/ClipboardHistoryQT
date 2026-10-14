#include "image_store.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QThread>

#include <utility>

namespace ClipboardHistory::Core {
namespace {

ImageStoreStatus failure(ImageStoreError error)
{
    return {error, {}};
}

} // namespace

ImageStore::ImageStore(QString imagesDirectory)
    : m_imagesDirectory(std::move(imagesDirectory)),
      m_ownerThread(QThread::currentThread())
{
}

ImageStoreStatus ImageStore::store(const QImage &image)
{
    if (QThread::currentThread() != m_ownerThread)
        return failure(ImageStoreError::WrongThread);
    if (image.isNull())
        return failure(ImageStoreError::InvalidImage);
    if (!QDir().mkpath(m_imagesDirectory))
        return failure(ImageStoreError::DirectoryUnavailable);

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly))
        return failure(ImageStoreError::EncodeFailed);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(image))
        return failure(ImageStoreError::EncodeFailed);

    const QByteArray digest = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256);
    const QString reference = QString::fromLatin1(digest.toHex());
    const QString filePath = QDir(m_imagesDirectory).filePath(reference + QStringLiteral(".png"));
    QFile existing(filePath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly))
            return failure(ImageStoreError::ReadFailed);
        QCryptographicHash existingDigest(QCryptographicHash::Sha256);
        if (!existingDigest.addData(&existing))
            return failure(ImageStoreError::ReadFailed);
        return existingDigest.result() == digest
            ? ImageStoreStatus{ImageStoreError::None, reference}
            : failure(ImageStoreError::HashConflict);
    }

    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly))
        return failure(ImageStoreError::WriteFailed);
    qsizetype offset = 0;
    while (offset < encoded.size()) {
        const qint64 written = output.write(encoded.constData() + offset, encoded.size() - offset);
        if (written <= 0) {
            output.cancelWriting();
            return failure(ImageStoreError::WriteFailed);
        }
        offset += written;
    }
    if (!output.commit())
        return failure(ImageStoreError::WriteFailed);
    return {ImageStoreError::None, reference};
}

ImageStoreStatus ImageStore::load(const QString &reference, QImage &image) const
{
    if (QThread::currentThread() != m_ownerThread)
        return failure(ImageStoreError::WrongThread);
    if (!isValidReference(reference))
        return failure(ImageStoreError::InvalidReference);

    const QString filePath = QDir(m_imagesDirectory).filePath(reference + QStringLiteral(".png"));
    if (!QFileInfo::exists(filePath))
        return failure(ImageStoreError::NotFound);
    QImage loaded;
    if (!loaded.load(filePath, "PNG"))
        return failure(ImageStoreError::ReadFailed);
    image = std::move(loaded);
    return {ImageStoreError::None, reference};
}

ImageStoreStatus ImageStore::remove(const QString &reference) const
{
    if (QThread::currentThread() != m_ownerThread)
        return failure(ImageStoreError::WrongThread);
    if (!isValidReference(reference))
        return failure(ImageStoreError::InvalidReference);

    const QString filePath = QDir(m_imagesDirectory).filePath(reference + QStringLiteral(".png"));
    QFile file(filePath);
    if (!file.exists())
        return failure(ImageStoreError::NotFound);
    return file.remove()
        ? ImageStoreStatus{ImageStoreError::None, reference}
        : failure(ImageStoreError::RemoveFailed);
}

bool ImageStore::isValidReference(const QString &reference) const
{
    if (reference.size() != 64)
        return false;
    for (const QChar character : reference) {
        const bool isDecimal = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool isLowerHex = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        if (!isDecimal && !isLowerHex)
            return false;
    }
    return true;
}

} // namespace ClipboardHistory::Core
