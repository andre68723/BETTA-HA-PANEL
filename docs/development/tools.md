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

## Home Assistant Template export

If HA works in the browser but an API client or connector cannot reach it, the owner
can export a diagnostic snapshot through **Developer tools → Template**
(`/developer-tools/template`; some versions label the tools menu differently).
This needs no token or helper. Paste the template below and copy the rendered JSON
result, not the template subscription list. Rendering only reads data; it does not
save configuration or call services. See HA's [templating guide](https://www.home-assistant.io/docs/templating/).

Replace `entity_ids` with exact IDs from the entity settings or States tool.
Set `include_device_sensors` to `false` to export only those entities; `true` also
includes sensor siblings from their devices. Use one known entity per device to
inspect several devices. The example IDs are public synthetic placeholders.

```jinja
{% set entity_ids = ['sensor.demo_room_humidity', 'sensor.demo_air_co2'] %}
{% set include_device_sensors = true %}
{% set ns = namespace(ids=entity_ids, rows=[]) %}

{% if include_device_sensors %}
  {% for eid in entity_ids %}
    {% set did = device_id(eid) %}
    {% if did %}
      {% for sibling in device_entities(did) %}
        {% if sibling.startswith('sensor.') and sibling not in ns.ids %}
          {% set ns.ids = ns.ids + [sibling] %}
        {% endif %}
      {% endfor %}
    {% endif %}
  {% endfor %}
{% endif %}

{% for eid in ns.ids | unique | sort %}
  {% set did = device_id(eid) %}
  {% set ns.rows = ns.rows + [{
    'entity_id': eid,
    'friendly_name': state_attr(eid, 'friendly_name'),
    'device_id': did,
    'device_name': device_attr(did, 'name') if did else none,
    'device_name_by_user': device_attr(did, 'name_by_user') if did else none,
    'manufacturer': device_attr(did, 'manufacturer') if did else none,
    'model': device_attr(did, 'model') if did else none,
    'state': states(eid),
    'has_value': has_value(eid),
    'unit': state_attr(eid, 'unit_of_measurement'),
    'device_class': state_attr(eid, 'device_class'),
    'state_class': state_attr(eid, 'state_class'),
    'options': state_attr(eid, 'options')
  }] %}
{% endfor %}

{{ {'captured_at': now().isoformat(), 'entities': ns.rows}
   | to_json(pretty_print=True) }}
```

Always guard `device_attr()` and `device_entities()` with a valid device ID.
Standalone sensors have none; an unguarded call raises
`TemplateError: Must provide a device or entity ID`.
Exact IDs avoid ambiguous room/device names; device membership establishes siblings,
whereas a shared area or similar names do not. Additional attributes can be exported
explicitly with `state_attr(eid, 'attribute_name')` when needed.

`has_value` distinguishes a usable state from `unknown`/`unavailable`; it does not
validate numeric format, units or device freshness. `unknown` can also mean no state
exists for the ID. `captured_at` is export time, not measurement time. This export
does not establish registry disabled/hidden flags, integration identity or every device
capability; inspect those on the device/entity settings pages when relevant. Preserve
reported units and enum options literally; option order is not a severity scale.
Review household names and values before sharing. Record the output as an
owner-supplied HA UI snapshot, not a successful connector read or panel hardware test.

## Tool versions

[pyproject.toml](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/pyproject.toml) pins Python packages and declares
CI targets under `tool.betta.ci`. `check_web.py` checks workflow bootstrap values against them. Node is a separate
executable; newer local Node versions can run the same tests. ESP-IDF owns its own Python environment, separate
from the uv tooling environment. Do not install another SDK environment just to run these checks.

CI removes the Azure entry from the runner's APT mirror list, preferring Ubuntu's archive with the remaining
fallbacks. Package installs have bounded retries and a six-minute step limit. The host group installs system
cJSON and pkg-config; simulator groups install SDL2 and pinned CMake from PyPI. The web group needs no APT
packages. Pillow comes from the locked uv environment. The documentation workflow installs pngquant through APT.
