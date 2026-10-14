# 阶段 3：Windows 系统入口模块

- 状态：已接入应用主入口和 QML；自动化测试通过，待 Windows 桌面验收
- 日期：2026-10-11
- 关联规范：`docs/development-spec.md` 第 2、3、4、8 节；ADR-0001

## 目标和结果

实现一个 Qt Quick 可调用的 Windows 10 系统控制器，覆盖跨进程单实例、托盘显示/隐藏/退出、可配置的 Windows 全局热键和开机启动，不依赖 QHotkey 或 Qt Widgets。

已实现：

- 用 Windows 命名 mutex 处理跨数据目录的同一应用实例，并以 `QLockFile` 在实际数据目录防止两个程序副本同时写同一数据库；`QLocalServer` 接收后续进程的前台激活请求。实例键先作 SHA-256。
- 用 Win32 隐藏消息窗口处理全局热键和 Shell 托盘事件；托盘双击切换显示状态，右键菜单仅提供显示窗口和直接退出。主界面右上角“×”及 Windows 窗口关闭事件遵循“每次询问 / 保存到托盘 / 直接退出”偏好；询问时可记住选择，且可在设置页修改。托盘“退出”和窗口确认框的“直接退出”共用平台控制器的直接退出接口，不经过 QML 确认框。
- `RegisterHotKey` 支持 Ctrl/Alt/Shift/Windows 与一个字母、数字、F1–F24、方向/编辑键。注册采用新 ID 成功后才注销旧 ID，冲突失败时保留当前热键并发出错误。
- 设置页可开关当前用户登录启动；HKCU Run 项指向当前 exe 并附带 `--startup`，配置偏好留在应用数据目录。以该参数启动时托盘可用则隐藏窗口，否则正常显示。
- 可记住应用激活前的外部窗口；设置启用时，复制记录后尝试恢复该窗口并发送 Ctrl+V。仅在确认目标仍为前台窗口后注入按键，否则保留剪贴板内容并报告失败。
- 析构时注销热键、移除 Shell 图标、销毁窗口并释放实例锁。
- 在 `tests/platform/` 提供独立 CMake/CTest 工程，不修改根 `CMakeLists.txt`。

## 改动清单

| 文件 | 变更用途 | 集成注意事项 |
|---|---|---|
| `src/platform/windows/windows_platform_controller.h` | QObject/QML API、单实例结果和状态属性 | 仅添加到 WIN32 target |
| `src/platform/windows/windows_platform_controller.cpp` | QLocalServer/QLockFile、Win32 tray/menu/hotkey/autostart 实现 | 链接 `Qt6::Network`、`user32`、`shell32`、`advapi32` |
| `tests/platform/CMakeLists.txt` | 独立平台测试 target | 不并入根 CMake 前可单独配置运行 |
| `tests/platform/tst_windows_platform_controller.cpp` | IPC、热键、自启动注册/清除测试 | 测试用独立 HKCU 子项，不碰真实 Run 项 |

## 集成 API

模块头文件：`src/platform/windows/windows_platform_controller.h`

1. 在 `QGuiApplication` 初始化后创建 `WindowsPlatformController`。
2. 在数据路径迁移、数据库/剪贴板服务初始化之前调用，传入应用 ID 和最终数据目录：

   ```cpp
   const auto result = platform.acquireSingleInstance(QStringLiteral("ClipboardHistory"), dataDirectory);
   if (result == WindowsPlatformController::InstanceResult::Forwarded)
       return EXIT_SUCCESS;
   if (result == WindowsPlatformController::InstanceResult::Error)
       return EXIT_FAILURE;
   ```

3. 主窗口创建后调用 `platform.setWindow(rootWindow)`。返回 `false` 时检查 `lastError()`；此时 tray/hotkey 不可用。
4. 将 QObject 暴露到 QML，例如 `windowsPlatform`。QML/设置页调用：
   - `setGlobalHotkey("Ctrl+Shift+V")`：配置热键；失败返回 `false` 并发 `hotkeyRegistrationFailed(error)`。
   - `setGlobalHotkey("")`：注销并清除热键。
   - `setAutoStartEnabled(true/false)`：设置/移除当前用户登录启动项。
   - `showWindow()`、`hideToTray()`、`toggleWindow()`、`quit()`。
   - 监听 `lastError` 与 `trayAvailable`；平台控制器的 `quit()` 直接请求 `QCoreApplication::exit(0)`。
5. 副实例转发请求后退出，不应继续创建剪贴板监听器或打开数据库。

## CMake 集成项

现有 WIN32 应用 target 增加：

```cmake
find_package(Qt6 6.11 REQUIRED COMPONENTS Network)
target_sources(ClipboardHistory PRIVATE
    src/platform/windows/windows_platform_controller.h
    src/platform/windows/windows_platform_controller.cpp
)
target_link_libraries(ClipboardHistory PRIVATE Qt6::Network user32 shell32 advapi32)
```

此处只列出 Windows 模块的新增依赖；Qt Core/Gui 已由主应用链接。不得把该实现加入 Linux target。

## 验证证据

使用 Qt 6.11.1 MinGW 64-bit、MinGW 13.1，在 Windows 10 环境执行：

```powershell
$env:PATH='C:/Qt/6.11.1/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;'+$env:PATH
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' -S tests/platform -B build/platform-windows-tests `
  -G 'MinGW Makefiles' -DCMAKE_PREFIX_PATH='C:/Qt/6.11.1/mingw_64' `
  -DCMAKE_CXX_COMPILER='C:/Qt/Tools/mingw1310_64/bin/g++.exe' `
  -DCMAKE_MAKE_PROGRAM='C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe' `
  -DCMAKE_BUILD_TYPE=Debug
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --build build/platform-windows-tests --parallel 4
& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir build/platform-windows-tests --output-on-failure -j1
```

- 构建：通过。
- CTest：1/1 target 通过；QtTest 覆盖单实例子进程前台转发、无效热键错误、空热键清除和退出释放锁。
- 开机启动注册/删除用例写入隔离 HKCU 测试键；当前沙箱返回 `ERROR_ACCESS_DENIED`，该用例明确跳过，未触碰真实 Run 项。QML 设置开关和配置落盘回归通过。
- 性能基准：不适用。
- 平台目标自动化通过：单实例副进程前台转发、无效热键错误、空热键清除。部署 Release 副本启动并完成二次实例转发。
- 2026-10-11 退出行为修正：顶栏“×”调用平台退出；Alt+F4 在托盘可用时隐藏，否则退出。Debug `ClipboardHistory` 目标在 `build/Debug-QuitCheck` 配置并构建通过；本轮未运行自动化测试或桌面点击验收。
- 2026-10-13 图标补齐：多尺寸 `.ico` 编入 Windows EXE 和托盘，PNG 资源提供 Qt 窗口图标；Release 应用与 Windows 平台测试目标均编译通过，PE 中可提取 32×32 图标。新便携包完成 `windeployqt` 部署与 ZIP 文件清单检查；未启动程序做桌面图标/剪贴板验收。
- 2026-10-13 托盘菜单调整：右键菜单移除“隐藏到托盘”和分隔线，仅保留“显示窗口”“退出”；双击行为及主窗口的关闭到托盘行为不变。本轮未运行自动化测试或桌面托盘点击验收。
- 平台测试还覆盖没有先前窗口时不执行粘贴。托盘实际显示/交互、Explorer 重启恢复、Windows 热键冲突/成功/注销、真实焦点恢复和 Ctrl+V 效果：未由自动测试覆盖，需 Windows 桌面验收。

## 限制与风险

- 该模块仅适用于 Windows；Linux/Wayland 目前没有对应实现。
- 热键格式是一个带至少一个修饰键的单键组合；不接受多段快捷键或无修饰键按键。Windows/Qt 规范化后的热键字符串会保存在 `globalHotkey` 属性。
- 已有统一的程序、Qt 窗口和托盘图标资源；Windows 桌面显示仍待人工验收。
- `setWindow()` 应传入应用实际顶层 `QWindow`。模块不会拦截窗口关闭事件；关闭按钮行为需由 QML/主窗口生命周期定义。
- 热键系统注册冲突只能通过失败状态呈现，不能告知占用它的其他应用。
- 自启动只对当前 Windows 用户生效；软件移动后，下次手动启动会将启动项更新为新路径。实际登录启动和托盘行为仍需桌面验收。
- Explorer 重启或崩溃后，Shell 托盘图标不会自动重建；升级为支持 `TaskbarCreated` 恢复消息后再补测。
- Windows/目标应用可拒绝前台切换或模拟输入；设置允许关闭自动粘贴，失败时剪贴板仍保留已复制内容。
- 选择了 Win32 `RegisterHotKey` 而非 QHotkey，以降低首版依赖与来源锁定工作；若未来 Linux 要复用 API，需要另写平台后端。

## 接手者下一步

1. 在 Windows 10 桌面手工验收程序/窗口/托盘图标，以及登录启动、托盘、热键冲突、显示隐藏、退出和二次启动前台激活。
