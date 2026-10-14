#include "windows_platform_controller.h"
#include "app_icon.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <string>

namespace ClipboardHistory::Platform::Windows {
namespace {

constexpr UINT trayCallbackMessage = WM_APP + 17;
constexpr UINT trayShowCommand = 101;
constexpr UINT trayQuitCommand = 103;
constexpr UINT hotkeyIdFirst = 0xC101;
constexpr UINT hotkeyIdSecond = 0xC102;
constexpr wchar_t trayWindowClassName[] = L"ClipboardHistory.TrayWindow";
#ifdef CLIPBOARDHISTORY_TESTING
constexpr wchar_t autoStartRegistryKey[] = L"Software\\ClipboardHistory\\Testing\\Run";
#else
constexpr wchar_t autoStartRegistryKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
constexpr wchar_t autoStartValueName[] = L"ClipboardHistory";

bool mapVirtualKey(Qt::Key key, UINT *virtualKey)
{
    const int value = static_cast<int>(key);
    if ((value >= static_cast<int>(Qt::Key_A) && value <= static_cast<int>(Qt::Key_Z))
        || (value >= static_cast<int>(Qt::Key_0) && value <= static_cast<int>(Qt::Key_9))) {
        *virtualKey = static_cast<UINT>(value);
        return true;
    }
    if (value >= static_cast<int>(Qt::Key_F1) && value <= static_cast<int>(Qt::Key_F24)) {
        *virtualKey = VK_F1 + static_cast<UINT>(value - static_cast<int>(Qt::Key_F1));
        return true;
    }

    switch (key) {
    case Qt::Key_Space: *virtualKey = VK_SPACE; return true;
    case Qt::Key_Tab: *virtualKey = VK_TAB; return true;
    case Qt::Key_Return: *virtualKey = VK_RETURN; return true;
    case Qt::Key_Enter: *virtualKey = VK_RETURN; return true;
    case Qt::Key_Escape: *virtualKey = VK_ESCAPE; return true;
    case Qt::Key_Insert: *virtualKey = VK_INSERT; return true;
    case Qt::Key_Delete: *virtualKey = VK_DELETE; return true;
    case Qt::Key_Home: *virtualKey = VK_HOME; return true;
    case Qt::Key_End: *virtualKey = VK_END; return true;
    case Qt::Key_PageUp: *virtualKey = VK_PRIOR; return true;
    case Qt::Key_PageDown: *virtualKey = VK_NEXT; return true;
    case Qt::Key_Left: *virtualKey = VK_LEFT; return true;
    case Qt::Key_Up: *virtualKey = VK_UP; return true;
    case Qt::Key_Right: *virtualKey = VK_RIGHT; return true;
    case Qt::Key_Down: *virtualKey = VK_DOWN; return true;
    default: return false;
    }
}

bool parseHotkey(const QString &text, UINT *modifiers, UINT *virtualKey)
{
    if (text.trimmed().isEmpty())
        return true;

    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1)
        return false;

    const QKeyCombination combination = sequence[0];
    const auto keyModifiers = combination.keyboardModifiers();
    if (!(keyModifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier)))
        return false;

    UINT nativeModifiers = MOD_NOREPEAT;
    if (keyModifiers & Qt::ControlModifier) nativeModifiers |= MOD_CONTROL;
    if (keyModifiers & Qt::AltModifier) nativeModifiers |= MOD_ALT;
    if (keyModifiers & Qt::ShiftModifier) nativeModifiers |= MOD_SHIFT;
    if (keyModifiers & Qt::MetaModifier) nativeModifiers |= MOD_WIN;
    if (!mapVirtualKey(combination.key(), virtualKey))
        return false;

    *modifiers = nativeModifiers;
    return true;
}

LRESULT CALLBACK trayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *controller = reinterpret_cast<WindowsPlatformController *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        controller = static_cast<WindowsPlatformController *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
    }
    if (controller) {
        if (message == WM_HOTKEY) {
            controller->processTrayMessage(message, static_cast<quintptr>(wParam));
            return 0;
        }
        if (message == trayCallbackMessage) {
            controller->processTrayMessage(message, static_cast<quintptr>(lParam));
            return 0;
        }
        if (message == WM_COMMAND) {
            controller->processTrayMessage(message, LOWORD(wParam));
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

WindowsPlatformController::WindowsPlatformController(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection,
            this, &WindowsPlatformController::acceptConnections);
}

WindowsPlatformController::~WindowsPlatformController()
{
    if (m_trayWindow) {
        const HWND hwnd = static_cast<HWND>(m_trayWindow);
        if (m_hotkeyId)
            UnregisterHotKey(hwnd, m_hotkeyId);
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = hwnd;
        icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        DestroyWindow(hwnd);
    }
    if (m_server.isListening()) {
        m_server.close();
        QLocalServer::removeServer(m_serverName);
    }
    if (m_instanceMutex)
        CloseHandle(static_cast<HANDLE>(m_instanceMutex));
}

WindowsPlatformController::InstanceResult
WindowsPlatformController::acquireSingleInstance(const QString &instanceKey, const QString &lockDirectory)
{
    if (instanceKey.trimmed().isEmpty() || lockDirectory.trimmed().isEmpty()) {
        setError(QStringLiteral("单实例标识不能为空"));
        return InstanceResult::Error;
    }
    const QString keyHash = QString::fromLatin1(QCryptographicHash::hash(
        instanceKey.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_serverName = QStringLiteral("ClipboardHistory-%1").arg(keyHash);
    const QString lockName = QStringLiteral("ClipboardHistory-%1.lock").arg(keyHash);
    const std::wstring mutexName = (QStringLiteral("Local\\ClipboardHistory-%1").arg(keyHash)).toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex) {
        setError(QStringLiteral("无法创建单实例互斥对象（Windows 错误码 %1）").arg(GetLastError()));
        return InstanceResult::Error;
    }
    m_instanceMutex = mutex;
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    const QString canonicalLockDirectory = QFileInfo(lockDirectory).canonicalFilePath();
    if (canonicalLockDirectory.isEmpty()) {
        setError(QStringLiteral("单实例锁目录不可用"));
        return InstanceResult::Error;
    }
    if (alreadyRunning) {
        if (forwardActivationRequest())
            return InstanceResult::Forwarded;
        setError(QStringLiteral("已有应用实例，但无法转发激活请求"));
        return InstanceResult::Error;
    }

    m_instanceLock = std::make_unique<QLockFile>(QDir(canonicalLockDirectory).filePath(lockName));
    if (m_instanceLock->tryLock(0)) {
        QLocalServer::removeServer(m_serverName);
        if (m_server.listen(m_serverName))
            return InstanceResult::Primary;
        setError(QStringLiteral("无法建立单实例服务：%1").arg(m_server.errorString()));
        m_instanceLock.reset();
        CloseHandle(mutex);
        m_instanceMutex = nullptr;
        return InstanceResult::Error;
    }
    setError(QStringLiteral("已有实例占用数据目录，无法建立单实例服务"));
    CloseHandle(mutex);
    m_instanceMutex = nullptr;
    return InstanceResult::Error;
}

bool WindowsPlatformController::forwardActivationRequest()
{
    QLocalSocket socket;
    for (int attempt = 0; attempt < 10 && socket.state() == QLocalSocket::UnconnectedState; ++attempt) {
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(100))
            break;
        socket.abort();
        QThread::msleep(50);
    }
    if (socket.state() == QLocalSocket::ConnectedState) {
        socket.write("show\n");
        const bool forwarded = socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        if (forwarded)
            return true;
    }
    return false;
}

bool WindowsPlatformController::setWindow(QWindow *window)
{
    m_window = window;
    if (!window) {
        setError(QStringLiteral("托盘初始化失败：主窗口为空"));
        return false;
    }
    connect(window, &QWindow::activeChanged, this, [this] {
        if (m_window && !m_window->isActive())
            rememberForegroundWindow();
    });
    return createTrayWindow();
}

bool WindowsPlatformController::createTrayWindow()
{
    if (m_trayWindow)
        return true;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = trayWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = trayWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        setError(QStringLiteral("托盘窗口初始化失败（Windows 错误码 %1）").arg(GetLastError()));
        setTrayAvailable(false);
        return false;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, trayWindowClassName, L"Clipboard History",
                                WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                GetModuleHandleW(nullptr), this);
    if (!hwnd) {
        setError(QStringLiteral("托盘窗口创建失败（Windows 错误码 %1）").arg(GetLastError()));
        setTrayAvailable(false);
        return false;
    }

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = hwnd;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = trayCallbackMessage;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_CLIPBOARD_HISTORY));
    wcscpy_s(icon.szTip, L"剪切板历史");
    if (!Shell_NotifyIconW(NIM_ADD, &icon)) {
        const DWORD error = GetLastError();
        DestroyWindow(hwnd);
        setError(QStringLiteral("系统托盘不可用（Windows 错误码 %1）").arg(error));
        setTrayAvailable(false);
        return false;
    }
    m_trayWindow = hwnd;
    setTrayAvailable(true);
    return true;
}

bool WindowsPlatformController::setGlobalHotkey(const QString &sequence)
{
    UINT modifiers = 0;
    UINT virtualKey = 0;
    if (!parseHotkey(sequence, &modifiers, &virtualKey)) {
        const QString error = QStringLiteral("全局热键格式无效；请使用 Ctrl+Shift+V 形式的单键组合");
        setError(error);
        emit hotkeyRegistrationFailed(error);
        return false;
    }
    if (sequence.trimmed().isEmpty()) {
        if (m_hotkeyId && m_trayWindow)
            UnregisterHotKey(static_cast<HWND>(m_trayWindow), m_hotkeyId);
        m_hotkeyId = 0;
        m_globalHotkey.clear();
        emit globalHotkeyChanged();
        setError({});
        return true;
    }
    if (!m_trayWindow) {
        const QString error = QStringLiteral("全局热键注册失败：Windows 平台窗口尚未初始化");
        setError(error);
        emit hotkeyRegistrationFailed(error);
        return false;
    }

    const UINT nextId = m_hotkeyId == hotkeyIdFirst ? hotkeyIdSecond : hotkeyIdFirst;
    if (!RegisterHotKey(static_cast<HWND>(m_trayWindow), nextId, modifiers, virtualKey)) {
        const DWORD code = GetLastError();
        wchar_t systemMessage[512]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, systemMessage, 512, nullptr);
        const QString reason = code == ERROR_HOTKEY_ALREADY_REGISTERED
            ? QStringLiteral("该组合键已被其他程序或系统功能占用，请选择其他组合键。")
            : QString::fromWCharArray(systemMessage).trimmed();
        const QString error = QStringLiteral("无法注册快捷键“%1”：%2（Windows 错误码 %3）")
            .arg(sequence.trimmed(), reason.isEmpty() ? QStringLiteral("Windows 未提供具体原因。") : reason)
            .arg(code);
        setError(error);
        emit hotkeyRegistrationFailed(error);
        return false;
    }

    if (m_hotkeyId)
        UnregisterHotKey(static_cast<HWND>(m_trayWindow), m_hotkeyId);
    m_hotkeyId = nextId;
    m_globalHotkey = sequence.trimmed();
    emit globalHotkeyChanged();
    setError({});
    return true;
}

bool WindowsPlatformController::setAutoStartEnabled(bool enabled)
{
    HKEY runKey = nullptr;
    LONG status = enabled
        ? RegCreateKeyExW(HKEY_CURRENT_USER, autoStartRegistryKey, 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &runKey, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, autoStartRegistryKey, 0, KEY_SET_VALUE, &runKey);
    if (!enabled && status == ERROR_FILE_NOT_FOUND) {
        setError({});
        return true;
    }
    if (status != ERROR_SUCCESS) {
        setError(QStringLiteral("开机启动设置失败（Windows 错误码 %1）").arg(status));
        return false;
    }

    if (enabled) {
        const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const std::wstring command = QStringLiteral("\"%1\" --startup").arg(executable).toStdWString();
        status = RegSetValueExW(runKey, autoStartValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(runKey, autoStartValueName);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }
    RegCloseKey(runKey);

    if (status != ERROR_SUCCESS) {
        setError(QStringLiteral("开机启动设置失败（Windows 错误码 %1）").arg(status));
        return false;
    }
    setError({});
    return true;
}

void WindowsPlatformController::showWindow()
{
    rememberForegroundWindow();
    emit activationRequested();
    if (m_window) {
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }
}

void WindowsPlatformController::rememberForegroundWindow()
{
    const HWND foreground = GetForegroundWindow();
    const HWND appWindow = m_window ? reinterpret_cast<HWND>(m_window->winId()) : nullptr;
    if (foreground && foreground != appWindow && foreground != static_cast<HWND>(m_trayWindow))
        m_previousWindow = foreground;
}

bool WindowsPlatformController::pasteToPreviousWindow()
{
    const HWND target = static_cast<HWND>(m_previousWindow);
    if (!target || !IsWindow(target) || !m_window)
        return false;

    if (!SetForegroundWindow(target) && GetForegroundWindow() != target) {
        setError(QStringLiteral("无法恢复之前的窗口，内容仍已复制到剪贴板。"));
        return false;
    }
    m_window->hide();
    QTimer::singleShot(100, this, [this, target] {
        const auto restoreWindow = [this] {
            if (!m_window)
                return;
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        };
        if (!IsWindow(target) || GetForegroundWindow() != target) {
            setError(QStringLiteral("无法恢复之前的窗口，内容仍已复制到剪贴板。"));
            restoreWindow();
            return;
        }
        INPUT input[4]{};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_CONTROL;
        input[1].type = INPUT_KEYBOARD;
        input[1].ki.wVk = 'V';
        input[2] = input[1];
        input[2].ki.dwFlags = KEYEVENTF_KEYUP;
        input[3] = input[0];
        input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        if (SendInput(4, input, sizeof(INPUT)) != 4) {
            setError(QStringLiteral("自动粘贴失败，内容仍已复制到剪贴板。"));
            restoreWindow();
        } else {
            setError({});
        }
    });
    return true;
}

void WindowsPlatformController::hideToTray()
{
    if (m_window)
        m_window->hide();
}

void WindowsPlatformController::toggleWindow()
{
    if (m_window && m_window->isVisible())
        hideToTray();
    else
        showWindow();
}

void WindowsPlatformController::quit()
{
    QCoreApplication::exit(0);
}

void WindowsPlatformController::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll().trimmed();
            if (request == "show")
                showWindow();
            socket->disconnectFromServer();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void WindowsPlatformController::processTrayMessage(quintptr message, quintptr data)
{
    if (message == WM_HOTKEY) {
        if (data == m_hotkeyId) {
            emit globalHotkeyActivated();
            toggleWindow();
        }
        return;
    }

    if (message == trayCallbackMessage) {
        const UINT event = LOWORD(data);
        if (event == WM_LBUTTONDBLCLK)
            toggleWindow();
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            HMENU menu = CreatePopupMenu();
            if (!menu)
                return;
            AppendMenuW(menu, MF_STRING, trayShowCommand, L"显示窗口");
            AppendMenuW(menu, MF_STRING, trayQuitCommand, L"退出");
            POINT point{};
            GetCursorPos(&point);
            rememberForegroundWindow();
            SetForegroundWindow(static_cast<HWND>(m_trayWindow));
            TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0,
                           static_cast<HWND>(m_trayWindow), nullptr);
            DestroyMenu(menu);
        }
        return;
    }

    if (message == WM_COMMAND) {
        switch (static_cast<UINT>(data)) {
        case trayShowCommand: showWindow(); break;
        case trayQuitCommand: quit(); break;
        default: break;
        }
    }
}

void WindowsPlatformController::setError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void WindowsPlatformController::setTrayAvailable(bool available)
{
    if (m_trayAvailable == available)
        return;
    m_trayAvailable = available;
    emit trayAvailableChanged();
}

} // namespace ClipboardHistory::Platform::Windows
