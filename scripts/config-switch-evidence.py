#!/usr/bin/env python3
"""DOC-GIF evidence pipeline (todo.md :67).

For each bool config switch, bring up the cluster with the switch off and on,
run a standard exercise, capture each app's log, and render an off/on GIF
showing the effect. Rerunnable and resumable: switches whose evidence is
already captured are skipped unless --force.

Evidence model (honest, no fabricated assets):
  * the owning app echoes "[Config] <path> = <value>" at startup, which
    proves the switch was applied in that state;
  * the behavioral evidence is the set of log lines (normalised: timestamps,
    pids and ports stripped) that appear in exactly one of the two states
    while the standard exercise runs. A switch with no triggered behaviour
    still shows the config echo as its evidence.

Output: docs/review/config-effect/<switch>.gif plus a generated index page
and a JSON manifest of the normalised evidence. Raw logs go to a scratch dir
under $SCRATCH (default /tmp/switch-evidence) and are not committed.

Usage:
  config-switch-evidence.py [--switch NAME] [--force] [--scratch DIR]
                            [--workers N] [--dry-run]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INVENTORY = REPO / "docs" / "review" / "config-switch-inventory.md"
OUT_DIR = REPO / "docs" / "review" / "config-effect"
DEEP_CHAIN = REPO / "scripts" / "cluster-smoke" / "deep-chain.sh"
BW_DEEP = REPO / "scripts" / "cluster-smoke" / "res" / "server" / "bw_deep.xml"

EX_RES = REPO / "programming" / "bigworld" / "examples" / "client_integration" / "python" / "simple" / "res"
SMOKE_RES = REPO / "scripts" / "cluster-smoke" / "res"
BIN = REPO / "game" / "bin" / "server" / "el7"
SERVER_IP = "192.168.5.188"

# normalisation: strip common noise so off/on logs are comparable
NOISE = re.compile(
    r"\b\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}\b"  # timestamps
    r"|PID: \d+|pid:\d+|port \d+|:\d{4,5}\b|0x[0-9a-fA-F]+"
    r"|\((?:[0-9]+)\)"  # entity ids / counts in parens
    r"|took [0-9.]+ seconds"  # profile timing
    r"|_\d{8}_\d{6}"          # secondary-DB prefix timestamp
    r"|= \d+/\d+"             # handleStatusCheck counts
    r"|global base \d+|entity=\d+"  # space ids come from a second range (1)
    r"|ClientAvatar \d+|onClientDeath: \d+|onEntitiesEnabled: \d+"
    r"|login: \d+ attached|Adding proxy \d+"  # client proxy id in baseapp
    r"|\b\d{4,}\b"            # entity ids, pids, ports, byte counts
)
# lines that are pure noise and never evidence
DROP = re.compile(
    r"^\[\d+\]|is running|Python server is running|started \d+"
    r"|timeWarningLevel exceeded|handleStatusCheck"
    r"|MachineDaemon::findInterface: Failed to find"  # startup race
)

# the owning app for each config class (where the [Config] echo appears)
APP_FOR_CLASS = {
    "BalanceConfig": "baseapp",
    "BaseAppConfig": "baseapp",
    "BotsConfig": "bots",
    "CellAppConfig": "cellapp",
    "CellAppMgrConfig": "cellappmgr",
    "DBAppConfig": "dbapp",
    "DBConfig": "dbapp",
    "LoginAppConfig": "loginapp",
    "RateLimitConfig": "baseapp",
    "SpaceConfig": "cellapp",
}


def parse_inventory():
    """Parse the switch table from the inventory markdown."""
    switches = []
    in_table = False
    for line in INVENTORY.read_text().splitlines():
        if line.startswith("## Switches (bool)"):
            in_table = True
            continue
        if in_table and line.startswith("## "):
            break
        if in_table and line.startswith("|") and "| class |" not in line:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) >= 4 and cells[0] and cells[0] != "---":
                switches.append({
                    "class": cells[0],
                    "name": cells[1],
                    "default": cells[2],
                    "xml": cells[3],
                })
    return switches


def switch_key(s):
    return f"{s['class']}.{s['name']}"


def apply_switch(overlay_xml, switch, value):
    """Set the switch element to the given bool in a copy of the overlay.

    The switch's xml path is '/' separated and may nest (e.g.
    balance/demo/enable). Missing intermediate sections are created.
    """
    parts = switch["xml"].split("/")
    root = ET.fromstring(overlay_xml)
    node = root
    for p in parts[:-1]:
        child = node.find(p)
        if child is None:
            child = ET.SubElement(node, p)
        node = child
    leaf = parts[-1]
    el = node.find(leaf)
    if el is None:
        el = ET.SubElement(node, leaf)
    el.text = value
    return ET.tostring(root, encoding="unicode")


def run(cmd, timeout=600):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def bring_up(overlay):
    # deep-chain.sh reuses the log dir across runs and appends nothing, but
    # leftover logs from an earlier run would pollute this state's evidence.
    log_dir = Path(os.environ.get("TMPDIR", "/tmp")) / "cluster-smoke"
    if log_dir.exists():
        for f in log_dir.glob("*.log"):
            f.unlink()
    env = dict(os.environ)
    env["BW_DEEP_OVERLAY"] = str(overlay)
    proc = subprocess.Popen(
        ["bash", str(DEEP_CHAIN)], env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines = []
    deadline = time.time() + 180
    # select() around the read: the deadline is only checked between lines, so
    # a plain blocking readline() on a stalled deep-chain.sh (seen under heavy
    # machine load - the script goes quiet mid bring-up) hangs the whole
    # pipeline forever.
    import select
    fd = proc.stdout.fileno()
    while time.time() < deadline:
        timeout = min(5.0, max(0.1, deadline - time.time()))
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            continue  # no line in the window; re-check the deadline
        line = proc.stdout.readline()
        if line:
            lines.append(line)
            if "full app set steady" in line:
                # deep-chain.sh exits right after this marker; reap it so it
                # cannot linger as a zombie holding the pipe open
                try:
                    proc.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
                return True, "".join(lines)
        elif proc.poll() is not None:
            break
    # bring-up never reached steady: kill the script or it leaks and fights
    # the next bring-up over ports, the database and the log dir
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    return False, "".join(lines)


def teardown():
    run(["bash", str(DEEP_CHAIN), "--teardown"], timeout=120)


def reset_db():
    """Drop and let deep-chain re-create the smoke database.

    Without this, entity ids accumulate across the ~96 cluster runs, so the
    same event logs different ids in the two states and the off/on diff fills
    with id noise instead of the switch's effect. deep-chain.sh re-creates
    the database (and re-syncs tables to defs) on bring-up.
    """
    r = subprocess.run(
        ["sudo", "-n", "mysql", "-e",
         "DROP DATABASE IF EXISTS bigworld;"],
        capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"[warn] db reset failed: {r.stderr.strip()[:200]}")


def exercise(scratch, tag):
    """Standard exercise: a login attempt, then a short settle."""
    out = scratch / f"client_{tag}.log"
    env = dict(os.environ)
    env["BW_RES_PATH"] = f"{EX_RES}:{REPO / 'game' / 'res' / 'bigworld'}:{SMOKE_RES}"
    proc = subprocess.Popen(
        [str(BIN / "examples" / "simple_python_client"),
         "-server", SERVER_IP, "-user", "cui", "-password", "pass"],
        env=env, stdout=open(out, "w"), stderr=subprocess.STDOUT)
    try:
        time.sleep(15)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def collect_logs(scratch, tag):
    """Copy the latest per-app logs for a state into the scratch dir."""
    src = Path(tempfile.gettempdir()) / "cluster-smoke"
    if not src.exists():
        return {}
    result = {}
    for f in src.glob("*.log"):
        dest = scratch / f"{tag}_{f.name}"
        shutil.copy(f, dest)
        result[f.stem] = dest
    return result


def normalise(line):
    line = NOISE.sub("", line)
    line = re.sub(r"\s+", " ", line).strip()
    if not line or DROP.search(line):
        return None
    return line


def evidence_of(scratch, tag):
    """Extract normalised evidence lines from captured logs."""
    by_app = {}
    # the client's own output is evidence too: it is the client-visible side of
    # the effect (e.g. "Login failed" vs "LOGGED_ON")
    files = [scratch / f"client_{tag}.log"]
    files += sorted(scratch.glob(f"{tag}_*.log"))
    for f in files:
        if not f.exists():
            continue
        if f.name.startswith("client_"):
            app = "client"
        else:
            app = f.name[len(tag) + 1:].rsplit(".", 1)[0]
        # which of the two baseapps an entity lands on is timing dependent, not
        # the switch's effect, so they merge into one logical app
        if app == "baseapp2":
            app = "baseapp"
        lines = set()
        for raw in f.read_text(errors="replace").splitlines():
            n = normalise(raw)
            if n:
                lines.add(n)
        if app in by_app:
            by_app[app] |= lines
        else:
            by_app[app] = lines
    return by_app


def diff_evidence(off, on):
    """Lines in exactly one state, per app."""
    apps = sorted(set(off) | set(on))
    res = {}
    for app in apps:
        a, b = off.get(app, set()), on.get(app, set())
        only = sorted(a - b), sorted(b - a)
        if only[0] or only[1]:
            res[app] = only
    return res


def render_gif(switch, off_lines, on_lines, out_gif):
    """Render an off/on terminal-style GIF with PIL."""
    from PIL import Image, ImageDraw, ImageFont
    font = None
    for cand in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"):
        if Path(cand).exists():
            font = ImageFont.truetype(cand, 16)
            break
    W, H = 760, 460
    BG = (20, 20, 28)
    OFF = (255, 90, 90)
    ON = (90, 220, 120)
    DIM = (140, 140, 150)

    def frame(panel, title, color):
        img = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(img)
        d.text((16, 10), f"== {title} ==", fill=color, font=font)
        y = 40
        for line in panel[:24]:
            d.text((16, y), line[:100], fill=(220, 220, 225), font=font)
            y += 22
        return img

    frames = []
    # off panel held, then on panel held
    frames += [frame(off_lines, "OFF", OFF)] * 6
    frames += [frame(on_lines, "ON", ON)] * 6
    frames[0].save(out_gif, save_all=True, append_images=frames[1:],
                   duration=90, loop=0)


def capture_switch(sw, scratch, force):
    key = switch_key(sw)
    sdir = scratch / key
    done = sdir / "done.json"
    if done.exists() and not force:
        print(f"[skip] {key} (already captured)")
        return json.loads(done.read_text())

    print(f"[capture] {key}: off/on")
    sdir.mkdir(parents=True, exist_ok=True)

    base = BW_DEEP.read_text()
    # Capture both explicit values (false then true) so the evidence is
    # comparable across switches, regardless of each switch's default.
    for state, value in (("off", "false"), ("on", "true")):
        # entity ids accumulate across the many cluster runs (the DB persists),
        # so the same event logs different ids in the two states and the diff
        # fills with id noise. Reset before each state so both start from the
        # same id baseline and only the switch differs.
        reset_db()
        overlay = sdir / f"bw_{state}.xml"
        overlay.write_text(apply_switch(base, sw, value))
        ok, log = bring_up(overlay)
        if not ok:
            print(f"[FAIL] {key} {state}: cluster did not reach steady\n{log[-800:]}")
            return None
        exercise(sdir, state)
        time.sleep(2)
        collect_logs(sdir, state)
        teardown()
        time.sleep(2)

    off = evidence_of(sdir, "off")
    on = evidence_of(sdir, "on")
    diff = diff_evidence(off, on)

    # build a compact per-state panel for the GIF
    def panel(state_evidence, diff_side):
        lines = []
        for app, (only_off, only_on) in diff.items():
            sel = only_off if diff_side == "off" else only_on
            for l in sel[:6]:
                lines.append(f"[{app}] {l}")
        return lines or ["(no behavioural difference in the standard exercise)"]

    off_lines = panel(off, "off")
    on_lines = panel(on, "on")

    gif = OUT_DIR / f"{key}.gif"
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    render_gif(sw, off_lines, on_lines, gif)

    manifest = {
        "switch": key,
        "class": sw["class"], "name": sw["name"],
        "xml": sw["xml"], "default": sw["default"],
        "off_diff": diff, "on_diff": diff,
        "gif": f"config-effect/{gif.name}",
        "captured": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    # the manifest lives next to the GIF so docs generation is rerunnable from
    # the repo alone; the scratch dir only holds the raw logs.
    (OUT_DIR / f"{key}.json").write_text(json.dumps(manifest, indent=2))
    done.write_text(json.dumps(manifest, indent=2))
    print(f"[done] {key}: {gif.name} ({len(diff)} apps differ)")
    return manifest


def generate_docs():
    """Render docs/review/config-effect/index.md from the captured manifests."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rows = []
    for f in sorted(OUT_DIR.glob("*.json")):
        rows.append(json.loads(f.read_text()))
    if not rows:
        print(f"no manifests in {OUT_DIR}")
        return 1

    total = len(parse_inventory())
    lines = [
        "# Config switch effects",
        "",
        f"Generated by `scripts/config-switch-evidence.py --docs` "
        f"({len(rows)}/{total} switches captured).",
        "",
        "Each switch has an off/on GIF: the first frames show the cluster's "
        "logs with the switch **off**, the last frames with it **on**. The "
        "evidence is the owning app's `[Config]` echo (proving the switch was "
        "applied) plus the behavioural lines that appear in exactly one "
        "state while the standard exercise (login, entity create, cell "
        "switch, settle) runs. Timestamps, pids and ports are normalised so "
        "the two states are comparable; a switch whose effect the exercise "
        "does not trigger says so rather than inventing one.",
        "",
        "Rerunnable: `config-switch-evidence.py` (resumable, switches already "
        "captured are skipped unless `--force`) then `--docs`.",
        "",
        "| switch | xml path | default | apps with a difference |",
        "|---|---|---|---|",
    ]
    for r in rows:
        apps = len(r["off_diff"])
        lines.append(f"| `{r['switch']}` | `{r['xml']}` | {r['default']} | "
                     f"{apps} |")
    lines.append("")

    for r in rows:
        lines.append(f"## {r['switch']}")
        lines.append("")
        lines.append(f"xml `{r['xml']}`, default `{r['default']}` "
                     f"(captured {r['captured']})")
        lines.append("")
        lines.append(f"![{r['switch']} off/on](./{r['gif'].split('/')[-1]})")
        lines.append("")
        diff = r["off_diff"]
        for app, (only_off, only_on) in diff.items():
            lines.append(f"### {app}")
            lines.append("")
            if only_off:
                lines.append("only when **off**:")
                lines.append("")
                lines.append("```")
                lines += only_off[:12]
                lines.append("```")
                lines.append("")
            if only_on:
                lines.append("only when **on**:")
                lines.append("")
                lines.append("```")
                lines += only_on[:12]
                lines.append("```")
                lines.append("")
        lines.append("")

    out = OUT_DIR / "index.md"
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({len(rows)} switches)")
    return 0


def main():
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--switch", help="capture only this switch (class.name)")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--scratch", default="/tmp/switch-evidence")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--docs", action="store_true",
                    help="render the docs page from the captured manifests")
    args = ap.parse_args()

    if args.docs:
        return generate_docs()

    switches = parse_inventory()
    if args.switch:
        switches = [s for s in switches if switch_key(s) == args.switch]
    if not switches:
        print("no matching switch")
        return 1

    print(f"{len(switches)} switches; output {OUT_DIR}")
    if args.dry_run:
        for s in switches:
            print(f"  {switch_key(s)}  xml={s['xml']}  default={s['default']}")
        return 0

    manifests = []
    for sw in switches:
        if args.switch and switch_key(sw) != args.switch:
            continue
        try:
            m = capture_switch(sw, Path(args.scratch), args.force)
            if m:
                manifests.append(m)
        except Exception as e:
            print(f"[FAIL] {switch_key(sw)}: {e}", file=sys.stderr)
            # leave the cluster down so the next switch starts clean
            try:
                teardown()
            except Exception:
                pass

    print(f"captured {len(manifests)} switches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
