#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Rudimentary memory/stability stress test: rapid page navigation, repeated Home Assistant
entity-discovery calls (Room/House import's own endpoint) and -- since 2026-09-24 -- a repeated
concurrent web-editor page load, the load patterns a maintainer's own usage produces (poking around
the touchscreen and the House/Room import menus; opening or reloading the editor in a browser).

Grew out of chasing two separate real incidents on a paneljc panel, both on 2026-09-24 -- see
docs/development/learnings.md ("Memory and heap") and the "Working practices and
learnings" section of docs/development/learnings.md for what each investigation did and did not find:

  1. SDIO transport packet drops (`H_SDIO_DRV: mempool OOM`) and a slow Room Import under rapid
     navigation + discovery load -- what --no-webui-load below reproduces.
  2. A *different* bug entirely, found the same evening: this script's own navigation+discovery
     load never triggered it, because it only ever has (at most) two small requests in flight at
     once. The real trigger needed several genuinely concurrent, sizeable HTTP responses, exactly
     what a browser produces by opening a handful of connections at once to load the editor's HTML/
     CSS/JS -- reproduced instead with plain concurrent `curl` calls, and only after that with this
     script's own --webui-load addition. Both bugs sat on the same SDIO transport queue but needed
     unrelated load shapes to surface, which is why one script's original load pattern could not
     find the other's bug -- a second, deliberately different load shape was necessary, not a tuning
     knob on the first.

Also reports where the CPU time went: `GET /api/version?tasks=1` carries each task's run time, so the difference of the
reads before and after the load gives every task's share of its core (`core`, `run_time`, `total_time`; firmware before
2026-10-29 lacks them and only the idle-based `cpu.load_percent` is printed).

Reports `GET /api/version`'s `memory` object before, during (at each poll interval) and after a
cool-down pause once the load stops, so a real leak (does not recover after the load stops) can be
told apart from a one-time allocation (a persistent cache, drops once then stays flat).

Read-mostly and safe to run against a real panel: `POST /api/navigate` only switches the page
already on screen (matches manually tapping between tabs), never touches layout or settings; the
discovery calls and the web-editor asset fetches are all plain GETs. Does not touch USB/serial at
all, since opening the serial port can reset the board even with dtr/rts held low (docs/development/learnings.md);
everything here goes over the network, like the rest of this project's tooling.

    tools/stress_test.py <panel-ip>                       # 45 s, alternating domains (forces
                                                            # abort+restart of discovery every call,
                                                            # the more adversarial case), plus a
                                                            # concurrent web-editor load every 10 s
    tools/stress_test.py <panel-ip> --webui-concurrency 10 --webui-copies 3 # several editor opens at once
    tools/stress_test.py <panel-ip> --duration 90
    tools/stress_test.py <panel-ip> --domains room         # repeat one domain only, no aborts
    tools/stress_test.py <panel-ip> --no-navigate          # discovery + web-editor load only
    tools/stress_test.py <panel-ip> --no-discovery         # navigation + web-editor load only
    tools/stress_test.py <panel-ip> --no-webui-load        # navigation + discovery only (the
                                                            # original 2026-09-24 load pattern)

Only the Python standard library is used.
"""
import argparse
import gzip
import http.client
import json
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

POLL_INTERVAL_S = 15
COOLDOWN_S = 20


def get_json(base, path, timeout=15):
    with urllib.request.urlopen(f"{base}{path}", timeout=timeout) as resp:
        return json.loads(resp.read())


def post_json(base, path, payload, timeout=5):
    req = urllib.request.Request(
        f"{base}{path}",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def fetch_pages(base):
    layout = get_json(base, "/api/layout")
    pages = [p["id"] for p in layout.get("pages", []) if p.get("id")]
    if not pages:
        raise SystemExit("panel has no pages to navigate between")
    return pages


def fetch_text(base, path, timeout=15):
    # The panel serves its editor files gzip-compressed regardless of the request's
    # Accept-Encoding (docs/editor-source.md); urllib does not decompress automatically.
    with urllib.request.urlopen(f"{base}{path}", timeout=timeout) as resp:
        body = resp.read()
        if resp.headers.get("Content-Encoding", "").lower() == "gzip":
            body = gzip.decompress(body)
        return body.decode("utf-8", errors="replace")


def discover_webui_assets(base):
    """The real set of files a browser loads for GET /, parsed out of index.html itself (css
    href="...", script src="...") so this never drifts from whatever the editor actually ships --
    the whole point is to reproduce a real browser page load, not a guess at one."""
    try:
        html = fetch_text(base, "/")
    except (urllib.error.URLError, OSError) as exc:
        raise SystemExit(f"could not fetch / to discover web-editor assets: {exc}") from exc
    hrefs = re.findall(r'href="([^"]+\.css)"', html)
    srcs = re.findall(r'src="([^"]+\.js)"', html)
    assets = ["/"] + [p for p in (hrefs + srcs) if p.startswith("/")]
    if len(assets) < 3:
        raise SystemExit("could not discover CSS and JS assets in editor index.html")
    return assets


def webui_burst(base, assets, concurrency, timeout=15):
    """Fetches the assets with bounded concurrency, like a browser loading one page,
    instead of navigate_loop/discovery_loop's one-request-at-a-time pattern -- see the module
    docstring for why that distinction is exactly what the 2026-09-24 SDIO queue bug needed to
    surface at all."""
    results = []
    endpoint = urllib.parse.urlsplit(base)

    def one(path):
        t0 = time.time()
        conn = http.client.HTTPConnection(endpoint.hostname, endpoint.port or 80, timeout=timeout)
        stage = "connect"
        try:
            conn.connect()
            stage = "request"
            conn.request("GET", path)
            stage = "headers"
            resp = conn.getresponse()
            stage = "body"
            body = resp.read()
            if resp.status != 200:
                return path, False, len(body), time.time() - t0, f"HTTP {resp.status}"
            return path, True, len(body), time.time() - t0, ""
        except (http.client.HTTPException, OSError) as exc:
            return path, False, 0, time.time() - t0, f"{stage}: {exc}"
        finally:
            conn.close()

    with ThreadPoolExecutor(max_workers=min(concurrency, len(assets))) as pool:
        for path, ok, size, dt, error in pool.map(one, assets):
            results.append((path, ok, size, dt, error))
    return results


def webui_load_loop(base, assets, stop, interval_s, concurrency, stats):
    while not stop.is_set():
        t0 = time.time()
        results = webui_burst(base, assets, concurrency)
        dt = time.time() - t0
        failed = [(path, error) for path, ok, _, _, error in results if not ok]
        stats["webui_bursts"] += 1
        stats["webui_failed_assets"] += len(failed)
        if failed:
            print(f"[webui-load] {dt:.2f}s total, FAILED: {failed} (of {len(assets)})")
        else:
            slowest = max(results, key=lambda item: item[3])
            print(f"[webui-load] {dt:.2f}s total, all {len(assets)} ok, slowest {slowest[0]} ({slowest[3]:.2f}s)")
        stop.wait(interval_s)


def print_memory(base, label):
    try:
        mem = get_json(base, "/api/version")["memory"]
    except (urllib.error.URLError, TimeoutError, KeyError, json.JSONDecodeError) as exc:
        print(f"[{label}] could not read memory: {exc}")
        return None
    print(
        f"[{label}] internal free={mem['free_heap_internal']} "
        f"min_free={mem['min_free_heap_internal']} psram_free={mem['free_psram']}"
    )
    return mem


def cpu_snapshot(base):
    """Per-task run times from /api/version?tasks=1, or None when the firmware predates them."""
    try:
        data = get_json(base, "/api/version?tasks=1")
    except (urllib.error.URLError, OSError, json.JSONDecodeError):
        return None
    if "total_time" not in data:
        return None
    return {"total": data["total_time"], "tasks": {t["name"]: t for t in data["tasks"] if "run_time" in t},
            "load": data.get("cpu", {}).get("load_percent")}


def print_cpu(label, first, last, top=6):
    """Share of one core's time per task between two snapshots, busiest first, idle tasks last."""
    total = last["total"] - first["total"]
    if total <= 0:
        return
    rows = []
    for name, task in last["tasks"].items():
        before = first["tasks"].get(name)
        if before is not None:
            rows.append(((task["run_time"] - before["run_time"]) * 100.0 / total, name, task.get("core", -1)))
    rows.sort(reverse=True)
    idle = {name: share for share, name, _ in rows if name.startswith("IDLE")}
    busy = [f"{name} c{core} {share:.0f}%" for share, name, core in rows if not name.startswith("IDLE")][:top]
    print(f"[cpu {label}] " + ", ".join(f"{n} {100 - v:.0f}% busy" for n, v in sorted(idle.items())) +
          " | " + ", ".join(busy))


def navigate_loop(base, pages, stop, interval_s, stats):
    i = 0
    while not stop.is_set():
        page = pages[i % len(pages)]
        i += 1
        try:
            post_json(base, "/api/navigate", {"page": page})
        except (urllib.error.URLError, OSError) as exc:
            stats["nav_errors"] += 1
            print(f"[nav] error: {exc}")
        stop.wait(interval_s)


def discovery_loop(base, domains, stop, interval_s, stats):
    i = 0
    while not stop.is_set():
        domain = domains[i % len(domains)]
        i += 1
        t0 = time.time()
        try:
            get_json(base, f"/api/ha/light_entities?domain={domain}")
            dt = time.time() - t0
            print(f"[discovery:{domain}] {dt:.2f}s")
        except (urllib.error.URLError, OSError) as exc:
            stats["discovery_errors"] += 1
            print(f"[discovery:{domain}] error: {exc}")
        stop.wait(interval_s)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", help="panel IP or hostname")
    parser.add_argument("--duration", type=int, default=45, help="seconds of load (default 45)")
    parser.add_argument(
        "--domains",
        default="room,any",
        help='comma-separated discovery domains to cycle through (default "room,any"; a single '
        "domain never aborts an in-flight discovery, several domains do on every call)",
    )
    parser.add_argument("--no-navigate", action="store_true", help="skip the page-navigation load")
    parser.add_argument("--no-discovery", action="store_true", help="skip the entity-discovery load")
    parser.add_argument("--no-webui-load", action="store_true",
        help="skip the concurrent web-editor page load (a burst of GETs for /, styles.css and the "
        "js/*.js files, discovered from index.html, with browser-like concurrency -- the "
        "2026-09-24 SDIO queue freeze needed this, not the navigate/discovery load pattern)")
    parser.add_argument("--nav-interval", type=float, default=0.35, help="seconds between page switches (default 0.35)")
    parser.add_argument("--discovery-interval", type=float, default=1.0, help="seconds between discovery calls (default 1.0)")
    parser.add_argument("--webui-load-interval", type=float, default=10.0,
        help="seconds between concurrent web-editor page load bursts (default 10.0)")
    parser.add_argument("--webui-concurrency", type=int, default=6,
        help="maximum simultaneous editor asset requests (default 6; use 10 for heavier load; "
        "the panel HTTP server allows at most 12 open sockets)")
    parser.add_argument("--webui-copies", type=int, default=1,
        help="fetch each editor asset this many times per burst to simulate multiple concurrent editor opens "
        "(default 1; use 2 or 3 with bundled editor assets)")
    parser.add_argument("--min-internal-watermark", type=int, default=8192,
        help="fail if the since-boot minimum free internal heap is below this many bytes "
        "(default 8192; 0 disables this guard)")
    args = parser.parse_args()
    if args.webui_concurrency < 1 or args.webui_copies < 1 or args.duration < 1 or args.min_internal_watermark < 0:
        parser.error("--webui-concurrency, --webui-copies and --duration must be positive; "
                     "--min-internal-watermark must be nonnegative")

    base = f"http://{args.host}"
    domains = [d.strip() for d in args.domains.split(",") if d.strip()]

    pages = [] if args.no_navigate else fetch_pages(base)
    if not args.no_navigate:
        print(f"navigating between pages: {pages}")
    if not args.no_discovery:
        print(f"discovery domains: {domains}")
    webui_assets = [] if args.no_webui_load else discover_webui_assets(base)
    if not args.no_webui_load:
        print(f"web-editor load burst, {len(webui_assets)} assets x {args.webui_copies} copies, at most "
              f"{args.webui_concurrency} concurrent requests: {webui_assets}")
        webui_assets *= args.webui_copies

    cpu_before = cpu_snapshot(base)
    before_mem = print_memory(base, "before")
    memory_ok = before_mem is not None
    lowest_internal = before_mem.get("min_free_heap_internal") if before_mem else None

    stop = threading.Event()
    stats = {"webui_bursts": 0, "webui_failed_assets": 0, "nav_errors": 0, "discovery_errors": 0}
    threads = []
    if not args.no_navigate:
        threads.append(threading.Thread(target=navigate_loop,
            args=(base, pages, stop, args.nav_interval, stats), daemon=True))
    if not args.no_discovery:
        threads.append(threading.Thread(target=discovery_loop,
            args=(base, domains, stop, args.discovery_interval, stats), daemon=True))
    if not args.no_webui_load:
        threads.append(threading.Thread(
            target=webui_load_loop,
            args=(base, webui_assets, stop, args.webui_load_interval, args.webui_concurrency, stats), daemon=True))
    if not threads:
        raise SystemExit("--no-navigate, --no-discovery and --no-webui-load all given, nothing to do")

    for t in threads:
        t.start()

    elapsed = 0
    while elapsed < args.duration:
        wait = min(POLL_INTERVAL_S, args.duration - elapsed)
        time.sleep(wait)
        elapsed += wait
        mem = print_memory(base, f"t={elapsed}s")
        memory_ok &= mem is not None
        if mem is not None:
            lowest_internal = min(lowest_internal, mem["min_free_heap_internal"]) if lowest_internal is not None else mem["min_free_heap_internal"]

    cpu_during = cpu_snapshot(base)
    if cpu_before and cpu_during:
        print_cpu("load", cpu_before, cpu_during)
    stop.set()
    for t in threads:
        # Each request has a timeout; finish every in-flight request before the
        # cool-down measurement, otherwise a slow burst can still be running.
        t.join()

    print(f"load stopped, cooling down {COOLDOWN_S}s to check for recovery...")
    cpu_idle_start = cpu_snapshot(base)
    time.sleep(COOLDOWN_S)
    cpu_idle_end = cpu_snapshot(base)
    if cpu_idle_start and cpu_idle_end:
        print_cpu("idle", cpu_idle_start, cpu_idle_end)
    mem = print_memory(base, f"t=+{COOLDOWN_S}s after load")
    memory_ok &= mem is not None
    if mem is not None:
        lowest_internal = min(lowest_internal, mem["min_free_heap_internal"]) if lowest_internal is not None else mem["min_free_heap_internal"]
    try:
        version = get_json(base, "/api/version")
        print(f"coredump: {version.get('coredump')}")
        print(f"uptime_s: {version.get('uptime_s')}")
    except (urllib.error.URLError, OSError, json.JSONDecodeError) as exc:
        print(f"[after] could not read version: {exc}")
        memory_ok = False
    print(f"summary: {stats['webui_bursts']} editor bursts, "
          f"{stats['webui_failed_assets']} failed assets, "
          f"{stats['nav_errors']} navigation errors, {stats['discovery_errors']} discovery errors")
    if args.min_internal_watermark and lowest_internal is not None and lowest_internal < args.min_internal_watermark:
        print(f"[memory] FAIL: since-boot internal-heap watermark {lowest_internal} bytes is below "
              f"{args.min_internal_watermark} bytes; if it was already low before this test, reboot and retest")
        memory_ok = False
    if not memory_ok or any(stats[k] for k in ("webui_failed_assets", "nav_errors", "discovery_errors")):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
