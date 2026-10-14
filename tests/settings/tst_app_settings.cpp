#include "../../src/core/settings/app_paths.h"
#include "../../src/core/settings/app_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

using ClipboardHistory::Core::AppPaths;
using ClipboardHistory::Core::AppSettings;

namespace {
QString testRoot() { return QDir::cleanPath(qEnvironmentVariable("CLIPBOARD_HISTORY_SETTINGS_TEST_ROOT")); }
QString bootstrapPath() { return QDir(testRoot()).filePath(QStringLiteral("ClipboardHistory.ini")); }
QString dataPath() { return QDir(testRoot()).filePath(QStringLiteral("data")); }

bool removeTree(const QString &path)
{
    return !QFileInfo::exists(path) || QDir(path).removeRecursively();
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
} // namespace

class AppSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void settingsUseDefaultsAndUpgradeVersionZero();
    void themesRoundTripAcrossInstances();
    void invalidThemeFallsBackToDark();
    void refusesInvalidAndFutureSchemaVersions();
    void pathsStayUnderIsolatedApplicationDirectory();
    void migratesDataIntoAnEmptyDirectory();
    void keepsSourceWhenPendingDestinationChangesBeforeRestart();
    void rejectsInvalidAndNonEmptyDestinations();
    void importedThemesUseProgramDirectoryAndNeverOverwrite();
    void importedThemesRejectInvalidFilesAndReportReadErrors();
};

void AppSettingsTest::initTestCase()
{
    QVERIFY2(!testRoot().isEmpty(), "The test runner must set its isolated root.");
    QCOMPARE(QDir::cleanPath(QCoreApplication::applicationDirPath()), testRoot());
}

void AppSettingsTest::init()
{
    QVERIFY(QFile::remove(bootstrapPath()) || !QFileInfo::exists(bootstrapPath()));
    QVERIFY(removeTree(dataPath()));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("themes"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("migrated"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("occupied"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("blocked-migration"))));
}

void AppSettingsTest::cleanup()
{
    QVERIFY(QFile::remove(bootstrapPath()) || !QFileInfo::exists(bootstrapPath()));
    QVERIFY(removeTree(dataPath()));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("themes"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("migrated"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("occupied"))));
    QVERIFY(removeTree(QDir(testRoot()).filePath(QStringLiteral("blocked-migration"))));
}

void AppSettingsTest::importedThemesUseProgramDirectoryAndNeverOverwrite()
{
    AppPaths paths;
    const QString json = QStringLiteral("{\"name\":\"松林\",\"page\":\"#112233\"}");
    const QVariantMap saved = paths.saveImportedThemeFile("theme-test", json);
    QVERIFY(saved.value("ok").toBool());
    QCOMPARE(saved.value("path").toString(), QDir(testRoot()).filePath("themes/theme-test.json"));
    QVERIFY(paths.saveImportedThemeFile("theme-test", json).value("ok").toBool());
    QVERIFY(!paths.saveImportedThemeFile("theme-test", "{\"name\":\"different\"}").value("ok").toBool());
    QVERIFY(paths.saveImportedThemeFile("theme-second", json).value("ok").toBool());
    const QVariantMap loaded = paths.readImportedThemeFiles();
    QVERIFY(loaded.value("ok").toBool());
    QVERIFY(loaded.value("errors").toStringList().isEmpty());
    QCOMPARE(loaded.value("themes").toList().size(), 2);
    const QVariantMap original = paths.readThemeFile(QUrl::fromLocalFile(saved.value("path").toString()));
    QCOMPARE(QJsonDocument::fromJson(original.value("json").toString().toUtf8()).object().value("name").toString(),
             QStringLiteral("松林"));
}

void AppSettingsTest::importedThemesRejectInvalidFilesAndReportReadErrors()
{
    AppPaths paths;
    QVERIFY(!paths.saveImportedThemeFile("../outside", "{}").value("ok").toBool());
    QVERIFY(!paths.saveImportedThemeFile("theme-invalid", "not json").value("ok").toBool());
    QVERIFY(!paths.saveImportedThemeFile("theme-array", "[]").value("ok").toBool());
    QVERIFY(!paths.saveImportedThemeFile("theme-large", QString(65537, QLatin1Char('x'))).value("ok").toBool());
    const QString folder = QDir(testRoot()).filePath("themes");
    QVERIFY(writeFile(folder, "blocked"));
    QVERIFY(!paths.saveImportedThemeFile("theme-test", "{}").value("ok").toBool());
    QVERIFY(!paths.readImportedThemeFiles().value("ok").toBool());
    QVERIFY(QFile::remove(folder));
    QVERIFY(paths.saveImportedThemeFile("theme-test", "{}").value("ok").toBool());
    QVERIFY(writeFile(QDir(folder).filePath("broken.json"), "not json"));
    const QVariantMap loaded = paths.readImportedThemeFiles();
    QVERIFY(loaded.value("ok").toBool());
    QCOMPARE(loaded.value("themes").toList().size(), 1);
    QCOMPARE(loaded.value("errors").toStringList().size(), 1);
}

void AppSettingsTest::settingsUseDefaultsAndUpgradeVersionZero()
{
    {
        AppSettings settings;
        QCOMPARE(settings.theme(), AppSettings::Theme::Dark);
        QVERIFY(settings.isWritable());
    }
    QSettings config(AppPaths::configFilePath(), QSettings::IniFormat);
    QCOMPARE(config.value(QStringLiteral("meta/schemaVersion")).toInt(), 1);
    config.setValue(QStringLiteral("meta/schemaVersion"), 0);
    config.sync();
    QCOMPARE(config.status(), QSettings::NoError);
    AppSettings upgraded;
    QCOMPARE(upgraded.theme(), AppSettings::Theme::Dark);
    QVERIFY(upgraded.isWritable());
    config.sync();
    QCOMPARE(config.value(QStringLiteral("meta/schemaVersion")).toInt(), 1);
}

void AppSettingsTest::themesRoundTripAcrossInstances()
{
    const QList<AppSettings::Theme> themes{AppSettings::Theme::Dark, AppSettings::Theme::Light,
                                            AppSettings::Theme::Blue};
    for (const auto theme : themes) {
        {
            AppSettings settings;
            QVERIFY(settings.isWritable());
            QVERIFY(settings.setTheme(theme));
            QCOMPARE(settings.theme(), theme);
        }
        AppSettings reopened;
        QCOMPARE(reopened.theme(), theme);
    }
}

void AppSettingsTest::invalidThemeFallsBackToDark()
{
    {
        AppSettings settings;
        QVERIFY(settings.isWritable());
        QVERIFY(settings.setTheme(AppSettings::Theme::Blue));
    }
    QSettings config(AppPaths::configFilePath(), QSettings::IniFormat);
    config.setValue(QStringLiteral("Appearance/themeMode"), QStringLiteral("unknown"));
    config.sync();
    QCOMPARE(config.status(), QSettings::NoError);
    AppSettings reloaded;
    QCOMPARE(reloaded.theme(), AppSettings::Theme::Dark);
    QVERIFY(reloaded.isWritable());
    QCOMPARE(config.value(QStringLiteral("Appearance/themeMode")).toString(), QStringLiteral("unknown"));
}

void AppSettingsTest::refusesInvalidAndFutureSchemaVersions()
{
    {
        QSettings config(AppPaths::configFilePath(), QSettings::IniFormat);
        config.setValue(QStringLiteral("meta/schemaVersion"), QStringLiteral("not-a-number"));
        config.sync();
        QCOMPARE(config.status(), QSettings::NoError);
    }
    AppSettings invalidVersion;
    QVERIFY(!invalidVersion.isWritable());
    QVERIFY(!invalidVersion.setTheme(AppSettings::Theme::Light));
    {
        QSettings config(AppPaths::configFilePath(), QSettings::IniFormat);
        config.setValue(QStringLiteral("meta/schemaVersion"), 99);
        config.sync();
        QCOMPARE(config.status(), QSettings::NoError);
    }
    AppSettings futureVersion;
    QVERIFY(!futureVersion.isWritable());
    QVERIFY(!futureVersion.setTheme(AppSettings::Theme::Light));
    QSettings verify(AppPaths::configFilePath(), QSettings::IniFormat);
    QCOMPARE(verify.value(QStringLiteral("meta/schemaVersion")).toInt(), 99);
}

void AppSettingsTest::pathsStayUnderIsolatedApplicationDirectory()
{
    QCOMPARE(AppPaths::dataDirectory(), dataPath());
    QCOMPARE(AppPaths::imagesDirectory(), QDir(dataPath()).filePath(QStringLiteral("images")));
    QCOMPARE(AppPaths::databaseFilePath(), QDir(dataPath()).filePath(QStringLiteral("history.sqlite3")));
    QCOMPARE(AppPaths::configDirectory(), dataPath());
    QCOMPARE(AppPaths::configFilePath(), QDir(dataPath()).filePath(QStringLiteral("config.ini")));
    QVERIFY(AppPaths::isDataDirectoryWritable());
    AppPaths paths;
    QCOMPARE(paths.currentDataDirectory(), dataPath());
    QCOMPARE(paths.currentDataDirectoryUrl(), QUrl::fromLocalFile(dataPath()));
    QCOMPARE(paths.settingsFileUrl(), QUrl::fromLocalFile(AppPaths::configFilePath()));
    QCOMPARE(paths.localFilePath(paths.currentDataDirectoryUrl()), dataPath());
}

void AppSettingsTest::migratesDataIntoAnEmptyDirectory()
{
    const QString source = AppPaths::dataDirectory();
    const QString sourceFile = QDir(source).filePath(QStringLiteral("notes/sample.txt"));
    QVERIFY(writeFile(sourceFile, QByteArray("private test fixture")));
    const QString destination = QDir(testRoot()).filePath(QStringLiteral("migrated"));
    AppPaths paths;
    QSignalSpy changed(&paths, &AppPaths::dataDirectoryChanged);
    QVERIFY(changed.isValid());
    QVERIFY(paths.setDataDirectory(destination));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(AppPaths::dataDirectory(), source);
    QCOMPARE(paths.pendingDataDirectory(), destination);
    QVERIFY(QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                        | QDir::Hidden | QDir::System).isEmpty());

    QVERIFY(AppPaths::applyPendingDataDirectoryChange());
    QCOMPARE(AppPaths::dataDirectory(), destination);
    QCOMPARE(paths.pendingDataDirectory(), QString());
    QFile copied(QDir(destination).filePath(QStringLiteral("notes/sample.txt")));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("private test fixture"));
    QSettings bootstrap(bootstrapPath(), QSettings::IniFormat);
    QCOMPARE(bootstrap.value(QStringLiteral("Storage/dataDirectory")).toString(), destination);
    QVERIFY(!bootstrap.contains(QStringLiteral("Storage/pendingDataDirectory")));
}

void AppSettingsTest::keepsSourceWhenPendingDestinationChangesBeforeRestart()
{
    const QString source = AppPaths::dataDirectory();
    const QString sourceFile = QDir(source).filePath(QStringLiteral("notes/sample.txt"));
    QVERIFY(writeFile(sourceFile, QByteArray("source remains intact")));
    const QString destination = QDir(testRoot()).filePath(QStringLiteral("blocked-migration"));
    AppPaths paths;
    QVERIFY(paths.setDataDirectory(destination));
    QVERIFY(writeFile(QDir(destination).filePath(QStringLiteral("unexpected.txt")),
                      QByteArray("leave destination intact")));

    QVERIFY(!AppPaths::applyPendingDataDirectoryChange());
    QCOMPARE(AppPaths::dataDirectory(), source);
    QCOMPARE(paths.pendingDataDirectory(), destination);
    QFile sourceFileCheck(sourceFile);
    QVERIFY(sourceFileCheck.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFileCheck.readAll(), QByteArray("source remains intact"));
    QFile destinationFileCheck(QDir(destination).filePath(QStringLiteral("unexpected.txt")));
    QVERIFY(destinationFileCheck.open(QIODevice::ReadOnly));
    QCOMPARE(destinationFileCheck.readAll(), QByteArray("leave destination intact"));
}

void AppSettingsTest::rejectsInvalidAndNonEmptyDestinations()
{
    AppPaths paths;
    const QString source = AppPaths::dataDirectory();
    QVERIFY(!paths.setDataDirectory(QString()));
    QVERIFY(!paths.setDataDirectory(QStringLiteral("relative/path")));
    const QString destination = QDir(testRoot()).filePath(QStringLiteral("occupied"));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(writeFile(QDir(destination).filePath(QStringLiteral("keep.txt")), QByteArray("leave me")));
    QVERIFY(!paths.setDataDirectory(destination));
    QCOMPARE(AppPaths::dataDirectory(), source);
    QFile keep(QDir(destination).filePath(QStringLiteral("keep.txt")));
    QVERIFY(keep.open(QIODevice::ReadOnly));
    QCOMPARE(keep.readAll(), QByteArray("leave me"));
    QVERIFY(!QFileInfo::exists(bootstrapPath()));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    AppSettingsTest test;
    if (qEnvironmentVariableIsSet("CLIPBOARD_HISTORY_SETTINGS_TEST_CHILD"))
        return QTest::qExec(&test, argc, argv);

    QTemporaryDir isolatedRoot;
    if (!isolatedRoot.isValid())
        return 2;
    const QString executable = QDir(isolatedRoot.path()).filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    if (!QFile::copy(QCoreApplication::applicationFilePath(), executable))
        return 3;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CLIPBOARD_HISTORY_SETTINGS_TEST_CHILD"), QStringLiteral("1"));
    environment.insert(QStringLiteral("CLIPBOARD_HISTORY_SETTINGS_TEST_ROOT"), isolatedRoot.path());
    const QString qtBinaries = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    if (!qtBinaries.isEmpty()) {
        environment.insert(QStringLiteral("PATH"),
                           qtBinaries + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    }
    QProcess child;
    child.setProcessEnvironment(environment);
    child.start(executable);
    if (!child.waitForStarted() || !child.waitForFinished(-1))
        return 4;
    const QByteArray output = child.readAllStandardOutput() + child.readAllStandardError();
    std::fwrite(output.constData(), 1, size_t(output.size()), stdout);
    return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : 5;
}

#include "tst_app_settings.moc"
