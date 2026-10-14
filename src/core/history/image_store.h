#pragma once

#include <QImage>
#include <QString>

class QThread;

namespace ClipboardHistory::Core {

enum class ImageStoreError {
    None,
    WrongThread,
    InvalidImage,
    InvalidReference,
    DirectoryUnavailable,
    EncodeFailed,
    ReadFailed,
    WriteFailed,
    RemoveFailed,
    NotFound,
    HashConflict
};

struct ImageStoreStatus {
    ImageStoreError error = ImageStoreError::None;
    QString reference;

    explicit operator bool() const { return error == ImageStoreError::None; }
};

class ImageStore final {
public:
    explicit ImageStore(QString imagesDirectory);

    ImageStoreStatus store(const QImage &image);
    ImageStoreStatus load(const QString &reference, QImage &image) const;
    ImageStoreStatus remove(const QString &reference) const;

private:
    bool isValidReference(const QString &reference) const;

    QString m_imagesDirectory;
    QThread *m_ownerThread;
};

} // namespace ClipboardHistory::Core
