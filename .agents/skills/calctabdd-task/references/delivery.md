# CalcTabdd 验证与 GitHub 交付参考

在实施/交付阶段读取。以下变量和路径为示例，先从当前仓库及缓存取得实际值；不要输出认证信息。

## 本地构建

先检查 `CMakeLists.txt`、`docs/testing.md` 和已有 `build/*/CMakeCache.txt`。
从缓存读取 Qt 路径、`CALCTABDD_HOST_SOURCE_DIR` 与可选的 `CALCTABDD_QSCINTILLA_LIBRARY`，验证路径存在且对应正确宿主。
没有已有构建时，在 `build/local` 配置：

```bash
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="<实际 Qt 前缀>" \
  -DCALCTABDD_HOST_SOURCE_DIR="<实际固定宿主源码目录>"
cmake --build build/local --parallel 4
ctest --test-dir build/local -N
ctest --test-dir build/local --output-on-failure --no-tests=error
```

`plugin_tests` 依赖宿主源码；可用的预编译静态库必须与宿主一致。否则让项目从固定源码编译，不静默取消该套件。
`*_tests.txt` 与 `Testing/Temporary/` 是本轮本地证据；比较时间和构建源码，不采信旧文件。
UI/宿主变更必要时保存截图；合成输入法事件和隔离 QScintilla 测试不代表用户完整宿主验收。

## 文档与提交

检查新增及修改文件的 Markdown 链接、锚点和表格。`git diff --check` 不包含未跟踪文件；对新文件另做检查或暂存后执行 `git diff --cached --check`。

```bash
git fetch origin
git status --short --branch
git diff --stat
git add <逐个列出的任务相关文件>
git diff --cached --check
git diff --cached --name-only
git commit -m "feat: <实际变化>"
git rev-parse HEAD
git push origin <已确认目标分支>
git ls-remote origin refs/heads/<已确认目标分支>
```

已有主分支以远端为基线；有分歧时保留他人提交，检查自己的修改并安全同步，不用 force push。
网络或沙箱失败按运行环境的授权路径处理，不改凭据或权限来规避限制。

## 识别本次流水线

有已认证 `gh` 时可用；否则使用可用 GitHub 连接器或允许的 GitHub REST API。不要为一次操作修改用户全局认证配置。

```bash
gh run list --repo <owner/repo> --branch <branch> --commit <完整SHA> --limit 10 \
  --json databaseId,headSha,workflowName,status,conclusion,url
gh run view <run-id> --repo <owner/repo> --json headSha,status,conclusion,jobs,url
gh run view <run-id> --repo <owner/repo> --log-failed
```

REST 对应读接口：`GET /repos/{owner}/{repo}/actions/runs?head_sha={sha}`、
`GET /repos/{owner}/{repo}/actions/runs/{run_id}/jobs`。
先取得具体 run/job ID，再读其日志。不要使用只查询 PR 触发运行的快捷工具来判断主分支 push 是否成功。

轮询间隔约 20–30 秒，尽量异步运行，等待期间持续提供有意义的进度。
只有匹配本次 SHA 的工作流 `completed/success`，且预期编译、三组测试、打包/发布作业均成功，才记录云端通过。
PR 或功能分支若按规则不发布包，记录预期跳过的发布范围；不要把意外取消或跳过必要测试当成通过。
每次修复推送后重新匹配新 SHA，保留前一个失败的根因与修复证据。

## 发布包

先读当前版本和流水线规则；版本变化时同步 release notes、安装说明和测试文档。
从实际 Release/Artifact 返回结果取得文件名和下载地址，不猜测私有文件 ID。
主分支流程生成预发布时，确认发布目标提交、`prerelease` 状态、ZIP 和 `.sha256` 两个资源，再下载到 `build/delivery-<sha>/`：

```bash
python3 scripts/verify-package.py <已下载的ZIP> --commit <对应完整SHA>
```

还要对照 `BUILD-INFO.json.workflow_run`，核对包内中文说明、手测记录、变更说明与该提交一致。
`real_host_manual_test` 是该次构建的状态，不覆盖已有用户的实机反馈，也不代表新功能已实机验收。
只有当前自动化证据成功后才记录对应层次 `passed`；发布资源不存在、下载失败或校验失败要继续诊断或交接阻塞。

## 交接收尾

功能提交 C 的验证证据写入一次文档收尾提交 D。D 也可能触发 Windows 构建与新预发布，继续核对 D。
交接主体记录 C，最终回复报告 D；不循环提交“上一份文档提交的 CI 已通过”。
用户明确要求只创建/修改技能时，按技能作者验证范围完成；不要因此擅自执行示例中的 EVO 产品任务。
