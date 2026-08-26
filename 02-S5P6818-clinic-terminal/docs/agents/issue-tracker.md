# Issue tracker: Local Markdown

## 中文判断提示

- 当前状态：ready-for-agent
- 这是什么意思：本仓库使用本地 Markdown 文件记录 PRD 和实现任务。
- 是否还需要继续讨论：不需要，除非以后切换到 GitHub、GitLab 或其他平台。
- 建议下一步：在 `.scratch/<feature-slug>/` 下创建项目工作项。
- 还缺什么：没有。

Issues and PRDs for this repo live as markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The PRD is `.scratch/<feature-slug>/PRD.md`
- Implementation issues are `.scratch/<feature-slug>/issues/<NN>-<slug>.md`
- Triage state is recorded as a `Status:` line near the top of each issue file.
- Comments and conversation history append under `## Comments`.
- This repository has no external PR request surface.

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/`.
