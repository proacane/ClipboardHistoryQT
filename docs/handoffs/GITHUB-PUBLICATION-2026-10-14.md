# GitHub 公开源码准备（2026-10-14）

用户授权上传源代码并公开，确认在 proacane 下新建 ClipboardHistoryQT、使用 MIT。旧 ClipboardHistory 仓库保留。

README 改为中文，覆盖真实功能、便携数据目录与迁移、Qt Creator/命令行 Release 构建、测试边界、开发状态、架构和贡献入口。主题章节按 Main.qml.parseCustomTheme 与 AppPaths 的当前实现编写，涵盖四个必填字段、全部可选字段、内容类型颜色、UTF-8/64 KiB 限制、导入/命名/保存/重启及完整示例链接。未宣称支持系统跟随主题、Linux 后端、数据库加密或正式性能验收。

按用户选择加入根目录 MIT LICENSE，第三方开发技能保留自身版权，Qt 分发材料不由项目 MIT 替代。开发规范和 ADR 增补本次许可决策。

公开前检查全部现有 Git 提交的常见凭据签名，并检查当前已跟踪文件：不含运行历史数据库、真实配置、构建目录、试用包或 Profiler trace。README 本地链接和 JSON 模板检查通过。验收截图来自合成数据。

本次仅文档及许可变更，未运行新代码测试。程序最近一次 CTest 11/11；原生分类按钮居中断言问题和完整性能门槛仍以先前交接为准。
