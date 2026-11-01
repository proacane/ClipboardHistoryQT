# Windows Release 安装包

使用已构建的 Qt 6.11.1 / MinGW 13.1 / Release 运行库目录制作 Windows 10 1809+ x64 安装包。安装包编译器为 Inno Setup 6.7.3，安装位置 `E:\Tools\InnoSetup`。

在仓库根目录运行：

```powershell
& 'E:\Tools\InnoSetup\ISCC.exe' packaging/windows/ClipboardHistory.iss
```

默认输入为 `dist/ClipboardHistory-0.2.0-Windows-x64`。更换运行库目录时传入 `/DPackageDir="绝对路径"`，版本可用 `/DAppVersion=0.2.0` 覆盖。输入必须经过 Release 构建、windeployqt 和测试，不能包含个人历史或配置。

输出：`dist/ClipboardHistory-0.2.0-Windows-x64-Setup.exe`。

制作输入目录时，只复制 Release 的 `ClipboardHistory.exe`、`resources/custom-theme.json`、根目录 `LICENSE`，随后使用同一 Qt kit 的 `windeployqt --release --compiler-runtime --qmldir src/ui --no-translations --skip-plugin-types qmltooling` 部署。应包含 `Qt6Svg.dll`、`imageformats/qsvg.dll` 和 `iconengines/qsvgicon.dll`。安装脚本排除 QML 调试插件及仅被 3D profiler 使用的 `Qt6Quick3DUtils.dll`，主程序不使用 Qt Quick 3D。再收集所用 kit 的许可材料：

```powershell
python packaging/windows/collect-licenses.py dist/ClipboardHistory-0.2.0-Windows-x64 --qt-docs C:/Qt/Docs/Qt-6.11.1 --mingw C:/Qt/Tools/mingw1310_64
```

此脚本使用 Python 3 标准库，复制 Qt 许可全文、第三方组件声明以及 MinGW 运行库许可，并生成安装目录内的 `THIRD_PARTY_NOTICES.md`。更新 Qt 或应用版本时，应同步该声明内的版本和源代码下载位置。

默认安装到当前用户 `%LOCALAPPDATA%\Programs\ClipboardHistory`，无需管理员权限。程序需要写入安装目录；自选目录也必须可写。提供开始菜单快捷方式、可选桌面快捷方式和 Windows 卸载入口。安装结束默认不启动程序。

升级替换程序和运行库，保留 `data/`、`ClipboardHistory.ini`、`themes/` 和用户编辑的 `custom-theme.json`。卸载仅移除安装文件及快捷方式，保留历史、配置和主题；外部数据目录不处理。卸载前在程序设置中关闭开机启动，并从托盘退出。卸载后若需删除历史，请自行确认数据目录后删除。

升级前从托盘菜单“退出”，无需先卸载。固定 AppId 必须保持不变，`UsePreviousAppDir=yes` 会沿用同一用户之前的安装目录。便携版没有安装注册信息，需手动选择原来的可写程序目录。测试编译可指定 `/DTestAppId=CHTest.<32位随机十六进制>`，隔离正式安装记录，且不创建快捷方式或启动应用；这种测试安装包不能对外发布。

本包未进行代码签名，未完成干净虚拟机及完整发布性能门禁。2026-11-01 的 0.1.0 → 0.2.0 安装升级和卸载检查覆盖默认与外部数据目录，验证配置、主题、真实合成数据库及图片保留。
