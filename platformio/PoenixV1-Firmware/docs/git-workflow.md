# Phoenix Instrument Firmware – Git Workflow (Contributor Guide)

This document is the reference for how we manage topic branches, commits, and merges in the Phoenix Instrument firmware repo. Keep it handy when you start a new feature, bugfix, or refactor.

**Why we follow this workflow**

- Preserve small, atomic commits for traceability.
- Group related work behind descriptive merge commits created with `--no-ff` so the main branch tells a story.
- Keep multiple efforts in flight without clobbering local state by using `git worktree`.
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

## Parallel work with `git worktree`

Isolate each topic branch in its own checkout so experiments never collide.

1. Create the worktree and branch:
   ```bash
   git worktree add ../phoenix-fw-<short> -b <topic-branch> main
   ```
   Example: `git worktree add ../phoenix-fw-timeouts -b fix/channel-map-timeouts main`
3. Develop inside the new directory. Use `git add -p` and make frequent atomic commits.
4. Clean up after merge:
   ```bash
   git worktree remove ../phoenix-fw-timeouts
   git push origin :fix/channel-map-timeouts # optional once merged
   ```

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
  merges = log --merges --first-parent --oneline
```

- `git lg1` shows the mainline with merge boundaries only.
- `git merges` lists just the merge headlines (`FW: …`).
- `git lg` reveals the full commit graph when you need detail.

---

## Branch lifecycle checklist

1. **Create** topic branch via `git worktree add … -b <topic> main`.
2. **Develop** with atomic Conventional Commit-style messages.
3. **Sync** regularly: `git fetch --all --prune && git rebase main`.
4. **Self-review** with `git range-diff`, run required builds/tests, update docs.
5. **Merge** back to `main` using `git merge --no-ff <topic>` and an `FW:` merge headline.
6. **Tag** milestones when appropriate (for example, releases or major hardware validation).
7. **Clean** the worktree with `git worktree remove …` and prune the remote branch if finished.

---

## Quick reference

```bash
# Start parallel work
git worktree add ../phoenix-fw-<topic> -b <topic> main

# Atomic commit loop
git add -p && git commit -m "refactor(benchmark): move timing helpers"

# Refresh against main
git fetch --all --prune && git rebase main

# Merge with grouped headline
git switch main && git pull --ff-only
git merge --no-ff <topic> -m "FW: consolidate benchmark timing\n\nSummary:\n- ...\n"

# Inspect history
git lg1
```

This workflow keeps day-to-day commits expressive while preserving the long-form history structure already in use for Phoenix firmware. When onboarding new work, revisit this document alongside `docs/style-guide.md`, `docs/contributor-checklist.md`, and `docs/tdd-workflow.md` to ensure process alignment.
