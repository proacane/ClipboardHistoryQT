# 展开正文去重及标题样式统一

接续 EXPANDED-UI-2026-10-14.md、development-spec.md 的 UI 模块。根据用户反馈修正：展开时隐藏截断标题，以完整正文替代；收起恢复原标题。正文直接绑定 entryTitle.font 和 entryTitle.color，确保字体、字号、字重和颜色一致。展开图标沿用折叠配色及尺寸，类型和使用次数移至底部。纯文本、换行、300px 限高、加载/错误提示和复制语义保留；无数据层修改。

Release 主程序及验收程序构建成功。原生 D3D11 的独立展开/收起、字体颜色一致、无重复标题、图片裁剪/展开/点击复制验证通过；截图见 docs/acceptance/2026-10-14-expanded-text/expanded.png，使用合成内容，已目视检查。

当前桌面缩放下全量原生 UI 验收为 19 通过、1 失败、2 可选跳过：未改动的分类按钮居中测试 categoryContentsAreCentered 触发 0.6px 容差断言。此记录不能算全量原生验收通过，未扩大本次修改去改分类栏。

新试用包：dist/ClipboardHistory-Windows10-x64-Release-20261014-expanded-text.zip，继承前版运行依赖，更新主程序和清单。未重跑完整性能或 Windows 10 干净系统验证。
