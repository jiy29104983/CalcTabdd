# 2026-10-01 可行性验证记录

本文由首次调研的 JSON 结果、按键隔离前的失败记录和共享宿主路径复跑记录整理而来。
此次文档整理没有重新执行功能测试。临时探针、原始检索及迁移文件已清理，本文保存历史
检查项目与结论，不提供可直接复跑的测试工程。正式实现应建立自己的持续回归测试。

## 环境与范围

- 平台：Linux，GCC 13.3，C++14，Qt 5.15.13，offscreen。
- 宿主参考提交：`91105f68b74382128f3313ac5af8accdc77de918`。
- 主程序使用既有宿主配套 QScintilla 静态库；嵌入界面动态模块只依赖公共 Qt 接口。
- 探针只记录公式输入，未实现数学求值；未启动完整 CCNotePad。
- Qt 父子对象销毁检查不等于真实宿主的标签关闭或退出流程测试。

## 执行结果与边界

| 验证层次 | 状态 | 结果与范围 |
| --- | --- | --- |
| 静态源码调研 | passed | 标签容器、插件回调、关闭、焦点和会话保存路径已检查 |
| Linux Enter 方案 | passed | 解决按键冒泡后，16/16 检查通过 |
| Linux Ctrl+Enter 方案 | passed | 19/19 检查通过，明细如下 |
| 仓库迁移后复跑 | passed | 在 CalcTabdd 路径重新编译，Ctrl+Enter 19/19 |
| 共享宿主路径复跑 | passed | 使用 /home/testcode/notepad-- 的头文件重新编译，19/19 |
| Windows Release DLL 编译 | not run | 当时尚无正式插件 DLL |
| Artifact 校验 | not run | 当时尚无发布包 |
| 真实 notepad-- 宿主测试 | not verified | 加载、菜单、关闭退出、输入法和 DPI 均未验收 |

## Ctrl+Enter 方案的 19 项检查

| 序号 | 检查内容 | 原始检查标识 | 结果 |
| --- | --- | --- | --- |
| 1 | 初始原生编辑器为空且无修改标记 | `initial_editor_is_empty_and_clean` | passed |
| 2 | 标签根对象保持为原生 QScintilla | `tab_root_remains_original_qscintilla` | passed |
| 3 | 嵌入界面覆盖编辑器区域并可见 | `embedded_page_covers_editor` | passed |
| 4 | 输入区获得键盘焦点 | `page_receives_keyboard_focus` | passed |
| 5 | 普通 Enter 不提交公式 | `plain_enter_does_not_submit` | passed |
| 6 | 普通 Enter 保留待提交公式 | `plain_enter_preserves_pending_formula` | passed |
| 7 | 普通 Enter 不修改底层文本 | `plain_enter_does_not_modify_editor` | passed |
| 8 | Ctrl+主键盘 Enter 与 Ctrl+小键盘 Enter 各追加记录 | `ctrl_enter_and_ctrl_keypad_enter_append_two_rows` | passed |
| 9 | 提交后清空输入区 | `input_cleared_after_submit` | passed |
| 10 | 输入与提交均不修改底层缓冲区 | `editor_buffer_unmodified` | passed |
| 11 | 历史区鼠标命中嵌入控件 | `history_hit_test_reaches_overlay` | passed |
| 12 | 切到普通标签时隐藏计算器页面 | `switch_away_hides_calculator` | passed |
| 13 | 模拟宿主 viewport 聚焦时转交输入区 | `host_viewport_focus_redirects_to_input` | passed |
| 14 | 切回标签后页面可见且历史保留 | `return_to_tab_preserves_history` | passed |
| 15 | 嵌入界面随标签尺寸调整 | `overlay_tracks_tab_size` | passed |
| 16 | 第二窗口历史独立 | `second_window_has_independent_history` | passed |
| 17 | 销毁原生编辑器时释放计算器页面 | `editor_destruction_cleans_page` | passed |
| 18 | 关闭一处页面不影响另一窗口 | `other_window_survives_close` | passed |
| 19 | 普通文档内容不变 | `ordinary_document_unchanged` | passed |

## 曾发现的失败及处理

按键隔离前，`editor_buffer_unmodified` 失败，其余 15 项检查通过。原因是普通 Enter
从输入框向父级原生编辑器传播，导致隐藏文本变化；不能因为可见历史区工作正常就判定
接入成功。

在嵌入页面接收未处理的 keyPress/keyRelease 后，Enter 方案 16/16 通过；进一步改为
Ctrl+Enter 提交，增加普通 Enter 不提交、保留输入且不修改底层文本的检查，19/19 通过。
这说明按键边界需要专门处理；它不证明宿主 QAction 或应用级快捷键已正确路由。

## 真实宿主仍需验证

- 当前标签关闭、关闭其他、关闭左右、关闭全部、退出和取消退出。
- 会话恢复与插件历史独立保存的交互。
- Ctrl+S、另存为、查找替换、撤销，以及宿主菜单剪贴板操作。
- 多窗口、中文输入法、明暗主题、显示缩放和 DPI。

接入方案与源码位置见[可行性调研](host-feasibility.md)。
