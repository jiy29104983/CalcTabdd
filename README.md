# CalcTabdd

notepad-- 的标签页计算器插件，使用 C++14 / Qt 5.15。名称中的 `dd` 表示 `--`。
当前版本 **0.1.0 测试版**，采用“公式在上、结果在下”的连续计算记录布局。

## 使用

1. 将测试包中的 `plugin/calctabdd.dll` 放入 notepad-- 安装目录的 `plugin` 文件夹。
2. 重启 notepad--，选择 **插件 → CalcTabdd 计算器 → 打开计算器**。
3. 在底部输入公式，点击“计算”或按 **Ctrl+Enter**；普通 Enter 不提交。
4. 上方按顺序显示公式和结果。“再次使用 / 修改公式”可将原公式放回输入区；结果可选中复制。

每个宿主窗口拥有独立计算器。重复打开会回到已有标签；关闭标签后清空其记录，重开为新的
计算会话。当前不进行跨启动历史保存。尚待用户完成真实 Windows notepad-- 手动验收。

## 计算能力

- 四则运算 `+ - * /`、括号、小数、科学计数法，例如 `(1299 + 899) * 0.85`、`1e3`。
- `^` 乘方（右结合）；一元负号低于乘方：`-2^2 = -4`、`2^-3 = 0.125`。
- `%` 为取余，例如 `10 % 3 = 1`，不是百分比。
- 常量 `pi`、`e`、上一次成功结果 `ans`（初始为 0，失败计算不更新）。
- 函数 `sqrt`、`abs`、`sin`、`cos`、`tan`、`ln`、`log`、`exp`、`floor`、`ceil`、
  `round`、`min`、`max`、`pow`。三角函数采用弧度；`log` 为常用对数，`ln` 为自然对数。
- 使用 IEEE 754 双精度，显示最多 15 位有效数字；不是任意精度或符号计算系统。
- 除零、定义域、语法及溢出错误显示在对应记录中，保留输入便于修改。

## 宿主集成与兼容性

目标构建环境为 **Windows x64、Qt 5.15.2、MSVC v142**。公开 notepad-- 源码基线为
`91105f68b74382128f3313ac5af8accdc77de918`。本地 `v3.8.3` / `v3.9.0` 标签指向同一
源码提交，但不意味着两版发布二进制相同；实际 DLL 加载与宿主交互仍需手工验证。

插件让宿主创建并登记原生文本标签，保留原生 QScintilla 对象，在内部嵌入计算器页面，
不直接将普通 QWidget 插入宿主标签容器。底层空白文档设为只读并隔离输入事件。

计算器激活时，宿主菜单中的复制、粘贴、剪切、全选、撤销、重做转向计算器；保存、另存为、
查找替换等文本文件命令临时不可用。切回普通文档后恢复原操作。关闭、切换标签继续由宿主管理。

## 构建与测试

安装 Qt 5.15 开发包和 CMake 后：

```bash
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt \
  -DCALCTABDD_HOST_SOURCE_DIR=/path/to/notepad--
cmake --build build/local --parallel 4
ctest --test-dir build/local --output-on-failure --no-tests=error
```

没有指定宿主源码时仅运行引擎和页面测试，不包含原生控件集成测试。提供已编译的宿主
QScintilla 静态库时，可通过 `CALCTABDD_QSCINTILLA_LIBRARY` 指定；否则从固定宿主源码
构建测试依赖。正式插件仅依赖 Qt Core / Gui / Widgets，不依赖另一个 QScintilla DLL。

GitHub Windows 流水线在 `main` 推送、PR 和手动触发时编译 Release 并执行三个测试套件，
通过后打包 DLL、中文安装说明、手测清单、变更说明、许可证和构建信息。主分支成功构建
额外发布带提交号的**预发布测试包**，供手动验收，不标记为稳定正式版本。

## 文档

- [文档索引](docs/README.md)
- [构建和测试说明](docs/testing.md)
- [真实 Windows 宿主手动测试清单](docs/manual-testing.md)
- [0.1.0 测试版变更说明](docs/releases/v0.1.0-preview.md)
- [宿主与标签页可行性调研](docs/host-feasibility.md)
- [早期隔离探针的历史记录](docs/verification-2026-10-01.md)

`notepad--` 是本地共享参考源码的忽略软链接；`build/`、`dist/` 和本地指南不进入提交。
历史 19 项探针不属于当前测试套件。当前自动化测试在 `tests/`，验证边界见测试说明。

## 许可证

GNU GPL v3.0 or later，见 [LICENSE](LICENSE)。
