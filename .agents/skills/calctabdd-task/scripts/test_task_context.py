#!/usr/bin/env python3
"""Behavior checks for the read-only helper; no remote operations."""
import tempfile
from pathlib import Path
import unittest
from task_context import collect_docs, dependency_hints, parse_roadmap, recommend, reference_ids, resolve_task

TABLE = """| 编号 | 优先级／建议阶段 | 需求与范围 | 主要验收条件 | 前置依赖 | 成本／主要风险 | 责任人 | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| EVO-001 | P0／0.2 | 实机验收 | 用户确认 | 无 | 中 | 用户 | 已完成 |
| EVO-002 | P1／0.3 | 独立计算记录模型 | 保持行为 | EVO-001 | 中 | 待认领 | 待确认 |
| EVO-003 | P1／0.3 | 复制纯数值与记录 | 往返一致 | EVO-002 | 中 | 待认领 | 待确认 |
| EVO-010 | P1／后续 | 选区读取 | 原文不变 | 无 | 中 | 待认领 | 待确认 |
| EVO-016 | P3／后续 | 大整数 | 专项验证 | 无 | 高 | 待认领 | 暂缓 |

| 决策编号 | 关联需求 | 待确定的问题 | 建议起点 | 状态 |
| --- | --- | --- | --- | --- |
| D-01 | EVO-002、003、013 | ans 语义 | 保持行为 | 待确认 |
"""


class ContextTests(unittest.TestCase):
    def setUp(self):
        self.tasks, self.decisions = parse_roadmap(TABLE)

    def test_next_skips_completed_and_respects_prerequisites(self):
        self.assertEqual(recommend(self.tasks)["id"], "EVO-002")
        self.tasks[1]["status"] = "已完成"
        self.assertEqual(recommend(self.tasks)["id"], "EVO-003")

    def test_in_progress_is_resumed_first(self):
        self.tasks[3]["status"] = "进行中"
        self.assertEqual(recommend(self.tasks)["id"], "EVO-010")

    def test_pending_manual_acceptance_is_not_completion(self):
        self.tasks[1]["status"] = "待验收"
        self.assertEqual(recommend(self.tasks)["id"], "EVO-010")
        self.assertEqual(dependency_hints(self.tasks[2], self.tasks)[0]["status"], "待验收")

    def test_unknown_dependency_does_not_auto_qualify(self):
        self.tasks[1]["dependencies"] = "EVO-999"
        self.assertEqual(recommend(self.tasks)["id"], "EVO-010")
        self.assertEqual(dependency_hints(self.tasks[1], self.tasks)[0]["status"], "编号不存在")

    def test_explicit_task_accepts_id_name_or_invocation(self):
        for query in ["evo-002", "独立计算记录模型", "执行 EVO-002 任务", "继续 EVO-002 任务"]:
            with self.subTest(query=query):
                self.assertEqual(resolve_task(query, self.tasks)["id"], "EVO-002")

    def test_missing_and_ambiguous_tasks_are_errors(self):
        for query in ["EVO-999", "记录", "任务"]:
            with self.subTest(query=query), self.assertRaises(ValueError):
                resolve_task(query, self.tasks)

    def test_completed_explicit_task_is_reported_not_reselected(self):
        self.assertEqual(resolve_task("EVO-001", self.tasks)["status"], "已完成")

    def test_all_complete_or_deferred_yields_no_task(self):
        for task in self.tasks:
            if task["status"] != "暂缓":
                task["status"] = "已完成"
        self.assertIsNone(recommend(self.tasks))

    def test_compact_decision_references(self):
        self.assertEqual(reference_ids(self.decisions[0]["关联需求"]), ["EVO-002", "EVO-003", "EVO-013"])

    def test_duplicate_and_missing_headers_are_errors(self):
        for text in [TABLE.replace("| EVO-003 |", "| EVO-002 |"), TABLE.replace("主要验收条件", "不支持的表头"), "# empty"]:
            with self.subTest(text=text[:30]), self.assertRaises(ValueError):
                parse_roadmap(text)

    def test_escaped_pipe_in_scope(self):
        tasks, _ = parse_roadmap(TABLE.replace("独立计算记录模型", r"独立计算\|记录模型"))
        self.assertEqual(tasks[1]["scope"], "独立计算|记录模型")

    def test_ignored_local_requirements_and_recent_handoffs_are_listed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "docs/requirements").mkdir(parents=True)
            (root / "docs/handoffs").mkdir()
            (root / "docs/requirements/local.md").write_text("EVO-002 详细需求", encoding="utf-8")
            (root / "docs/handoffs/README.md").write_text("index", encoding="utf-8")
            (root / "docs/handoffs/2026-10-02-EVO-002.md").write_text("EVO-002 ongoing", encoding="utf-8")
            (root / "docs/handoffs/2026-10-03-EVO-010.md").write_text("EVO-010 context", encoding="utf-8")
            result = collect_docs(root, "EVO-002")
            self.assertEqual(result["task_requirements"], ["docs/requirements/local.md"])
            self.assertEqual(result["task_handoffs"], ["docs/handoffs/2026-10-02-EVO-002.md"])
            self.assertEqual(result["latest_handoffs"][0], "docs/handoffs/2026-10-03-EVO-010.md")
            self.assertIn("AGENTS.md", result["missing"])


if __name__ == "__main__":
    unittest.main()
