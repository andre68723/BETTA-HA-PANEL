<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Scripts and tools

[scripts/README.md](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/scripts/README.md) lists the manually invoked shell entry points.
[tools/README.md](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/tools/README.md) lists internal checks, generators and shared modules.
The [simulator](../simulator.md#test-pages), [test layouts](testing.md#test-layouts) and [build guide](build.md) cover
their task-specific commands. There is no additional dispatcher.

## Checks

The daily command is:

```bash
uv run --locked --only-group checks scripts/check.sh --changed
```

Use `--list` when the selection needs explanation, `--quick` for the fast stage and `--full --force` for the complete gate.

| Level | Coverage |
| --- | --- |
| `--quick` | Syntax, Node and Python tooling tests, HTML/translations, sizes and catalog/metadata checks; no SDL or C compilation |
| Standard | Quick responsibilities plus host tests for panel4/paneljc and simulator regressions/layout at text step 0 on all three sizes |
| `--full` | Standard plus Larger/Largest layout checks; CI always forces the full coverage of its selected group |
| Explicit `--build` | All five firmware variants; for one board use `scripts/build.sh <variant> build` |

`--changed` selects from each step's stamp inputs, including common runner/lock/tool metadata. There is no second
path-to-check table. It includes staged, unstaged, deleted, moved and non-ignored untracked paths relative to HEAD.
An unmapped path forces the selected level without cached passes. `--list` prints the actual selected steps and
inputs without executing checks. An empty diff selects none; existing uncommitted work still counts.
Standard `--changed` runs the strict docs build when its inputs match. `--group docs` checks that responsibility
explicitly. `--quick` deliberately omits that build. Local quick/changed modes are rejected under CI.

Successful checks print one summary with exit code, duration, passed/cached/unavailable/warning counts and log path.
Unavailable prerequisites are distinct from cached passes and fail under CI. The first failure stops by default,
prints its log path and rerun command; `--keep-going` collects later results. `--verbose` prints full per-step output.
`--help` exits successfully; invalid combinations exit 2. There is no `--json` interface without an actual consumer.

Each run gets a log directory under `${TMPDIR:-/tmp}/betta-check.*`; `BETTA_CHECK_LOG_DIR` overrides it. Passing steps
retain stamps in `build/check-stamps/`. Commands, input contents, locked metadata and tool versions decide reuse;
`tool-versions.log` records tool versions and the effective shared simulator clock. `--force` reruns everything. CI never reuses local stamps.

## Tool versions

[pyproject.toml](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/pyproject.toml) pins Python packages and declares
CI targets under `tool.betta.ci`. `check_web.py` checks workflow bootstrap values against them. Node is a separate
executable; newer local Node versions can run the same tests. ESP-IDF owns its own Python environment, separate
from the uv tooling environment. Do not install another SDK environment just to run these checks.
