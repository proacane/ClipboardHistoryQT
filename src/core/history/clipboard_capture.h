#pragma once

#include "history_task_queue.h"

#include <QObject>
#include <QString>

class QClipboard;
class QMimeData;

namespace ClipboardHistory::Core {

class ClipboardCapture final : public QObject {
    Q_OBJECT
public:
    enum class Error { None, Empty, Unsupported, Sensitive, TooLarge, InvalidData };
    Q_ENUM(Error)

    struct Snapshot {
        HistoryCapture capture;
        Error error = Error::None;

        explicit operator bool() const { return error == Error::None; }
    };

    ClipboardCapture(QClipboard *clipboard, HistoryTaskQueue *queue, QObject *parent = nullptr);

    static Snapshot snapshot(const QMimeData &mimeData);
    static constexpr qsizetype maxSnapshotBytes() { return 64 * 1024 * 1024; }

signals:
    void captureRejected(ClipboardHistory::Core::ClipboardCapture::Error error);
    void submissionRejected(ClipboardHistory::Core::HistoryTaskQueue::SubmitResult result);
    void recordFailed(const ClipboardHistory::Core::HistoryRecordResult &result);
    void initializationFailed(const ClipboardHistory::Core::HistoryServiceStatus &status);

private slots:
    void onClipboardChanged();
    void onQueueInitialized(const ClipboardHistory::Core::HistoryServiceStatus &status);
    void onRecordCompleted(const ClipboardHistory::Core::HistoryRecordResult &result);

private:
    void captureCurrentClipboard();

    QClipboard *m_clipboard = nullptr;
    HistoryTaskQueue *m_queue = nullptr;
    bool m_queueReady = false;
};

} // namespace ClipboardHistory::Core
