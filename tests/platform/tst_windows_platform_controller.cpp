#include "../../src/platform/windows/windows_platform_controller.h"

#include <QSignalSpy>
#include <QProcess>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using ClipboardHistory::Platform::Windows::WindowsPlatformController;

namespace {
constexpr wchar_t testRunRegistryKey[] = L"Software\\ClipboardHistory\\Testing\\Run";
}

class WindowsPlatformControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void forwardsSecondLaunchToPrimary();
    void rejectsInvalidHotkeyExplicitly();
    void emptyHotkeyClearsConfiguration();
    void doesNotPasteWithoutPreviousWindow();
    void registersAndRemovesAutoStart();
    void quitExitsProcessAndReleasesInstance_data();
    void quitExitsProcessAndReleasesInstance();
};

void WindowsPlatformControllerTest::initTestCase()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, testRunRegistryKey);
}

void WindowsPlatformControllerTest::cleanupTestCase()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, testRunRegistryKey);
}

void WindowsPlatformControllerTest::forwardsSecondLaunchToPrimary()
{
    const QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTemporaryDir lockDirectory;
    QVERIFY(lockDirectory.isValid());
    WindowsPlatformController primary;
    QSignalSpy activation(&primary, &WindowsPlatformController::activationRequested);

    QCOMPARE(primary.acquireSingleInstance(key, lockDirectory.path()), WindowsPlatformController::InstanceResult::Primary);
    QProcess secondary;
    secondary.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--forward"), key, lockDirectory.path()});
    QVERIFY(secondary.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(secondary.state(), QProcess::NotRunning, 3000);
    QCOMPARE(secondary.exitStatus(), QProcess::NormalExit);
    QCOMPARE(secondary.exitCode(), static_cast<int>(WindowsPlatformController::InstanceResult::Forwarded));
    QTRY_COMPARE(activation.count(), 1);
}

void WindowsPlatformControllerTest::rejectsInvalidHotkeyExplicitly()
{
    WindowsPlatformController controller;
    QSignalSpy failed(&controller, &WindowsPlatformController::hotkeyRegistrationFailed);

    QVERIFY(!controller.setGlobalHotkey(QStringLiteral("not a hotkey")));
    QCOMPARE(failed.count(), 1);
    QVERIFY(!controller.lastError().isEmpty());
}

void WindowsPlatformControllerTest::emptyHotkeyClearsConfiguration()
{
    WindowsPlatformController controller;
    QVERIFY(controller.setGlobalHotkey(QString()));
    QVERIFY(controller.globalHotkey().isEmpty());
}

void WindowsPlatformControllerTest::doesNotPasteWithoutPreviousWindow()
{
    WindowsPlatformController controller;
    QVERIFY(!controller.pasteToPreviousWindow());
}

void WindowsPlatformControllerTest::registersAndRemovesAutoStart()
{
    HKEY probeKey = nullptr;
    const LONG access = RegCreateKeyExW(HKEY_CURRENT_USER, testRunRegistryKey, 0, nullptr, 0,
                                        KEY_SET_VALUE, nullptr, &probeKey, nullptr);
    if (access == ERROR_ACCESS_DENIED)
        QSKIP("The current test sandbox cannot write HKCU; no real startup entry was touched.");
    QVERIFY2(access == ERROR_SUCCESS, qPrintable(QStringLiteral("Test registry setup failed (%1)").arg(access)));
    RegCloseKey(probeKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, testRunRegistryKey);

    WindowsPlatformController controller;
    QVERIFY2(controller.setAutoStartEnabled(true), qPrintable(controller.lastError()));

    HKEY runKey = nullptr;
    QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER, testRunRegistryKey, 0, KEY_QUERY_VALUE, &runKey), ERROR_SUCCESS);
    wchar_t command[2048]{};
    DWORD commandBytes = sizeof(command);
    DWORD type = 0;
    QCOMPARE(RegQueryValueExW(runKey, L"ClipboardHistory", nullptr, &type,
                              reinterpret_cast<BYTE *>(command), &commandBytes), ERROR_SUCCESS);
    QCOMPARE(type, static_cast<DWORD>(REG_SZ));
    const QString expected = QStringLiteral("\"%1\" --startup")
                                 .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    QCOMPARE(QString::fromWCharArray(command), expected);
    RegCloseKey(runKey);

    QVERIFY(controller.setAutoStartEnabled(false));
    QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER, testRunRegistryKey, 0, KEY_QUERY_VALUE, &runKey), ERROR_SUCCESS);
    QCOMPARE(RegQueryValueExW(runKey, L"ClipboardHistory", nullptr, &type,
                              reinterpret_cast<BYTE *>(command), &commandBytes), ERROR_FILE_NOT_FOUND);
    RegCloseKey(runKey);
}

void WindowsPlatformControllerTest::quitExitsProcessAndReleasesInstance_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("direct") << QStringLiteral("--quit-direct");
    QTest::newRow("tray-command") << QStringLiteral("--quit-tray");
}

void WindowsPlatformControllerTest::quitExitsProcessAndReleasesInstance()
{
    QFETCH(QString, mode);
    const QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTemporaryDir lockDirectory;
    QVERIFY(lockDirectory.isValid());
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {mode, key, lockDirectory.path()});
    QVERIFY(child.waitForStarted(3000));
    QVERIFY(child.waitForFinished(3000));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);

    WindowsPlatformController reopened;
    QCOMPARE(reopened.acquireSingleInstance(key, lockDirectory.path()),
             WindowsPlatformController::InstanceResult::Primary);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--forward")) {
        WindowsPlatformController controller;
        return static_cast<int>(controller.acquireSingleInstance(arguments.at(2), arguments.at(3)));
    }
    if (arguments.size() == 4 && (arguments.at(1) == QStringLiteral("--quit-direct")
                                  || arguments.at(1) == QStringLiteral("--quit-tray"))) {
        WindowsPlatformController controller;
        if (controller.acquireSingleInstance(arguments.at(2), arguments.at(3))
            != WindowsPlatformController::InstanceResult::Primary)
            return 1;
        QTimer::singleShot(0, &controller, [&controller, mode = arguments.at(1)] {
            if (mode == QStringLiteral("--quit-direct"))
                controller.quit();
            else
                controller.processTrayMessage(WM_COMMAND, 103);
        });
        return app.exec();
    }
    WindowsPlatformControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_windows_platform_controller.moc"
