# Clipboard History

A local-first clipboard history manager built with Qt Quick/QML and a C++ core.

## Current status

Core capture and history display are connected: the app continuously listens for text, image, local file URLs, and HTTP(S) links and stores them in the app data directory. The paged list supports type and favorites filters, substring search, long-text expansion, and cropped image previews that expand on demand. Clicking a valid entry copies it back to the clipboard; on Windows an enabled setting also restores the previously active window and sends Ctrl+V. A missing source file produces an explicit message. Favorites, single-entry deletion, and clear-history actions are available. On Windows, the app has a consistent executable, window, and tray icon, plus single-instance activation, a configurable global hotkey, and an optional login startup setting that launches to the tray.

The legacy importer is available in settings and reads the source database and images without modifying them. Missing file references update directly in the list when files disappear or return. The current database schema is v5.

This is a development build, not a release that has passed every gate. On 2026-10-12, Debug and RelWithDebInfo passed all 11 CTest modules, and a 30-minute isolated run on 500,000 synthetic records completed with zero Qt/QML warnings and correct final counts. Baselines, memory trends, and user experience feedback are recorded in [the acceptance report](docs/handoffs/ACCEPTANCE-2026-10-12.md). Formal hardware thresholds, sustained desktop rendering, cold/warm startup, and Windows integration checks remain incomplete. The independent search benchmark had two runs above the candidate threshold; slight late-run RSS growth does not establish leak freedom. Automatic retention, a full orphan scan, tray recovery after Explorer restarts, a Linux backend, an installer, and CI are not implemented. Login startup is implemented but still needs real desktop verification. The application license has not been selected.

## Build

Requirements:

- Qt 6.11 or 6.12 for the Windows 10 target, with Core, Gui, Quick, Quick Controls 2, Quick Dialogs 2, Sql, and Network; Qt Test is needed for tests
- CMake 3.21 or newer
- A C++17 compiler supported by the selected Qt kit

In Qt Creator, open CMakeLists.txt, select Desktop Qt 6.11.1 MinGW 64-bit, and choose Debug, Release, or RelWithDebInfo before configuring and building. The validated kit uses GCC 13.1.0 and C++17. The Windows target is Windows 10 1809 or newer; the latest acceptance ran on Windows 10 22H2. CMake rejects Qt 6.13+ on Windows. See [the technical baseline](docs/decisions/ADR-0001-technical-baseline.md).

To rebuild and test the existing Qt Creator RelWithDebInfo directory, run the following PowerShell from the repository root. Adjust the Qt tool paths if your installation differs. This reuses the existing CMake cache and does not create another build directory:

```powershell
$taskBuildDir = ".\build\Desktop_Qt_6_11_1_MinGW_64_bit_RelWithDebInfo"
$env:PATH = "C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B $taskBuildDir -DBUILD_TESTING=ON
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build $taskBuildDir --parallel 4
& "C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir $taskBuildDir --output-on-failure
```

Stop if any command reports failure. To use the existing Debug build, change only `$taskBuildDir` to `.\build\Desktop_Qt_6_11_1_MinGW_64_bit_Debug`. These are single-configuration MinGW builds: the selected directory/cache determines the build type.

After a successful build, launch the main application:

```powershell
& "$taskBuildDir\ClipboardHistory.exe"
```

Exit another running copy first if you want this executable to become the primary instance. A secondary launch activates the running instance. Running the application listens to the real clipboard; running the tests below does not.

## Test boundaries

CTest supplies each Windows test with the selected Qt and compiler DLL paths. The QML acceptance harness defaults to offscreen rendering and uses the real model, queue, service, and repository with temporary synthetic data. Clipboard, platform, and path boundaries are replaced for that harness; it does not register real hotkeys, create a real tray icon, or change the system clipboard. Platform process tests use independent instance keys and temporary lock directories.

These checks prove module and interaction behavior, not real GPU frame timing, tray clicks, hotkey conflicts, or automatic-paste focus. AppSettings and PrivacyLogger have passing standalone tests but are not wired into main/QML; the UI currently persists appearance settings through QtCore.Settings.

## Automatic retention

Preferences → 定时删除 lets you retain non-favorite history for a custom number of hours or days. It is disabled by default: unset or zero means keep forever. Edit the period and click 应用保留设置 to enable it. Favorites are exempt; referenced source files are never deleted. The period uses the most recent capture timestamp, so capturing identical content renews it.

Enabling or changing the period checks immediately, then checks every minute while running. Startup checks resume a saved policy. Cleanup runs on the storage thread in transactions of at most 500 records, deleting image payloads only when no history row still references them. Deleted history cannot be restored by turning the option off. For a portable trial, extract the complete Release ZIP into a writable folder and run ClipboardHistory.exe.

## Optional total entry limit

Preferences → 总条目数量上限 is disabled by default. Enable it, enter a count, and apply. It counts all history, including favorites, and deletes the oldest non-favorite records after captures and during background checks. Favorites are protected even when their count exceeds the limit. The time and count policies can be used independently or together; turning a policy off does not restore deleted records.

## Data location

By default, all runtime data is stored beside the executable in data/:

- config.ini for settings
- history.sqlite3 for history metadata
- images/ for image payloads
- logs/ is the intended privacy-log location; the logger module is not yet wired into the application

The settings page uses a Qt folder picker to select a writable, empty destination. It saves a pending change; on the next startup, before database handles are opened, the application copies the data and activates the new location. The old folder is retained. If migration fails, the application rolls back its copy and continues using the old location.

ClipboardHistory.ini beside the executable stores the active and pending folder paths. The app does not fall back to %AppData% or another system user directory. Run the portable build from a writable folder; a protected location such as Program Files cannot hold this app's data or path pointer and produces an explicit startup error.

Import a JSON theme in Preferences and choose its display name. Each imported theme is saved as an independent JSON file in the `themes` folder beside the executable, independent of the history data directory. Previously imported themes stored in settings are migrated to this folder at startup; failed migration preserves the original settings for retry. The folder must be writable. Theme previews have a fixed size and wrap as the window width changes.

The base custom palette still uses `custom-theme.json` beside the executable. The build seeds that file only when missing; importing additional themes does not replace it.
