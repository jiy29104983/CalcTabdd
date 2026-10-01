# CalcTabdd 文档

CalcTabdd 是集成到 notepad-- 原生标签页的计算器插件。当前已有 0.1.0 测试版实现，
采用上下分行的公式与结果布局，真实 Windows 宿主验收由用户完成。

| 文档 | 内容 |
| --- | --- |
| [构建与测试](testing.md) | 本地构建、三组测试边界、Windows 流水线与测试包交付 |
| [手动测试](manual-testing.md) | 安装、公式输入、菜单、标签关闭、退出、输入法与 DPI |
| [0.1.0 测试版](releases/v0.1.0-preview.md) | 功能变化、安装、兼容性与验证边界 |
| [宿主可行性调研](host-feasibility.md) | 初始源码调查、接入方案、已知限制 |
| [2026-10-01 历史验证记录](verification-2026-10-01.md) | 已清理早期探针的检查项目与历史结果 |

共享宿主源码位于 `/home/testcode/notepad--`，本仓库 `notepad--` 为已忽略软链接。
CI 在 `build/host` 检出固定提交用于集成测试，不将宿主源码纳入插件仓库或安装包。
历史探针不作为当前测试；当前可运行测试位于 `tests/`。
