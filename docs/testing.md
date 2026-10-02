# 构建、自动化测试与交付

## 当前测试套件

| 套件 | 验证内容 | 边界 |
| --- | --- | --- |
| `engine_tests` | 优先级、括号、科学计数法、函数、ans、错误、精度与区域设置、帮助清单能力可执行性 | 双精度实数计算，不承诺任意精度 |
| `page_tests` | 上下分行布局、按键提交、错误保留、公式复用、滚动、剪贴板、输入法预编辑事件、配色、@ 补全筛选与插入／撤销／取消、帮助搜索和关于信息 | Qt 控件事件级验证，不代表真实中文输入法验收 |
| `plugin_tests` | 实际动态库加载、两个导出、与宿主头文件逐字段 ABI 对照、真实 QScintilla 嵌入、原生缓冲区隔离、菜单路由、切换、关闭、重开、多窗口、销毁、帮助／关于菜单共用窗口、补全过程的原生缓冲区隔离 | 宿主窗口由测试驱动模拟，未执行完整 CCNotePad 的关闭/退出实现 |

`plugin_tests` 必须提供 `CALCTABDD_HOST_SOURCE_DIR`。正式插件本体不链接测试用的
QScintilla。宿主头文件强制的 DLL 导入宏仅在构建目录的测试头文件副本中移除，
用于静态测试依赖；不修改共享宿主源码或插件 ABI。CI 固定检出宿主提交 `91105f68b74382128f3313ac5af8accdc77de918`。

本地 Linux 测试允许使用已有 Qt 开发包和宿主 QScintilla 静态库。测试记录由 QtTest 写入
构建目录的 `*_tests.txt`；CTest 日志位于 `Testing/Temporary/`。定义
`CALCTABDD_SCREENSHOT_DIR` 环境变量后，页面测试还保存明暗两种截图。

## Windows 流水线

入口为 `.github/workflows/windows.yml`。环境固定为 GitHub `windows-2022`、Qt 5.15.2
`win64_msvc2019_64`、Visual Studio 2022 generator、MSVC v142、x64、Release。
Action 固定完整提交 SHA。

1. 检出插件和固定宿主测试依赖，安装 Qt。
2. 构建 DLL、计算引擎、页面测试和宿主配套 QScintilla。
3. 实际运行三组自动化测试；任一失败、跳过或缺失则不打包。
4. 生成 ZIP、SHA256 及 `BUILD-INFO.json`，记录源码提交与各组测试结果。
5. 校验包文件清单、校验和、PE32+ x64 格式、两个插件入口、Release 运行库依赖。
6. 上传测试日志、截图和安装包 Artifact。
7. 主分支成功运行后，重新下载并校验安装包，创建唯一标签对应的预发布下载。

PR 不发布预发布。预发布标签格式为 `test-v<版本>-<run_number>-<run_attempt>`，不覆盖旧包。
安装包不携带 Qt DLL，不覆盖宿主运行库。预发布正文明确保留真实宿主 `not verified` 状态，
由用户使用 [手动测试清单](manual-testing.md) 完成最终确认。

## 状态规则

分开记录静态检查、本地自动化、Windows 编译、Windows 自动化、下载包校验和真实宿主。
使用 `passed` / `blocked` / `not run` / `not verified`。绿色 CI 不能替代实际宿主 DLL
加载、完整关闭退出路径、输入法、DPI 等手工结果；早期调研 19/19 也不能替代当前回归。
