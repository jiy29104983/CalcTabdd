# CalcTabdd

面向 notepad-- 的标签页计算器插件。名称中的 `dd` 表示 `--`，沿用现有 Markdown
插件工程的命名风格。

计划在独立标签页中提供类似对话的计算体验：底部输入公式，确认后在上方逐条显示
公式与结果。提交方式可采用 Ctrl+Enter，最终交互规则仍待需求阶段确定。

当前处于可行性调研阶段，尚未实现数学求值引擎、完整插件入口或可安装的 Windows DLL。
仓库保留整理后的调研文档；早期临时探针和迁移过程文件已清理。

## 文档

- [文档索引](docs/README.md)
- [宿主与标签页可行性调研](docs/host-feasibility.md)
- [2026-10-01 验证记录](docs/verification-2026-10-01.md)

优先验证路线是由宿主创建原生文本标签页，保留原生编辑器对象，在其内部嵌入计算器
界面。公开源码尚无完整的自定义标签注册接口；直接插入普通 QWidget 存在关闭路径风险。

## 本地目录

```text
CalcTabdd/
  docs/              整理后的调研与验证文档
  AGENTS.md          本地仓库指南，Git 忽略
  notepad--          指向 ../notepad-- 的软链接，Git 忽略
  build/             本机构建与验证产物，Git 忽略
```

共享宿主源码的实体仓库位于 `/home/testcode/notepad--`，与 CalcTabdd 和 Markdown
插件同级。本仓库的 `notepad--` 软链接仅用于访问这份独立参考源码，不纳入插件提交。

## 历史验证状态

| 项目 | 状态 | 范围 |
| --- | --- | --- |
| 静态源码调研 | passed | 已确认接口能力及限制 |
| Linux Qt 隔离探针 | passed | Enter 16/16，Ctrl+Enter 19/19，详见验证记录 |
| Windows Release 编译 | not run | 尚无正式插件 DLL |
| Artifact 校验 | not run | 尚无发布包 |
| 真实 notepad-- 宿主测试 | not verified | 加载、菜单、关闭、退出与输入法等仍需实测 |

下一步是用最小 Windows DLL 验证标签接入、宿主命令路由与关闭生命周期，再实现
计算引擎和历史功能。上述探针结果是历史记录，不能替代未来正式插件的回归与验收。
