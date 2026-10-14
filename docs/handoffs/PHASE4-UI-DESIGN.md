# 任务交接：阶段4 - 历史界面设计与 QML 可访问性

- 状态：新 UI 已接入 QML；Release 应用目标构建通过，桌面体验验收待做
- 负责人对话：专门推进 ClipboardHistoryQT 的 UI 设计
- 日期：2026-10-13
- 关联开发规范/任务：[开发与测试规范](../development-spec.md)、[阶段4历史模型集成](PHASE4-HISTORY-MODEL-INTEGRATION.md)

## 目标和结果

- 目标：评估现有 QML，给出可落地的界面概念预览，并限于 UI/QML 范围完成必要的界面改进。
- 参考新设计稿 `D:\文档\xwechat_files\wxid_3mmatiop5s9n22_a1b8\msg\file\2026-10\clipboard_opus_v2.html`：顶部主页/设置导航、快捷键提示、搜索后分类筛选、记录卡片、键盘提示底栏，以及卡片式设置分组。
- 当前实现：无左侧导航栏，主页/设置图标与快捷键、关闭按钮统一在顶栏；左侧用应用图标、标题和简短副标题呈现品牌区；不显示“监听中”状态。搜索在分类筛选上方，筛选图标和文字居中，卡片式列表保留。记录点击复制，展开按钮在卡片底部居中，时间与收藏/删除操作位于右侧；图片点击复制，图片沿用 96px 缩略预览，文件行继续显示模型提供的 `sourceMissing` 状态。
- 设置页按 HTML 预览使用带图标和说明的分组标题，组内选项左右排列；支持暗色、浅色、北极光和自定义主题，移除“跟随系统”。旧配置中的“跟随系统”会迁移为暗色。运行时主题文件为可执行文件同目录的 `custom-theme.json`；`resources/custom-theme.json` 是构建/首次启动时的默认模板。
- 保留真实 HistoryModel 字段：设计稿中的文件大小、计数、标签示例没有对应模型数据，本轮不伪造这些字段。
- 已完成：Windows 右上角关闭按钮和窗口关闭事件支持每次询问、记住“保存到托盘”或“直接退出”；设置页可更改关闭行为，默认每次询问。
- 已完成：图片预览横向铺满记录内容区，展开图片提高预览高度；展开长文本以图标栏+可滚动正文呈现，标签、展开/收起、时间和收藏操作统一放在下方操作行。
- 已完成：快捷键设置改为按键录制按钮，监听用户按下的组合键并注册；录制时暂时注销旧全局热键，避免重复按下旧组合触发窗口切换；Esc、失焦或注册失败时恢复旧热键，注册成功后才保存新值。
- 已完成：主动显示文件源已不存在状态，在对应文件记录行内提示。由 C++ HistoryModel 检查并通过 `sourceMissing` 角色提供状态，QML 不直接做文件系统检查；源文件父目录可监视时，恢复文件会自动清除行内提示；复制失败/成功也会即时标记/清除。
- 已完成：分类芯片改为可键盘聚焦和激活的按钮，提供当前分类的可访问名称/描述和可见焦点边框；支持主题为暗色、浅色、北极光及自定义。
- 已完成：设置支持宽 720px 至屏幕可用宽度（上限 3840px）、高 520px 至屏幕可用高度（上限 2160px）；用户可锁定当前宽高，解锁时拖动窗口的尺寸会延时保存到设置；设置页允许直接输入宽高。
- 本轮 UI 跟进：JSON 导入会要求用户命名，并把主题作为新选项追加到主题列表；调色板保存到程序目录的 `themes/*.json`，当前选择仍保存在 `Appearance` 设置中。预览卡片固定 180×166 个逻辑像素、间距 8px 自动换行；设置页移除基础自定义 JSON 的说明。展开/收起只显示文字；常规小字号从 9–10px 提升到 10–11px，微型主题预览保留紧凑比例。
- 未完成/阻塞：Windows 10 DPI、辅助技术和 Linux 实机体验仍待桌面验收。

## 改动清单

| 文件 | 变更用途 | 需要集成者注意的事项 |
|---|---|---|
| `src/ui/Main.qml` | 统一顶部导航、列表/设置外壳、搜索/分类布局、卡片列表和主题预览；沿用复制/收藏/删除/展开/源缺失状态 | 未改变数据模型接口或数据库 schema；旧“蓝色”主题值仍映射到北极光调色板，旧“跟随系统”设置转为暗色 |
| `src/ui/Main.qml` | 品牌顶栏、快捷键前的主页/设置图标、快捷键后的圆形关闭按钮；窗口宽高上下限、锁定与自动保存 | 配置键 `Appearance/windowSizeWidth`、`windowSizeHeight`、`resolutionLocked`；尺寸以 Qt 逻辑像素记录 |
| `src/ui/ThemePalette.js` | 主题 JSON 不完整时提供编译内置的颜色回退 | 运行时不直接编辑此文件；完整自定义色板读写于可执行文件同级的 `custom-theme.json` |
| `resources/custom-theme.json`、`cmake/copy_theme_if_missing.cmake`、`CMakeLists.txt` | 默认主题模板打入资源，并在构建后复制到可执行文件同级 | 仅在目标文件不存在时复制，保留用户在该位置编辑的主题 |
| `docs/custom-theme.example.json` | 可直接导入的暖色自定义主题参考文件 | 保持解析器支持的颜色字段和值格式 |
| `src/ui/Main.qml` | 分类按钮可键盘聚焦并有可访问标签；图片为缩略预览，行内源缺失状态由 `sourceMissing` role 驱动 | 本轮完整构建与桌面验收待补 |
| `src/core/history/history_model.h/.cpp` | 增加 `sourceMissing` 模型角色；加载页面时检查文件路径并记录缺失条目 ID | QML 只显示状态，不访问文件系统 |
| `src/ui/Main.qml` | 文件类别统一显示为“文件”；在对应记录行内显示“源文件已不存在” | 复制时仍保留原有检查，处理列表刷新后文件才被删除的情况 |
| `docs/development-spec.md`、`docs/development-plan.html` | 同步顶部导航、搜索/筛选和卡片列表目标 | 保持与最终 QML 结构一致 |
| `docs/handoffs/PHASE4-UI-DESIGN.md` | 记录概念预览、变更范围和验证状态 | 后续桌面验收结果应补充到本文件或阶段4集成记录 |

## 契约和决策

- 对外接口 / 数据 schema / 配置变化：新增内部 `sourceMissing` 模型角色供 QML 使用；关闭偏好保存在 `Appearance/closeAction`，窗口尺寸/锁定保存在 `Appearance/windowSizeWidth`、`windowSizeHeight`、`resolutionLocked`；基础自定义主题保存在程序同级 `custom-theme.json`，命名导入主题保存到同级 `themes/*.json`，当前选择保存在 `Appearance/selectedImportedThemeId`；`importedThemesJson` 仅保留迁移用途，不涉及数据库 schema。
- 兼容影响：新安装默认尺寸 920×680；旧配置缺少窗口尺寸键时使用默认值，既有数据库与剪贴板记录不受影响。
- 已同步给哪些依赖模块：无接口变化，无需同步。

## 自定义主题同级文件跟进（2026-10-13）

- 构建模板来自 `resources/custom-theme.json`，编入资源用于首次启动恢复，并在应用目标构建后复制到可执行文件同级；复制脚本只在文件缺失时写入，不覆盖用户改色。
- 基础“自定义”主题在启动时从该同级文件读取，源码直接编辑后重启生效；早期版本保存在 Qt Settings 中的有效基础主题会在首次启动迁移到文件。从设置页导入的命名主题保存于程序目录的 `themes` 文件夹，不覆盖基础主题。
- 文件读取/写入保留 64 KiB JSON 对象限制，并通过 `QSaveFile` 原子写入。文件目录必须可写；这与当前便携程序对同级配置文件的要求一致。

## 列表滚轮边界与分类内容居中（2026-10-13）

- 已复现：旧 `WheelHandler` 直接改变 `contentY`，连续向上滚轮可越过顶部；分类按钮的内容 Row 被 Control 拉宽，子文字仍从左侧排列。新增隔离回归在修前均失败。
- `src/ui/Main.qml`：分类 `contentItem` 改为 Item，内部图标/文字 Row 按自然宽度居中；文字与图标也垂直对齐，六个按钮等宽布局保留。
- 列表顶部/底部 12px 改为外部布局间距；滚轮保留原步长，并按 `originY`、内容高度、视口高度限定滚动范围，内容不足一屏时范围收敛。像素滚轮使用设备提供的像素位移。
- `tests/acceptance/tst_qml_acceptance.cpp`：验证六个分类的实际内容中心、顶部/底部连续滚轮不越界、中间正常滚动、过滤后的短列表不滚出空白。
- 验证：修后针对性用例通过；Release 应用/验收测试目标构建成功，`qmllint` 返回 0（已有静态提示保留）；`ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Release -R '^qml_acceptance$' --output-on-failure` 完整 QML 验收模块通过（1/1）。
- 未验收：真实鼠标/触控板和多 DPI 桌面体验；其余 CTest 模块未重跑。

## 固定主题预览与导入主题文件夹（2026-10-13）

- `src/ui/Main.qml`：预览使用 Flow、180×166 固定卡片和 8px 间距，窗口宽度只改变换行；移除 `custom-theme.json` 的说明和“自定义主题文件”文案，保留导入按钮。
- `src/core/settings/app_paths.h/.cpp`：新增 `readImportedThemeFiles()`、`saveImportedThemeFile(id, json)`，从可执行文件同级 `themes` 读取和原子保存独立 JSON；文件标识限定安全字符，每个文件最多 64 KiB；相同 ID 的不同内容拒绝覆盖。单个坏文件报告错误，继续加载其他文件。
- 启动迁移旧 `Appearance/importedThemesJson`：已存在的有效文件优先；其余条目写入独立文件，全部成功才清空旧设置。失败保留旧设置与可用主题供重试；新增导入也必须写入成功后才应用。
- `tests/settings/tst_app_settings.cpp`：隔离程序目录验证保存位置、重复导入不覆盖、非法标识/JSON/过大文件、文件夹创建失败和坏文件读取。
- `tests/acceptance/tst_qml_acceptance.cpp`：临时目录验证预览宽高不随窗口改变、固定间距/换行、重名主题独立文件、重新加载配色、写入失败保持当前主题、旧主题迁移失败后重试。
- 验证：Release 应用与两个测试目标构建成功；`qmllint` 返回 0（已有静态提示保留）；`ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Release -R '^(qml_acceptance|app_settings)$' --output-on-failure` 2/2 模块通过。
- 未验收：真实屏幕的各 DPI 视觉；全部 CTest 模块未重跑。程序目录的 `themes` 必须可写，失败会显示原因。

## 快捷键失败弹窗跟进（2026-10-13）

- `src/platform/windows/windows_platform_controller.cpp`：注册失败先保存 Windows 错误码；1409 明确说明组合键已被占用，其他错误使用系统错误文本和错误码，不误判为冲突。
- `src/ui/Main.qml`：设置、启动、取消录制恢复原快捷键失败及输入无效时均弹窗；显示组合键、失败原因和恢复结果，提供“知道了”和“修改快捷键”。“修改快捷键”打开设置、定位并开始录制；仅按修饰键时继续等待主键。
- 启动注册失败延后显示窗口和弹窗，确保 `--startup` 隐藏到托盘后仍能提醒用户。失败不写入新组合键，保留已保存的偏好；设置失败尝试恢复原快捷键。
- 弹窗使用现有主题令牌、按钮和可访问性角色。平台替身验证四种主题下弹窗背景跟随主题；不涉及数据 schema 或配置键变更。
- `tests/acceptance/tst_qml_acceptance.cpp`：补齐当前基础主题读取边界，并新增设置失败、启动占用/系统错误、窗口重新显示、恢复失败、取消恢复失败和修改入口回归。测试只操作临时数据和平台替身，不注册真实快捷键。
- 验证：Release `ClipboardHistory`、`tst_qml_acceptance` 目标构建成功；`qmllint src/ui/Main.qml` 返回 0，保留已有静态提示；`ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Release -R '^qml_acceptance$' --output-on-failure` 通过（1/1 模块）。
- 未验收：真实 Windows 快捷键被占用、登录启动托盘/焦点、实际屏幕 DPI 的弹窗外观；本轮未重跑全部 11 个 CTest 模块。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| Release 构建 | `& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Release --target ClipboardHistory --parallel 4` | 通过；QML 缓存和应用链接成功 |
| QML 静态解析 | `& 'C:\Qt\6.11.1\mingw_64\bin\qmllint.exe' src/ui/Main.qml` | 解析成功；有项目上下文对象和动态 `parent` 属性的静态提示，未发现语法错误 |
| 构建 | `& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Release --target ClipboardHistory --parallel 4` | 通过；包含 QML 与应用链接 |
| 新图片/展开/热键录制 UI 构建 | `& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Debug-QuitCheck --target ClipboardHistory --parallel 4` | 通过；QML 缓存生成并成功链接应用 |
| QML 静态检查 | `& 'C:\Qt\6.11.1\mingw_64\bin\qmllint.exe' src/ui/Main.qml` | 完成解析；输出项目已有的 context-property / `parent.hovered` 静态警告，未做 GUI 操作验收 |
| 本轮 Release 构建 | 构建 `tst_qml_acceptance` 和 `ClipboardHistory` | 通过；包含 QML 模块与自定义主题 JS |
| 本轮隔离 QML 验收 | `tst_qml_acceptance.exe`（Qt offscreen，临时数据库/合成图像） | 7 项通过、0 项失败、1 项可选压力测试跳过；覆盖展开/收起居中、图片点击复制及自定义主题色 |
| 关闭行为改动构建 | `& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Debug-QuitCheck --target ClipboardHistory --parallel 4` | 通过；QML 缓存生成并成功链接应用 |
| Debug 构建 | `& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --target ClipboardHistory --parallel 4` | 未通过：ld 报 `cannot open output file ClipboardHistory.exe: Permission denied`；未发现 undefined reference。可能是构建目录中的 exe 正在运行或被占用，未结束任何进程 |
| 启动 smoke check | 用 Qt 6.11.1 `windeployqt` 部署到隔离的 `build/ui-review-deploy`，启动 5 秒后正常存活；stderr 无输出 | 通过；没有执行真实剪贴板、主题切换或键盘辅助技术操作 |
| 同级自定义主题文件 | `cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Release --target ClipboardHistory --parallel 4` | 通过；`build/Desktop_Qt_6_11_1_MinGW_64_bit_Release/custom-theme.json` 已生成，存在性检查通过 |
| 顶栏导航和主题选项调整 | Release `ClipboardHistory` 目标构建；`qmllint src/ui/Main.qml` | 构建成功；静态解析返回 0，仍有 QML 动态上下文/布局警告；未运行自动化测试或桌面截图验收 |
| 命名导入主题、纯文字展开、常规小字调整 | Release `ClipboardHistory` 构建；`qmllint src/ui/Main.qml` | 构建成功；静态解析返回 0，保留项目已有动态上下文/布局提示；未运行自动化测试或桌面截图验收 |
| 自动化测试 | 本次同级文件跟进 | 未运行；本次只构建应用目标并确认 sidecar 输出 |
| 测试 | 自动化测试套件 | 未运行；本次只执行应用目标构建验证 |
| 性能基准 | UI 基准 | 未运行；不适用于本次小范围样式/可访问性修正 |

## 风险、已知问题与复现

- 风险/限制：尚未在真实桌面检查焦点顺序、读屏播报、窗口缩放、主题和视觉密度；隔离的 offscreen 测试不等同真实桌面 GPU/焦点行为。文件恢复监听依赖源文件父目录可被平台监视。
- 已知问题：Debug 构建目录中的 `ClipboardHistory.exe` 写入被 Windows 拒绝；关闭该构建目录启动的程序后重试。Release 应用目标已独立构建通过。
- 复现步骤：构建 `build/Release` 后启动应用，使用 Tab/Shift+Tab 聚焦分类芯片并用 Enter/Space 切换；分别检查暗色、浅色、北极光和自定义主题下的对比度。

## 接手者下一步

1. 在 Windows 10 不同缩放比例下检查分类栏宽度、拖动/缩放和键盘焦点可见性。
2. 用读屏工具确认分类名称/选中状态与缺失文件提示可理解。
3. 切换暗色、浅色、北极光和自定义主题，确认选中行对比度。

## 集成验收

- [x] 已检查实际 diff 与交接文档一致
- [x] 依赖模块已收到接口变化（无接口变化）
- [ ] 该模块必测项及集成测试已核对（需桌面验收）
- [x] 未解决事项已进入交接下一步
