#!/usr/bin/env python3
"""Read CalcTabdd task context; never execute a task or modify its status."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

FIELDS = {
    "id": "编号", "stage": "优先级／建议阶段", "scope": "需求与范围",
    "acceptance": "主要验收条件", "dependencies": "前置依赖",
    "cost_risk": "成本／主要风险", "owner": "责任人", "status": "状态",
}
OPEN = {"待确认", "待实施", "进行中"}


def table_rows(text):
    """Yield header-keyed Markdown rows, supporting escaped pipes."""
    header = None
    for number, line in enumerate(text.splitlines(), 1):
        if not line.startswith("|") or not line.endswith("|"):
            header = None
            continue
        cells = [c.strip().replace(r"\|", "|") for c in re.split(r"(?<!\\)\|", line[1:-1])]
        if cells and all(re.fullmatch(r":?-{3,}:?", c) for c in cells):
            continue
        if header is None:
            header = cells
            continue
        if len(cells) != len(header):
            raise ValueError(f"表格第 {number} 行列数与表头不一致")
        yield dict(zip(header, cells))


def reference_ids(text):
    """Expand notation such as EVO-002、003、013."""
    result = []
    for group in re.findall(r"EVO-\d{3}(?:\s*[、,，]\s*(?:EVO-)?\d{3})*", text):
        for suffix in re.findall(r"\d{3}", group):
            task_id = "EVO-" + suffix
            if task_id not in result:
                result.append(task_id)
    return result


def parse_roadmap(text):
    tasks, decisions = [], []
    for row in table_rows(text):
        if re.fullmatch(r"EVO-\d{3}", row.get("编号", "")):
            missing = set(FIELDS.values()) - row.keys()
            if missing:
                raise ValueError("需求表缺少列：" + "、".join(sorted(missing)))
            tasks.append({key: row[column] for key, column in FIELDS.items()})
        elif re.fullmatch(r"D-\d{2}", row.get("决策编号", "")):
            decisions.append(row)
    ids = [task["id"] for task in tasks]
    if not tasks:
        raise ValueError("未找到 EVO 需求表；不猜测任务")
    if len(ids) != len(set(ids)):
        raise ValueError("需求编号重复；先核对路线图")
    return tasks, decisions


def dependency_hints(task, tasks):
    statuses = {item["id"]: item["status"] for item in tasks}
    return [{"id": ref, "status": statuses.get(ref, "编号不存在")}
            for ref in reference_ids(task["dependencies"])]


def recommend(tasks):
    def priority(task):
        found = re.search(r"P([0-3])", task["stage"])
        return (0 if task["status"] == "进行中" else 1,
                int(found.group(1)) if found else 99, task["id"])
    candidates = [task for task in tasks if task["status"] in OPEN and
                  (task["status"] == "进行中" or all(h["status"] == "已完成" for h in dependency_hints(task, tasks)))]
    return min(candidates, key=priority) if candidates else None


def resolve_task(query, tasks):
    cleaned = re.sub(r"^(?:执行|继续)\s*", "", query.strip())
    cleaned = re.sub(r"\s*任务$", "", cleaned).strip()
    if not cleaned:
        raise ValueError("任务名称不能为空")
    if re.fullmatch(r"EVO-\d{3}", cleaned, re.IGNORECASE):
        matches = [t for t in tasks if t["id"] == cleaned.upper()]
    else:
        matches = [t for t in tasks if cleaned.casefold() in t["scope"].casefold()]
    if len(matches) != 1:
        ids = "、".join(t["id"] for t in matches)
        raise ValueError(f"任务未唯一匹配：{cleaned}；候选：{ids or '无'}")
    return matches[0]


def collect_docs(root, task_id):
    """Include ignored local requirements; return paths rather than instructions."""
    required = ["AGENTS.md", "docs/README.md", "docs/roadmap.md", "docs/handoffs/README.md", "docs/testing.md"]
    handoffs, requirements = [], []
    for path in sorted((root / "docs").rglob("*.md")):
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        mentions = bool(task_id and task_id in text)
        is_handoff = "handoffs" in path.parts or "handoff" in path.name.casefold() or "交接" in path.name
        if is_handoff and path.name != "README.md":
            handoffs.append({"path": rel, "mentions_task": mentions})
        elif "requirements" in path.parts and mentions:
            requirements.append(rel)
    handoffs.sort(key=lambda item: item["path"], reverse=True)
    return {
        "required": [name for name in required if (root / name).exists()],
        "missing": [name for name in required if not (root / name).exists()],
        "task_handoffs": [item["path"] for item in handoffs if item["mentions_task"]],
        "latest_handoffs": [item["path"] for item in handoffs[:3]],
        "task_requirements": requirements,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--task", help="需求编号、唯一名称或完整执行语句")
    group.add_argument("--next", action="store_true", help="只读建议下一任务（默认）")
    args = parser.parse_args()
    try:
        proc = subprocess.run(["git", "-C", str(args.repo), "rev-parse", "--show-toplevel"],
                              capture_output=True, text=True, check=True)
        root = Path(proc.stdout.strip()).resolve()
        cmake = root / "CMakeLists.txt"
        if not cmake.exists() or not re.search(r"project\s*\(\s*CalcTabdd\b", cmake.read_text(encoding="utf-8"), re.I):
            raise ValueError("当前 Git 根目录不是 CalcTabdd 工程")
        tasks, decisions = parse_roadmap((root / "docs/roadmap.md").read_text(encoding="utf-8"))
        selected = resolve_task(args.task, tasks) if args.task else recommend(tasks)
        task_id = selected["id"] if selected else None
        decision_rows = [row for row in decisions if task_id in reference_ids(row.get("关联需求", ""))]
        output = {
            "mode": "read-only", "repo": str(root),
            "head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
            "git_status": subprocess.check_output(["git", "-C", str(root), "status", "--short", "--branch"], text=True).strip(),
            "task": selected,
            "dependency_hints": dependency_hints(selected, tasks) if selected else [],
            "related_decisions": decision_rows,
            "progress": {s: sum(t["status"] == s for t in tasks) for s in sorted({t["status"] for t in tasks})},
            "documents": collect_docs(root, task_id),
            "notes": [
                "输出仅用于查询，不授权执行、改状态或推送。",
                "候选排序优先进行中，再按优先级和编号；结合交接及路线图推进顺序判断。",
                "依赖采用保守过滤，条件性依赖、待验收依赖及产品决定需阅读原文。",
                "远端与 CI 未联网核对；进一步读取文档正文，不能仅凭文件名判断进展。",
            ],
        }
        print(json.dumps(output, ensure_ascii=False, indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
