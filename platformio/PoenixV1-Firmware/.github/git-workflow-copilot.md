# Phoenix Firmware Git Workflow – Copilot Quick Reference

This file distills the contributor workflow so AI assistants stay aligned with how the team manages branches and commits.

## Branching
- New work happens on a topic branch forked from the latest `main`.
- Names follow `type/scope-short-summary` (e.g., `feat/channel-map-timeouts`).
- Create the branch directly from `main` and work there: `git switch -c <topic> main`.

## Commit Style
- Atomic commits with Conventional Commit headers: `<type>(<scope>): <imperative summary>`.
- Types allowed: `feat`, `fix`, `refactor`, `perf`, `docs`, `test`, `chore`, `build`, `ci`.
- Include tightly coupled tests/config/docs inside the same commit.
  
> Use Conventional Commit headers for commits inside the topic branch. Reserve the `FW: <headline>` format for the merge commit into `main`.

## Syncing During Development
- Expect periodic rebases onto `main`: `git fetch --all --prune && git rebase main`.
- Suggest `git range-diff origin/main...HEAD` for self-review before merge.
- Run the validation expected by the repo before merging (see `docs/contributor-checklist.md` and `docs/tdd-workflow.md`).

## Merge Policy
- `main` is protected: no force pushes, and all merges use `git merge --no-ff <topic>`.
- Merge commit message format stays `FW: <headline>` followed by a summary block outlining scope, rationale, and risks.
- After merge, delete the topic branch locally and remotely (`git branch -d <topic>`; optionally `git push origin :<topic>`). Tags are optional for releases.
  
> Never fast-forward `main`. The merge commit is the “headline” for the grouped work.


## Assistant Guidance
- When planning tasks, remind users to consult `docs/git-workflow.md` for full instructions.
- If suggesting git commands, keep them compatible with the workflow above (e.g., prefer `git merge --no-ff`, and avoid encouraging parallel worktrees by default).
- When generating commit messages, use Conventional Commit headers for topic-branch commits and reserve the `FW: …` headline for the merge commit into `main`.
