# Domain Docs

## 中文判断提示

- 当前状态：ready-for-agent
- 这是什么意思：项目采用 single-context 领域文档布局。
- 是否还需要继续讨论：不需要，除非项目以后拆成多个独立上下文。
- 建议下一步：领域术语确定后创建根目录 `CONTEXT.md`，架构决策放入 `docs/adr/`。
- 还缺什么：当前尚未形成正式领域词汇表；需要时再由 domain-modeling 技能创建。

## Layout

Single-context repo:

- `CONTEXT.md`：项目领域语言、核心概念和约束
- `docs/adr/`：架构决策记录
- `docs/agents/`：工程技能配置

## Consumer rules

- Before exploring, read `CONTEXT.md` if it exists.
- Read ADRs in `docs/adr/` that touch the work area.
- Use terms defined in `CONTEXT.md`.
- If a concept is not defined, record the gap instead of inventing synonyms.
- Surface conflicts with existing ADRs explicitly.
