# 第三方声明

根目录 MIT License 覆盖本项目原创代码及文档，不重新许可第三方内容。

## Qt

应用依赖 Qt Core、Gui、Quick、Quick Controls 2、Quick Dialogs 2、Sql，以及 Windows 上的 Network；测试另外使用 Qt Test。Qt 不包含在本源码仓库中，由开发者的 Qt kit 提供。模块及其依赖各有许可证，请以所用版本自带的许可证和 [Qt 官方许可说明](https://doc.qt.io/qt-6/licensing.html) 为准。二进制分发须另行提供适用的第三方声明和许可材料；仓库的 MIT 文件不代替它们。

## frontend-ui-engineering

开发技能文档来源于 Addy Osmani 的 agent-skills 项目，保留独立 [MIT 许可证及原作者署名](.agents/skills/frontend-ui-engineering/LICENSE)。该文档用于开发流程，不编译或打包进应用。

## 功能与架构参考

- 旧项目：[proacane/ClipboardHistory](https://github.com/proacane/ClipboardHistory)。
- CopyQ、Ditto 的参考记录见 [开发规范](docs/development-spec.md)。参考产品行为及公开架构不表示它们成为应用依赖；如未来复制其实现，应先处理相应许可证。
- 当前未集成 QHotkey；Windows 热键使用系统 API。
