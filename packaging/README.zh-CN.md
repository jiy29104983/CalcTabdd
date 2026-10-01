# CalcTabdd 0.1.0 测试版安装说明

面向 notepad-- 的 Windows x64 标签页计算器。公式在上，结果在下；底部输入后按
Ctrl+Enter 或点击“计算”。普通 Enter 不提交。错误会保留公式，“再次使用”可以回填历史。

## 安装

1. 退出所有 notepad-- 窗口。
2. 把本包的 `plugin/calctabdd.dll` 放入 notepad-- 安装目录的 `plugin` 子目录。
3. 启动宿主，选择“插件 → CalcTabdd 计算器 → 打开计算器”。

目标运行库为 Qt 5.15.2 / MSVC v142，架构 x64。请不要覆盖宿主的 Qt DLL。
每个宿主窗口独立保留计算历史，关闭标签或退出后清空；重复打开会返回已有标签。

支持 `+ - * / % ^`、括号、小数、科学计数法、`pi` / `e` / `ans`，以及
`sqrt`、`abs`、`sin`、`cos`、`tan`、`ln`、`log`、`exp`、`floor`、`ceil`、`round`、
`min`、`max`、`pow`。`%` 为取余，三角函数采用弧度，数值为双精度浮点。

计算器标签激活时保存、另存为和查找替换等文档命令不可用；切回普通文档恢复。

本包包含 手测清单 `TESTING.md`、变更说明 `CHANGELOG.md`、构建信息 `BUILD-INFO.json`
与 许可证 `LICENSE`。Windows 自动化测试通过不代表真实宿主体验已验收。

卸载：退出宿主后删除 `plugin/calctabdd.dll`。
