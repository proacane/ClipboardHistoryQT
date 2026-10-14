# ClipboardHistoryQT · 剪贴板历史

基于 **Qt Quick / QML + C++17 + SQLite** 的本地剪贴板历史工具。保存复制过的文本、链接、图片和文件记录，按类型查找、收藏，并快速复制回原来的应用。

这是 [ClipboardHistory](https://github.com/proacane/ClipboardHistory) 的重构项目，当前优先支持 Windows 10，采用 **MIT** 许可证。Linux 平台集成尚未完成。


## 功能

| 功能 | 说明 |
| --- | --- |
| 持续采集 | 文本、HTTP(S) 链接、图片、本地文件；相同内容合并并更新采集时间和使用次数 |
| 查找历史 | 类型及收藏筛选、字面子串搜索、分页加载、全库总数统计 |
| 完整预览 | 长文本展开与收起；图片默认裁剪预览，展开查看完整图片 |
| 文件状态 | 保存源路径引用；源文件消失或恢复时更新提示，不保存源文件本体 |
| 快速使用 | 点击条目复制；Windows 可选恢复此前窗口并发送 Ctrl+V |
| Windows 集成 | 托盘、单实例、可录制的全局快捷键、可选登录自启动、应用图标 |
| 关闭行为 | 隐藏至托盘或直接退出，可记住选择并在设置中修改 |
| 自定义外观 | 暗色、浅色、北极光及 JSON 自定义主题；导入多个主题并命名 |
| 自动清理 | 可选按小时/天保留非收藏记录，也可设置总条目上限；默认全部关闭 |
| 数据迁移 | 自选存储目录；旧项目数据库只读导入，重复导入跳过已有记录 |

**删除规则：**收藏始终受保护。数量上限统计全库，包含收藏；收藏数量超过上限时，实际总数允许超过上限。时长按最近采集时间计算，后台分批执行。历史清理不会删除引用的源文件；仅清理已无记录引用的应用图片。关闭策略不会恢复已删除记录。

## 快速上手

1. 从源码构建，或使用项目提供的便携试用包；便携包须完整解压。
2. 将软件放在**可写目录**，运行 `ClipboardHistory.exe`。
3. 复制一些内容，使用默认 `Ctrl+Shift+V` 呼出窗口，或从托盘打开。
4. 在设置中选择主题、录制快捷键，并按需要开启自动粘贴、自启动及清理规则。

首次使用默认永久保留历史、无数量上限。真实启动程序会持续监听剪贴板；退出程序后停止监听。

## 数据目录与隐私

```text
ClipboardHistory.exe
ClipboardHistory.ini          # 当前/待切换的数据目录指针
custom-theme.json             # 基础自定义主题
themes/                       # 导入的独立主题文件
data/
  config.ini                  # 应用设置
  history.sqlite3             # 历史元数据，schema v5
  images/                     # 保存的图片
  cache/                      # QML / 图形管线缓存
```

设置 → 存储路径通过文件夹选择器选择**可写空目录**。修改在下次启动、数据库打开之前执行：复制原数据并切换；旧目录保留，失败时继续使用原位置。主题和目录指针仍在程序目录。迁移前建议退出程序并备份数据。

程序不会回退到 `%AppData%` 等系统用户目录。安装在 `Program Files` 等不可写位置时会提示错误；即使数据放在其他位置，程序目录也需要写入目录指针及主题。

历史保存在本地，当前没有云同步、遥测或数据库加密。部分敏感 MIME 标记会被忽略，但不能保证所有敏感内容都被识别，请保护数据目录并按需清理历史。`PrivacyLogger` 是独立模块，目前未接入实际应用日志。

## 编写自定义主题

主题是一个 **UTF-8 JSON 对象**，无需修改 QML 或重新编译。最小示例，保存为 `my-theme.json`：

```json
{
  "name": "我的青绿主题",
  "page": "#202936",
  "surface": "#293545",
  "textMain": "#E8EEF5",
  "accent": "#65B7A8"
}
```

设置 → 外观主题 → **导入主题 JSON…** → 选择文件 → 命名并添加。主题成为一个新的选项，文件保存在程序目录的 `themes/`，不会覆盖原主题。

- `page`、`surface`、`textMain`、`accent` 必填，其余字段可选。
- 颜色必须使用 `#RRGGBB`，不支持透明通道、短格式或颜色名称。
- `name` 可选；填写时须为非空字符串，最长 48 个字符。
- 文件不得超过 64 KiB；不支持注释、尾随逗号或未知字段。
- 未填写颜色继承内置的**基础自定义主题**，不会自动继承当前暗色/浅色主题。制作浅色主题建议填写完整配色。

| 字段 | 控制内容 |
| --- | --- |
| `page` | 页面背景（必填） |
| `surface` | 通用面板背景（必填） |
| `textMain` | 主文字（必填） |
| `accent` | 主强调色、主要操作（必填） |
| `sidebarSurface` | 顶栏等导航区域底色 |
| `rowSurface` | 历史卡片背景 |
| `line` | 边框、分隔线 |
| `textMuted` | 时间、使用次数等次要文字 |
| `selectedRow` / `selectedText` | 选中记录背景 / 文字 |
| `inputSurface` | 搜索及输入区域背景 |
| `tileSurface` / `tagSurface` | 控件及标签的通用底色 |
| `danger` / `dangerSurface` | 删除、错误等危险操作文字 / 背景 |
| `favorite` | 收藏星标 |
| `onAccent` | 强调色背景上的文字 |
| `kindAccent` / `kindTint` | 各类型的强调色 / 标签、图标底色 |

类型颜色仅接受 `文本`、`链接`、`文件`、`图片` 四个键。例如将以下片段合并到包含四个必填字段的主题中，不能单独导入：

```json
{
  "kindAccent": {
    "文本": "#62C6A5",
    "链接": "#6ABBE8",
    "文件": "#B6A0F7",
    "图片": "#EAB968"
  },
  "kindTint": {
    "文本": "#18382F",
    "链接": "#173247",
    "文件": "#302747",
    "图片": "#40351F"
  }
}
```

可复制 [完整默认模板](resources/custom-theme.json) 开始修改。

也可退出程序后编辑程序旁的 `custom-theme.json`，重启并选择对应基础自定义主题。此文件只在缺失时生成，构建不会覆盖已有修改。直接编辑 `themes/` 中的文件后同样需重启重新读取；再次导入会创建新选项。

同时调整文字和背景的对比度，检查普通/选中记录、搜索框、收藏、错误提示及展开正文。

| 导入错误 | 处理方法 |
| --- | --- |
| 必须提供四个颜色 | 补齐必填字段，使用 `#RRGGBB` |
| 未知主题字段 | 检查拼写；不添加 `version`、`font` 等未支持字段 |
| 类型或颜色无效 | 使用上述中文类型名，不使用旧名称“文件路径” |
| JSON 格式错误 | 去掉注释/尾随逗号，使用双引号 |
| 无法保存主题 | 检查程序目录写入权限及磁盘空间 |

## 从源码构建

### 依赖

- Qt **6.11 或 6.12**；当前验证 Qt 6.11.1。
- Qt Core、Gui、Quick、Quick Controls 2、Quick Dialogs 2、Sql；Windows 还使用 Network。
- CMake 3.21+，C++17 编译器，工具链须与 Qt kit 匹配。
- 当前验证 Windows kit：MinGW 64-bit / GCC 13.1。

Windows 目标为 Windows 10 1809+ / x64；CMake 针对此目标拒绝 Qt 6.13+。Linux 后端尚未完成，不将 Linux 构建视作完整功能支持。

### Qt Creator（推荐）

打开 `CMakeLists.txt`，选择 `Desktop Qt 6.11.1 MinGW 64-bit` kit，添加 **Release** 构建配置，再配置、构建并运行。需要调试信息或 QML Profiler 时可使用 RelWithDebInfo；日常试用使用 Release。

### 命令行

在仓库根目录运行 PowerShell，按本机安装位置修改路径。每步失败时先停止并解决错误。

```powershell
$env:PATH = "C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
$taskBuildDir = ".\build\Release"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B $taskBuildDir `
  -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build $taskBuildDir --parallel 4
& "$taskBuildDir\ClipboardHistory.exe"
```

已有 Qt Creator 构建目录可直接使用，不需要额外建立测试副本。第二次启动会激活已运行实例。

向未安装 Qt 的电脑提供程序时，须部署 Qt DLL、QML 和插件，不能只复制 EXE。可使用 kit 的 `windeployqt --release --qmldir src/ui` 部署到独立目录，并带上 `custom-theme.json`；保持 Qt 与编译器运行库版本一致。

## 验证状态

本公开仓库提供主程序源代码，测试、性能基准、内部开发文档及 agent 配置保留在本地开发环境，不参与主程序编译。直接拉取本仓库后即可使用 Qt Creator 或上述命令构建，无需 Qt Test。

当前仍为试用阶段，完整性能及发布验收尚未通过。最新本地 Release 的 11 个 CTest 模块和文本/图片展开原生专项通过；原生全量 UI 检查还有分类按钮居中精度断言失败。50 万条无结果搜索曾超出候选目标；主程序冷启动、持续 GPU 帧时间、长期后台资源、全部 DPI 及干净系统部署仍需补充验证。

## 代码结构与贡献

```text
src/app/               程序启动、图形缓存
src/core/history/      采集、模型、队列、数据库和图片存储
src/core/settings/     数据路径与配置
src/core/import/       旧版数据只读导入
src/core/logging/      独立隐私日志模块
src/platform/windows/ 托盘、热键、单实例、自启动、焦点恢复
src/ui/                QML 页面、主题默认值
resources/             图标、主题模板
```

QML 负责呈现及交互，C++ 负责采集、平台和业务，存储任务在后台队列执行。纯样式调整通常集中在 QML；接口或业务变化须同步核心和测试。

欢迎通过 [Issues](https://github.com/proacane/ClipboardHistoryQT/issues) 提交建议或复现步骤。请注明系统、Qt/构建版本、操作步骤及预期结果，分享前移除截图、数据库及日志中的私人内容。提交修改时请说明影响范围和验证方式。

## 许可证与致谢

原创代码使用 [MIT License](LICENSE)，版权归 proacane。Qt 及其他第三方内容保留各自许可证，MIT 不替代其许可；见 [第三方声明](THIRD_PARTY_NOTICES.md)。开发过程中参考 CopyQ、Ditto 的功能与架构思路，当前 Windows 热键直接使用 Win32 `RegisterHotKey`，没有引入 QHotkey。
