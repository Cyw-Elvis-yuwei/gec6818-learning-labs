## 中文判断提示

- 当前状态：ready-for-agent
- 这是什么意思：本仓库的工程技能配置已经完成。
- 是否还需要继续讨论：不需要，除非切换 issue tracker 或领域文档布局。
- 建议下一步：按 `docs/agents/` 中的规则创建 PRD、issue 和架构文档。
- 还缺什么：正式领域词汇表将在领域术语确定后创建。

## Agent skills

### Issue tracker

Issues and PRDs live as local Markdown files under `.scratch/<feature-slug>/`; external PRs are not a request surface. See `docs/agents/issue-tracker.md`.

### Triage labels

The repo uses the default labels `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

The repo uses a single-context layout with root `CONTEXT.md` and `docs/adr/`. See `docs/agents/domain.md`.
