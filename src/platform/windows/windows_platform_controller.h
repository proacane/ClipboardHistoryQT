#pragma once

#include <QObject>
#include <QString>

#include <QLockFile>
#include <QLocalServer>

#include <memory>

class QLocalSocket;
class QWindow;

namespace ClipboardHistory::Platform::Windows {

class WindowsPlatformController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString globalHotkey READ globalHotkey NOTIFY globalHotkeyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(bool capturePaused READ capturePaused WRITE setCapturePaused NOTIFY capturePausedChanged)

public:
    enum class InstanceResult { Primary, Forwarded, Error };
    Q_ENUM(InstanceResult)

    explicit WindowsPlatformController(QObject *parent = nullptr);
    ~WindowsPlatformController() override;

    InstanceResult acquireSingleInstance(const QString &instanceKey, const QString &lockDirectory);
    bool setWindow(QWindow *window);
    QString globalHotkey() const { return m_globalHotkey; }
    QString lastError() const { return m_lastError; }
    bool trayAvailable() const { return m_trayAvailable; }
    bool capturePaused() const { return m_capturePaused; }
    void setCapturePaused(bool paused);

    Q_INVOKABLE bool setGlobalHotkey(const QString &sequence);
    Q_INVOKABLE bool setAutoStartEnabled(bool enabled);
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE bool pasteToPreviousWindow();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void toggleWindow();
    Q_INVOKABLE void quit();
    Q_INVOKABLE bool openExternalLink(const QString &url);
    Q_INVOKABLE bool revealFileInExplorer(const QString &filePath);
    Q_INVOKABLE bool copyPlainText(const QString &text);
    void processTrayMessage(quintptr message, quintptr data);
    void rememberForegroundWindow();

signals:
    void activationRequested();
    void globalHotkeyActivated();
    void globalHotkeyChanged();
    void lastErrorChanged();
    void trayAvailableChanged();
    void capturePausedChanged();
    void pauseToggleRequested();
    void settingsRequested();
    void hotkeyRegistrationFailed(const QString &error);

private slots:
    void acceptConnections();

private:
    bool createTrayWindow();
    bool forwardActivationRequest();
    void setError(const QString &error);
    void setTrayAvailable(bool available);
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_instanceLock;
    void *m_instanceMutex = nullptr;
    QString m_serverName;
    QString m_globalHotkey;
    QString m_lastError;
    QWindow *m_window = nullptr;
    void *m_trayWindow = nullptr;
    void *m_previousWindow = nullptr;
    unsigned int m_hotkeyId = 0;
    bool m_trayAvailable = false;
    bool m_capturePaused = false;
};

} // namespace ClipboardHistory::Platform::Windows
