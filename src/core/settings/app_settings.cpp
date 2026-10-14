#include "app_settings.h"

#include "app_paths.h"

namespace ClipboardHistory::Core {
namespace {

QString themeName(AppSettings::Theme theme)
{
    switch (theme) {
    case AppSettings::Theme::Dark: return QStringLiteral("暗色");
    case AppSettings::Theme::Light: return QStringLiteral("浅色");
    case AppSettings::Theme::Blue: return QStringLiteral("蓝色");
    }
    return {};
}

AppSettings::Theme parseTheme(const QString &value)
{
    if (value == QStringLiteral("浅色")) return AppSettings::Theme::Light;
    if (value == QStringLiteral("蓝色")) return AppSettings::Theme::Blue;
    return AppSettings::Theme::Dark;
}

} // namespace

AppSettings::AppSettings()
    : settings_(AppPaths::configFilePath(), QSettings::IniFormat)
{
    settings_.sync();
    if (settings_.status() != QSettings::NoError)
        return;

    bool versionIsValid = false;
    const int version = settings_.value(QStringLiteral("meta/schemaVersion"), 0).toInt(&versionIsValid);
    if (!versionIsValid || version < 0 || version > CurrentSchemaVersion)
        return;

    writable_ = true;
    if (version < CurrentSchemaVersion) {
        settings_.setValue(QStringLiteral("meta/schemaVersion"), CurrentSchemaVersion);
        settings_.sync();
        if (settings_.status() != QSettings::NoError) {
            writable_ = false;
            return;
        }
    }

    theme_ = parseTheme(settings_.value(QStringLiteral("Appearance/themeMode"), QStringLiteral("暗色")).toString());
}

bool AppSettings::setTheme(Theme theme)
{
    const QString value = themeName(theme);
    if (!writable_ || value.isEmpty())
        return false;

    settings_.setValue(QStringLiteral("Appearance/themeMode"), value);
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        writable_ = false;
        return false;
    }

    theme_ = theme;
    return true;
}

} // namespace ClipboardHistory::Core
