#pragma once

#include <QSettings>

namespace ClipboardHistory::Core {

class AppSettings final
{
public:
    enum class Theme { Dark, Light, Blue };

    AppSettings();

    Theme theme() const noexcept { return theme_; }
    bool setTheme(Theme theme);
    bool isWritable() const noexcept { return writable_; }

private:
    static constexpr int CurrentSchemaVersion = 1;

    QSettings settings_;
    Theme theme_ = Theme::Dark;
    bool writable_ = false;
};

} // namespace ClipboardHistory::Core
