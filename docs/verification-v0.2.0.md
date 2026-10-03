# 0.2.0 帮助与补全验证记录

日期：2026-10-02。计算引擎算法保持不变，新增帮助、`@` 补全和关于信息。
本记录区分本地 Qt 控件测试、Windows 云端测试和真实 notepad-- 手动验收。

## 本地环境与复现

Linux x86_64，GCC 13.3.0，Qt 5.15.13，C++14，Release。
Qt 平台插件为 `offscreen`；使用真实宿主配套 QScintilla 静态库进行隔离集成测试。
宿主源码核对为 `91105f68b74382128f3313ac5af8accdc77de918`。

```bash
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCALCTABDD_HOST_SOURCE_DIR=/home/testcode/notepad-- \
  -DCALCTABDD_QSCINTILLA_LIBRARY=/tmp/markdownview-font-probe-20260911/lib/libqmyedit_qt5.a
cmake --build build/local --parallel 4
CALCTABDD_SCREENSHOT_DIR="$PWD/build/screenshots" \
  ctest --test-dir build/local --output-on-failure --no-tests=error
```

以上宿主和静态库路径是本次环境的本地依赖；其他环境替换为自己的路径，或省略
`CALCTABDD_QSCINTILLA_LIBRARY`，从同一固定宿主源码构建测试依赖。
正式插件本体不依赖单独的 QScintilla 动态库。

## 本地结果

| 检查 | 状态 | 结果与边界 |
| --- | --- | --- |
| Release 编译 | passed | 本地 Qt 5.15.13 / GCC 构建插件和测试，无项目编译警告 |
| engine_tests | passed | 67 passed、0 failed、0 skipped，含清单中 14 个函数和 3 个常量的可执行性检查 |
| page_tests | passed | 30 passed、0 failed、0 skipped，含 12 条插入位置／模板数据以及键鼠、输入法事件、撤销、帮助和关于 |
| plugin_tests | passed | 11 passed、0 failed、0 skipped，含动态库加载、ABI、原生缓冲区隔离、菜单帮助共用窗口及编辑路由 |
| 界面截图 | passed | 查看明暗计算器、候选、帮助和关于；截图位于忽略的 build/screenshots，云端也会上传诊断截图 |
| 真实中文输入法、Windows DPI、完整宿主退出流程 | not verified | 合成 Qt 输入法事件与隔离窗口不能替代真实系统和完整 CCNotePad 验收 |

数量包含各测试套件的 QtTest 初始化／清理项。`offscreen` 关于窗口置顶、键盘抓取等提示
是平台插件能力提示；测试通过不代表真实窗口管理器行为已经验证。

关键回归包括：`2+@sq*3` 仅替换查询、`@sq(9)` 保留参数、`@pow` 插入二元模板、
中英文筛选、Enter/Tab 与鼠标确认、Esc 取消、未完成查询阻止提交、普通除法／科学计数法
不触发候选、组词期间不抢输入法候选、切换／关闭标签清理弹窗、帮助不改变公式、
帮助和关于使用当前版本及源码兼容参考。

## 云端与安装包

本地 Qt 5.15.13 不能代替目标 Windows Qt 5.15.2 / MSVC v142 验证。推送后的
`.github/workflows/windows.yml` 执行 Windows Release 编译和同样三个测试套件，随后校验
ZIP 清单、SHA256、PE32+ x64、插件入口和运行库依赖。

云端具体提交、测试数量与运行编号以该提交的 Actions 日志和对应安装包
`BUILD-INFO.json` 为准；只有完整成功的主分支运行才发布预发布包。
本文件不将待执行的云端任务预记为 passed。真实宿主手测继续使用
[Windows 手动测试清单](manual-testing.md)，本节在 2026-10-02 记录时的状态为 **not verified**。

## 2026-10-03 用户实机验收反馈

用户已确认 EVO-001 需求，并明确反馈“实机已验收了”。当前实机验收状态更新为
**passed（用户反馈）**，EVO-001 已完成；反馈来源和未另提供的环境信息见
[实机验收记录](manual-testing.md#验收记录)。上表保留原验证时点的结论，不改写历史自动化证据，
本次用户确认也不外推到所有宿主版本或未来构建。
