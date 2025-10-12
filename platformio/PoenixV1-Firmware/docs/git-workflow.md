# Phoenix Instrument Firmware – Git Workflow (Contributor Guide)

This document is the reference for how we manage topic branches, commits, and merges in the Phoenix Instrument firmware repo. Keep it handy when you start a new feature, bugfix, or refactor.

**Why we follow this workflow**

- Preserve small, atomic commits for traceability.
- Group related work behind descriptive merge commits created with `--no-ff` so the main branch tells a story.
- Stay focused by using lightweight topic branches instead of juggling parallel worktrees.
- Maintain an easy-to-scan history via `git log --first-parent` and merge boundaries.
- Align topic-branch commits with Conventional Commit structure while retaining the repository-standard `FW: …` headline for merges into `main`.

---

## Before you start a branch

1. Sync your main worktree:
   ```bash
   git fetch --all --prune
   git switch main
   git pull --ff-only
   ```
2. Decide on the branch scope and name using the patterns below.

## Branch naming

Create topic branches that reflect the type of work:

- `feat/<scope>-<short-summary>`
- `fix/<scope>-<short-summary>`
- `refactor/<scope>-<short-summary>`
- `chore/<scope>-<short-summary>`

Examples: `refactor/benchmark-timeouts`, `feat/channel-map-sweeps`.

> Keep branch names lowercase with dashes for readability. Avoid `wip/` prefixes—prefer starting a fresh branch when the direction changes.

---

## Commit messages inside the topic branch

Follow Conventional Commit headers so the history explains *what* changed without reading the diff:

```
<type>(<scope>): <imperative summary>
```

Types: `feat`, `fix`, `refactor`, `perf`, `docs`, `test`, `chore`, `build`, `ci`.

- ✅ `refactor(benchmark): isolate timing from IO`
- ❌ `update code`

Each commit should represent one logical change and include any tightly coupled tests or configuration updates.

> When you squash fixups locally, preserve the Conventional Commit header in the resulting commit.

---

## Start a topic branch

Create a branch directly in this workspace and keep all work for a change inside that branch.

1. Ensure `main` is up to date (see the previous section).
2. Create and switch to a branch in one step:
   ```bash
   git switch -c <topic-branch> main
   ```
   Example: `git switch -c fix/channel-map-timeouts main`
3. Develop on that branch, using `git add -p` for focused staging and keeping commits atomic.
4. When ready to share, push it with `git push -u origin <topic-branch>`.

---

## Keeping the branch current

- Periodically rebase onto `main` to absorb upstream fixes:
  ```bash
  git fetch --all --prune
  git rebase main
  ```
- Review the delta before merge:
  ```bash
  git range-diff origin/main...HEAD
  ```
- Run the full validation expected by the repository (see `docs/contributor-checklist.md` and `docs/tdd-workflow.md`).

---

## Merging back to `main`

1. Switch to the primary worktree and refresh `main`:
   ```bash
   git switch main
   git pull --ff-only
   ```
2. Merge with `--no-ff` so the topic branch remains visible:
   ```bash
   git merge --no-ff <topic-branch> \
     -m "FW: <concise summary of the grouped change>\n\n<bulleted summary / risks / scope>"
   ```
   - The merge commit message keeps the established `FW:` prefix while the body summarizes scope, rationale, and risks.
   - If you prefer Conventional Commit semantics, you can mention the type/scope in the body (for example, `Summary: feat(benchmark): expose timeout flags`).
3. Push `main` and any relevant tags:
   ```bash
   git push
   git push --tags # optional, when tagging a release
   ```

> Never fast-forward `main`. The merge commit is the “headline” for the grouped work.

---

## History navigation aliases

Add these to `~/.gitconfig` for quick insight:

```ini
[alias]
  lg  = log --graph --decorate --oneline --abbrev-commit
  lg1 = log --first-parent --graph --decorate --oneline --abbrev-commit

   ```
- `git lg1` shows the mainline with merge boundaries only.
- `git merges` lists just the merge headlines (`FW: …`).
- `git lg` reveals the full commit graph when you need detail.

---

## Branch lifecycle checklist

1. **Create** the topic branch with `git switch -c <topic> main`.
2. **Develop** with atomic Conventional Commit-style messages.
3. **Sync** regularly: `git fetch --all --prune && git rebase main` while on the topic branch.
4. **Self-review** with `git range-diff`, run required builds/tests, update docs.
5. **Merge** back to `main` using `git merge --no-ff <topic>` and an `FW:` merge headline.
6. **Tag** milestones when appropriate (for example, releases or major hardware validation).
7. **Delete** the topic branch after merge: `git branch -d <topic>` locally and `git push origin :<topic>` remotely.

---

## Quick reference

```bash
# Start a topic branch
git switch -c <topic> main

# Atomic commit loop
git add -p && git commit -m "refactor(benchmark): move timing helpers"

# Refresh against main
git fetch --all --prune && git rebase main

# Merge with grouped headline
git switch main && git pull --ff-only
git merge --no-ff <topic> -m "FW: consolidate benchmark timing\n\nSummary:\n- ...\n"

# Inspect history
git lg1

