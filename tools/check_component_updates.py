#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Check whether newer versions of the firmware's components exist.

Covers what Settings > Firmware Update > Firmware Info shows (ESP-IDF, LVGL,
esp_lvgl_port, esp_lcd_jd9365, esp_lcd_touch, esp_hosted, esp_wifi_remote,
esp_websocket_client, littlefs) plus the other managed components (mqtt,
cjson, the Waveshare BSP) and the ESP32-C6 co-processor firmware.

For every variant it reads the resolved versions from the dependencies.lock
file, the version constraints from the variant's manifest
(main/idf_component.<variant>.yml), asks the ESP Component Registry for the
newest stable version and tells you whether an update fits the constraint
(`idf.py update-dependencies`) or needs a manifest change.

    tools/check_component_updates.py                # all variants
    tools/check_component_updates.py paneljc        # one variant
    tools/check_component_updates.py --json         # machine readable
    tools/check_component_updates.py --prerelease   # also offer pre-releases

Only the Python standard library is used; ESP-IDF itself is compared through
`git ls-remote` of the esp-idf repository.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = "https://components.espressif.com/api/components/{ns}/{name}"

# Each manifest has its own lock; JC V1/V2 share one manifest and dependency set.
VARIANTS = {
    "paneljc": ("dependencies.lock.paneljc", "main/idf_component.paneljc.yml"),
    "panel4": ("dependencies.lock", "main/idf_component.panel4.yml"),
    "panel10": ("dependencies.lock.panel10", "main/idf_component.panel10.yml"),
    "tab5": ("dependencies.lock.tab5", "main/idf_component.tab5.yml"),
}
DEFAULT_VARIANTS = ["paneljc", "panel4", "panel10", "tab5"]

# Components whose major version is followed on purpose (esp_hosted: the v2.x release line, and the ESP32-C6 firmware must match it):
# newer majors are not offered for them.
TRACK_MAJOR = {"espressif/esp_hosted": 2}

# Newest (major, minor) that runs on the P4 rev v1.x silicon of the supported boards. The prebuilt libesp_ipa.a of esp_ipa 2.3.0 and later contains
# RISC-V Zba/Zbb instructions (sh2add ...) that v1.x does not execute (illegal instruction in create_ipa_array, 2026-10-02); esp_video 2.4+ needs
# esp_ipa 2.3+, and esp_cam_sensor follows esp_video's line. A build for v3.x silicon (ESP-IDF cannot mix them) would lift the caps.
MAX_LINE = {"espressif/esp_ipa": (2, 2), "espressif/esp_video": (2, 3), "espressif/esp_cam_sensor": (2, 3)}

# Shown in the web editor's Firmware Info table.
FIRMWARE_INFO = {
    "lvgl/lvgl", "espressif/esp_lvgl_adapter", "espressif/esp_lvgl_port", "espressif/esp_lcd_jd9365",
    "espressif/esp_lcd_touch", "espressif/esp_hosted", "espressif/esp_wifi_remote",
    "espressif/esp_websocket_client", "joltwallet/littlefs",
    "espressif/esp_video", "espressif/esp_ipa", "espressif/esp_cam_sensor", "espressif/esp_sccb_intf",
}

# Components copied into components/ (they show up in the lock file as "local"):
# name in the lock file -> registry package to compare against.
VENDORED = {"esp_cam_sensor": "espressif/esp_cam_sensor"}

PACKAGE_RE = re.compile(r"^  ([A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)?):\s*$")
VERSION_RE = re.compile(r"^    version:\s*['\"]?([^'\"#]+?)['\"]?\s*$")
SOURCE_RE = re.compile(r"^      type:\s*(\w+)")


def parse_lock(path):
    """{pkg: (version, source_type)} for the top-level packages of a lock file."""
    out, pkg, ver, src = {}, None, None, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = PACKAGE_RE.match(line)
            if m:
                if pkg and ver:
                    out[pkg] = (ver, src)
                pkg, ver, src = m.group(1), None, None
                continue
            if pkg is None:
                continue
            m = VERSION_RE.match(line)
            if m and ver is None:
                ver = m.group(1).strip()
            m = SOURCE_RE.match(line)
            if m:
                src = m.group(1)
    if pkg and ver:
        out[pkg] = (ver, src)
    return out


def parse_manifest(path):
    """{pkg: version constraint} from the simple manifest layout used here."""
    out, pkg = {}, None
    in_deps = False
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#")[0].rstrip()
            if not line.strip():
                continue
            if line.startswith("dependencies:"):
                in_deps = True
                continue
            if not in_deps:
                continue
            m = re.match(r"^  ([A-Za-z0-9_.\-]+/[A-Za-z0-9_.\-]+):\s*(.*)$", line)
            if m:
                pkg = m.group(1)
                rest = m.group(2).strip().strip('"\'')
                if rest:
                    out[pkg] = rest
                continue
            m = re.match(r"^    version:\s*['\"]?([^'\"]+?)['\"]?\s*$", line)
            if m and pkg:
                out[pkg] = m.group(1)
    return out


def vkey(v):
    """Sort key for 'x.y.z', 'x.y.z~N' and 'x.y.z-rc1'; pre-releases sort below the release."""
    base, _, rev = v.partition("~")
    core, _, pre = base.partition("-")
    nums = tuple(int(p) if p.isdigit() else 0 for p in re.split(r"[.]", core))
    nums += (0,) * (4 - len(nums))
    return (nums, 0 if pre else 1, pre, int(rev) if rev.isdigit() else 0)


def is_pre(v):
    return "-" in v.partition("~")[0]


def core(v):
    return tuple(int(p) if p.isdigit() else 0 for p in re.split(r"[.]", v.partition("~")[0].partition("-")[0]))


def satisfies(version, spec):
    """Minimal semver ranges: *, x.y.z, ^, ~, >=, >, <=, <, ==, comma-joined."""
    v = core(version)
    for part in [p.strip() for p in spec.split(",") if p.strip()]:
        if part == "*":
            continue
        m = re.match(r"^(\^|~|>=|<=|==|>|<)?\s*([0-9][0-9A-Za-z.\-]*)", part)
        if not m:
            return True
        op, base = m.group(1) or "==", core(m.group(2))
        base = base + (0,) * (3 - len(base))
        v3, b3 = (v + (0,) * 3)[:3], base[:3]
        if op == "==":
            ok = v3 == b3
        elif op == ">=":
            ok = v3 >= b3
        elif op == ">":
            ok = v3 > b3
        elif op == "<=":
            ok = v3 <= b3
        elif op == "<":
            ok = v3 < b3
        elif op == "^":
            upper = (b3[0] + 1, 0, 0) if b3[0] > 0 else ((0, b3[1] + 1, 0) if b3[1] > 0 else (0, 0, b3[2] + 1))
            ok = b3 <= v3 < upper
        else:  # ~
            ok = b3 <= v3 < (b3[0], b3[1] + 1, 0)
        if not ok:
            return False
    return True


# package -> {version: changelog URL}, filled by registry_versions() from each version's `docs.changelog`
CHANGELOGS = {}


def registry_versions(pkg, prerelease):
    ns, name = pkg.split("/")
    req = urllib.request.Request(REGISTRY.format(ns=ns, name=name), headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = json.load(r)
    versions = [v["version"] for v in data.get("versions", []) if not v.get("yanked_at")]
    CHANGELOGS[pkg] = {v["version"]: (v.get("docs") or {}).get("changelog") for v in data.get("versions", [])}
    if not prerelease:
        versions = [v for v in versions if not is_pre(v)]
    return sorted(versions, key=vkey)


def idf_status(prerelease):
    """(local version, latest stable tag, latest patch in the local minor)."""
    local = None
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:  # not sourced: fall back to a checkout in the home directory
        import glob
        candidates = sorted(glob.glob(os.path.expanduser("~/esp-idf*")))
        idf_path = candidates[-1] if candidates else None
    if idf_path and os.path.isdir(idf_path):
        try:
            local = subprocess.run(["git", "-C", idf_path, "describe", "--tags", "--always"],
                                   capture_output=True, text=True, timeout=20).stdout.strip() or None
        except Exception:
            pass
    tags = []
    try:
        out = subprocess.run(["git", "ls-remote", "--tags", "https://github.com/espressif/esp-idf", "refs/tags/v*"],
                             capture_output=True, text=True, timeout=60).stdout
        for line in out.splitlines():
            tag = line.split("refs/tags/")[-1]
            if tag.endswith("^{}"):
                continue
            if not prerelease and re.search(r"-(beta|rc|dev)", tag):
                continue
            if re.match(r"^v\d+\.\d+(\.\d+)?", tag):
                tags.append(tag)
    except Exception:
        pass
    tags.sort(key=lambda t: vkey(t.lstrip("v")))
    latest = tags[-1] if tags else None
    minor = None
    if local:
        m = re.match(r"^v(\d+\.\d+)", local)
        if m:
            same = [t for t in tags if t.startswith("v" + m.group(1))]
            minor = same[-1] if same else None
    return local, latest, minor


def c6_firmware():
    rel = os.path.join(ROOT, "release")
    found = []
    if os.path.isdir(rel):
        for n in os.listdir(rel):
            m = re.match(r"network_adapter_esp32c6_([0-9.]+)\.bin$", n)
            if m:
                found.append(m.group(1))
    found.sort(key=vkey)
    return found[-1] if found else None


def lock_seems_stale(variant):
    """True if the variant lock misses a package requested by its manifest."""
    lock_rel, manifest_rel = VARIANTS[variant]
    lock = parse_lock(os.path.join(ROOT, lock_rel))
    manifest = parse_manifest(os.path.join(ROOT, manifest_rel))
    return any(pkg not in lock for pkg in manifest if pkg != "idf")


def check_variant(variant, prerelease, cache):
    lock_rel, manifest_rel = VARIANTS[variant]
    lock = parse_lock(os.path.join(ROOT, lock_rel))
    manifest = parse_manifest(os.path.join(ROOT, manifest_rel))
    rows = []
    for pkg in sorted(lock):
        ver, src = lock[pkg]
        vendored = VENDORED.get(pkg) if src == "local" else None
        if src != "service" and not vendored:
            continue
        if vendored:
            pkg_display, pkg = pkg, vendored
            manifest.setdefault(pkg, None)
        constraint = manifest.get(pkg)  # None: pulled in by another component
        try:
            if pkg not in cache:
                cache[pkg] = registry_versions(pkg, prerelease)
            versions = cache[pkg]
            if pkg in TRACK_MAJOR:
                versions = [v for v in versions if core(v)[0] == TRACK_MAJOR[pkg]]
            if pkg in MAX_LINE:
                versions = [v for v in versions if core(v)[:2] <= MAX_LINE[pkg]]
        except Exception as exc:  # noqa: BLE001
            rows.append({"package": pkg, "locked": ver, "constraint": constraint, "latest": None,
                         "status": "registry error: %s" % exc, "in_firmware_info": pkg in FIRMWARE_INFO})
            continue
        latest = versions[-1] if versions else None
        fits = None
        if latest:
            fits = satisfies(latest, constraint) if constraint else None
        if latest is None or vkey(latest) <= vkey(ver):
            status = "up to date"
        elif core(latest) == core(ver) and not is_pre(latest):
            status = "same version, newer registry revision (%s)" % latest
        elif constraint is None:
            status = "newer (indirect: updates with its parent)"
        elif fits:
            status = "UPDATE within constraint"
        else:
            status = "NEWER, manifest change needed"
        if vendored and status != "up to date":
            status = "vendored copy in components/, newer registry version (update by hand)"
        # The newest version of the same major.minor line: a patch release the manifest pin would have to be moved to, which "latest"
        # hides when a new major or minor exists (a pinned esp_hosted 3.0.10 next to a later 3.x minor).
        same_line = [v for v in versions if core(v)[:2] == core(ver)[:2] and vkey(v) > vkey(ver)]
        line_latest = same_line[-1] if same_line else None
        if line_latest and line_latest != latest and status != "up to date":
            status += " | patch in your line: %s" % line_latest
        # The registry keeps the CHANGELOG.md of every published version; the newest one lists everything since the locked version.
        changelog = (CHANGELOGS.get(pkg, {}).get(latest) if latest and status != "up to date" else None)
        rows.append({"package": pkg, "locked": ver, "constraint": constraint, "latest": latest, "line_latest": line_latest,
                     "status": status, "in_firmware_info": pkg in FIRMWARE_INFO, "changelog": changelog})
    return rows


def print_table(title, rows):
    print("\n== %s" % title)
    head = ("Package", "Locked", "Constraint", "Latest", "Same line", "Status", "Info")
    data = [(r["package"], r["locked"], r["constraint"] or "-", r["latest"] or "?", r.get("line_latest") or "-", r["status"],
             "yes" if r["in_firmware_info"] else "no") for r in rows]
    widths = [max(len(str(x[i])) for x in [head] + data) for i in range(len(head))]
    for i, row in enumerate([head] + data):
        print("  ".join(str(c).ljust(widths[j]) for j, c in enumerate(row)))
        if i == 0:
            print("  ".join("-" * w for w in widths))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("variants", nargs="*", help="variants to check (default: %s)" % ", ".join(DEFAULT_VARIANTS))
    ap.add_argument("--json", action="store_true", help="print JSON instead of tables")
    ap.add_argument("--prerelease", action="store_true", help="also consider pre-release versions")
    ap.add_argument("--fail-on-updates", action="store_true", help="exit 1 if an update fits the constraints")
    args = ap.parse_args()

    variants = args.variants or DEFAULT_VARIANTS
    for v in variants:
        if v not in VARIANTS:
            ap.error("unknown variant %r (known: %s)" % (v, ", ".join(VARIANTS)))

    for v in variants:
        if lock_seems_stale(v):
            print("warning: %s does not match the %r manifest -- run scripts/build.sh %s reconfigure build "
                  "before comparing component versions" % (VARIANTS[v][0], v, v), file=sys.stderr)

    cache, result = {}, {"variants": {}}
    for v in variants:
        result["variants"][v] = check_variant(v, args.prerelease, cache)

    local, latest, minor = idf_status(args.prerelease)
    idf = {"local": local, "latest": latest, "latest_in_minor": minor}
    if local and latest:
        idf["status"] = "up to date" if local.lstrip("v").split("-")[0] == latest.lstrip("v") else (
            "patch available (%s)" % minor if minor and minor.lstrip("v") != local.lstrip("v").split("-")[0]
            else "newer release available (%s)" % latest)
    result["esp_idf"] = idf

    c6 = c6_firmware()
    hosted = {}
    for v in variants:
        for r in result["variants"][v]:
            if r["package"] == "espressif/esp_hosted":
                hosted[v] = r
    c6_info = {"bundled": c6}
    if hosted:
        newest = max((r["latest"] for r in hosted.values() if r["latest"]), key=vkey, default=None)
        c6_info["latest_esp_hosted"] = newest
        c6_info["status"] = ("up to date" if c6 and newest and vkey(c6) >= vkey(newest)
                             else "newer esp_hosted (%s): update the C6 image with the host driver" % newest)
    result["esp32_c6_firmware"] = c6_info

    if args.json:
        json.dump(result, sys.stdout, indent=2)
        print()
    else:
        print("ESP-IDF: local %s | latest stable %s | %s" % (local or "unknown (IDF_PATH not set)",
              latest or "?", idf.get("status", "not compared")))
        print("ESP32-C6 firmware bundled in release/: %s | %s" % (c6 or "none found", c6_info.get("status", "-")))
        for v in variants:
            print_table("Variant %s (%s, %s)" % (v, VARIANTS[v][0], VARIANTS[v][1]), result["variants"][v])
        links = []
        for v in variants:
            for r in result["variants"][v]:
                if r.get("changelog") and not r["status"].startswith(("newer (indirect", "same version")) and (r["package"], r["latest"]) not in links:
                    links.append((r["package"], r["latest"], r["locked"], r["changelog"]))
        if links:
            print("\nChangelogs of the updates that matter (everything since the locked version is in the file):")
            for pkg, latest, locked, url in sorted(set(links)):
                print("  %s %s -> %s: %s" % (pkg, locked, latest, url))
        print("\nNotes:")
        print("  Info = shown in the web editor's Firmware Info table.")
        print("  'UPDATE within constraint': idf.py -B <build dir> update-dependencies (or delete the lock entry).")
        print("  'NEWER, manifest change needed': raise the constraint in main/idf_component.<variant>.yml first.")
        print("  esp_hosted and the ESP32-C6 firmware (release/network_adapter_esp32c6_<ver>.bin) must match.")
        print("  LVGL bundles ThorVG, lodepng, tjpgd and stb_image; they update with LVGL.")

    if args.fail_on_updates and any(r["status"].startswith("UPDATE") for v in variants for r in result["variants"][v]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
