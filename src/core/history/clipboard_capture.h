#pragma once

#include "history_task_queue.h"

#include <QObject>
#include <QString>

class QClipboard;
class QMimeData;

namespace ClipboardHistory::Core {

class ClipboardCapture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
public:
    enum class Error { None, Empty, Unsupported, Sensitive, TooLarge, InvalidData };
    Q_ENUM(Error)

    struct Snapshot {
        HistoryCapture capture;
        Error error = Error::None;

        explicit operator bool() const { return error == Error::None; }
    };

    ClipboardCapture(QClipboard *clipboard, HistoryTaskQueue *queue, QObject *parent = nullptr);

    bool paused() const { return m_paused; }
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void togglePaused();

    static Snapshot snapshot(const QMimeData &mimeData);
    static constexpr qsizetype maxSnapshotBytes() { return 64 * 1024 * 1024; }

signals:
    void pausedChanged();
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
    bool m_paused = false;
};

} // namespace ClipboardHistory::Core
