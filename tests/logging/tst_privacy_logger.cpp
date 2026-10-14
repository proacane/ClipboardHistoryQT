#include "../../src/core/logging/PrivacyLogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

using ClipboardHistory::Core::PrivacyLogger;

namespace {

constexpr auto ChildMode = "--privacy-logger-child";
constexpr quint32 EventCode = 731;
constexpr quint32 DurationCode = 732;
constexpr quint32 CountCode = 733;
constexpr qint32 ErrorCode = -734;

int runLoggerChild()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ClipboardHistoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PrivacyLoggerTest"));
    PrivacyLogger logger;
    logger.recordEvent(EventCode);
    logger.recordDuration(DurationCode, 41);
    logger.recordCount(CountCode, 9);
    logger.recordError(ErrorCode);
    return 0;
}

class PrivacyLoggerTest final : public QObject
{
    Q_OBJECT

private slots:
    void writesNumericRecordsAndDrainsQueueOnDestruction();
};

void PrivacyLoggerTest::writesNumericRecordsAndDrainsQueueOnDestruction()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourceExecutable = QCoreApplication::applicationFilePath();
    const QString childExecutable = tempDir.filePath(QFileInfo(sourceExecutable).fileName());
    QVERIFY2(QFile::copy(sourceExecutable, childExecutable), "Could not copy the test executable into its isolated directory");

    const QByteArray canary = QByteArrayLiteral("CLIPBOARD_PRIVATE_CANARY_7A4E1F");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString qtBinaries = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    if (!qtBinaries.isEmpty()) {
        environment.insert(QStringLiteral("PATH"),
                           qtBinaries + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    }

    QProcess child;
    child.setProcessEnvironment(environment);
    child.setWorkingDirectory(tempDir.path());
    child.setProgram(childExecutable);
    child.setArguments({QString::fromLatin1(ChildMode), QString::fromLatin1(canary)});
    child.start();
    QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
    if (!child.waitForFinished(10000)) {
        child.kill();
        child.waitForFinished();
        QFAIL("The isolated logger process did not finish within 10 seconds");
    }
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);

    const QString logPath = tempDir.filePath(QStringLiteral("data/logs/events.log"));
    QFile logFile(logPath);
    QVERIFY2(logFile.open(QIODevice::ReadOnly), qPrintable(logFile.errorString()));
    const QByteArray log = logFile.readAll().replace("\r\n", "\n");
    QVERIFY(!log.contains(canary));
    QVERIFY(!log.contains(tempDir.path().toUtf8()));

    QList<QByteArray> lines = log.split('\n');
    QVERIFY(lines.size() > 1);
    QCOMPARE(lines.takeLast(), QByteArray());
    QCOMPARE(lines.size(), 4);

    const QList<QList<QByteArray>> expectedFields = {
        {QByteArrayLiteral("E"), QByteArray::number(EventCode)},
        {QByteArrayLiteral("D"), QByteArray::number(DurationCode), QByteArrayLiteral("41")},
        {QByteArrayLiteral("C"), QByteArray::number(CountCode), QByteArrayLiteral("9")},
        {QByteArrayLiteral("X"), QByteArray::number(ErrorCode)}
    };
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QList<QByteArray> fields = lines.at(index).split('\t');
        QCOMPARE(fields.size(), expectedFields.at(index).size() + 1);
        QVERIFY(!fields.constFirst().isEmpty());
        QCOMPARE(fields.mid(1), expectedFields.at(index));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() > 1 && app.arguments().at(1) == QString::fromLatin1(ChildMode))
        return runLoggerChild();

    PrivacyLoggerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_privacy_logger.moc"
