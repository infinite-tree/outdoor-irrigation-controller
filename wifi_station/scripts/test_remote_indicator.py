#!/usr/bin/env python3
"""Source checks for the station remote-running indicator (no hardware)."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
ASSETS = ROOT / "web_assets.h"
UI = ROOT / "web_ui.cpp"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_sources_include_remote_chip() -> None:
    app = read(WEB / "app.js")
    css = read(WEB / "style.css")
    ui = read(UI)

    if "chipHtml('RM'" not in app:
        fail("app.js is missing the RM status chip")
    if "z.remote" not in app or "remote_signal_on" not in app:
        fail("app.js must render remote from zones.remote or remote_signal_on")
    if "repeat(5,minmax(0,1fr))" not in css:
        fail("style.css must use a 5-column chip grid for RM")
    if 'zones["remote"]' not in ui:
        fail("web_ui.cpp must publish zones.remote")
    if '"%s + Remote · %s left"' not in ui:
        fail("web_ui.cpp must keep Remote in now_main when a zone is running")
    if '"Remote running"' not in ui:
        fail("web_ui.cpp must headline remote-only operation")


def test_embedded_assets_match_web() -> None:
    assets = read(ASSETS)
    for name in ("app.js", "style.css", "index.html"):
        data = read(WEB / name)
        if data not in assets:
            fail(f"web_assets.h is out of sync with web/{name}")
    if "chipHtml('RM'" not in assets:
        fail("web_assets.h is missing the RM chip")


def test_chip_html_marks_remote_on() -> None:
    app = read(WEB / "app.js")
    match = re.search(r"function chipHtml\(label, status\) \{.*?\n  \}", app, re.S)
    if not match:
        fail("could not extract chipHtml from app.js")

    script = match.group(0) + """
function chips(s) {
  const z = s.zones || {};
  const remoteStatus = z.remote || (s.remote_signal_on ? 'on' : 'off');
  return chipHtml('Z1', z.z1) +
    chipHtml('Z2', z.z2) +
    chipHtml('GH', z.gh) +
    chipHtml('WC', z.wc) +
    chipHtml('RM', remoteStatus);
}
const zoneOnRemoteOn = chips({zones:{z1:'on', z2:'off', gh:'off', wc:'off', remote:'on'}});
if (!zoneOnRemoteOn.includes('<b>RM</b>On') || !zoneOnRemoteOn.includes('chip-on')) {
  throw new Error('RM chip not On when zone and remote are both running: ' + zoneOnRemoteOn);
}
if (!zoneOnRemoteOn.includes('<b>Z1</b>On')) {
  throw new Error('Z1 chip should stay On: ' + zoneOnRemoteOn);
}
const fallback = chips({zones:{z1:'on'}, remote_signal_on: true});
if (!fallback.includes('<b>RM</b>On')) {
  throw new Error('RM chip should use remote_signal_on fallback: ' + fallback);
}
const remoteOff = chips({zones:{z1:'on', remote:'off'}});
if (!remoteOff.includes('<b>RM</b>Off') || remoteOff.includes('<b>RM</b>On')) {
  throw new Error('RM chip should be Off when remote is off: ' + remoteOff);
}
console.log('chip cases passed');
"""
    result = subprocess.run(
        ["node", "-e", script],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or result.stdout.strip() or "node chip test failed")


def main() -> None:
    test_sources_include_remote_chip()
    test_embedded_assets_match_web()
    test_chip_html_marks_remote_on()
    print("all remote indicator source tests passed")


if __name__ == "__main__":
    main()
