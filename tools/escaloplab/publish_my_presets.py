#!/usr/bin/env python3
"""
Publishes the owner's own presets as the built-in TripleF3D presets of every TripleF3D app.

The owner tunes three kinds of presets in the app, each saved as "<built-in name> - Main":
  printer  "TripleF3D v80 0.4 nozzle - Main"          -> its changes go into every v80 printer (all four)
  process  "<quality> @TripleF3D v80 - Main"           -> into that print quality
  filament "<brand> <type> @System - Main"             -> into that filament (Patron / Lynx only)
Only the settings changed in a "- Main" preset count; the rest stays as make_profiles.py makes it.

Run (the desktop shortcut "Publish presets" does this):
    python tools/escaloplab/publish_my_presets.py
It shows what changes, asks before publishing, then: writes owner_presets.json, raises the profile versions,
runs make_profiles.py and the profile checks, commits, publishes the profile update (branch "profiles", which
every app reads at start) and pushes the escalop branch when that is safe (only this commit on top of GitHub).
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
FEED = REPO.parent / "profiles-feed"            # worktree of the branch "profiles"
OWNER_FILE = HERE / "owner_presets.json"
SUFFIX = " - Main"
# Not settings: names, links and bookkeeping the app writes into every user preset
SKIP = {"from", "inherits", "name", "version", "type", "instantiation", "setting_id", "base_id", "compatible_printers",
        "compatible_printers_condition", "compatible_prints", "compatible_prints_condition", "renamed_from",
        "printer_settings_id", "print_settings_id", "filament_settings_id", "printer_extruder_id",
        "printer_extruder_variant", "print_extruder_id", "print_extruder_variant", "filament_extruder_variant",
        "filament_extruder_id", "updated_time", "user_id"}


def run(*cmd, cwd=REPO, check=True):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode:
        sys.exit(f"Failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
    return r.stdout.strip()


def user_folders():
    root = Path(os.environ["APPDATA"]) / "EscalopWorkshop" / "user"
    return [d for d in root.iterdir() if d.is_dir()] if root.exists() else []


def read_owner_presets():
    # name (or old name, "renamed_from") -> built-in name
    built_in = {"printers": {}, "processes": {}, "filaments": {}}
    for kind, files in (("printers", (REPO / "resources/profiles/Escaloplab/machine").glob("*0.4 nozzle.json")),
                        ("processes", (REPO / "resources/profiles/Escaloplab/process").glob("*@* v80.json")),
                        ("filaments", [f for f in (REPO / "resources/profiles/OrcaFilamentLibrary/filament").glob("*/*.json")
                                       if f.parent.name in ("Patron", "Lynx")])):
        for f in files:
            d = json.loads(f.read_text(encoding="utf-8"))
            built_in[kind][f.stem] = f.stem
            if d.get("renamed_from"):
                built_in[kind][d["renamed_from"]] = f.stem

    found = {"printers": {}, "processes": {}, "filaments": {}}
    sources = []
    for folder in user_folders():
        for sub, kind in (("machine", "printers"), ("process", "processes"), ("filament", "filaments")):
            for f in sorted((folder / sub).glob(f"*{SUFFIX}.json")):
                d = json.loads(f.read_text(encoding="utf-8"))
                base = built_in[kind].get(d.get("inherits", ""))
                if not base:
                    print(f"  skipped {f.stem}: it isn't based on a TripleF3D preset ({d.get('inherits') or 'no base'})")
                    continue
                settings = {k: v for k, v in d.items() if k not in SKIP}
                if kind == "printers":
                    found["printers"].update(settings)  # one printer setup for all v80 printers
                else:
                    found[kind][base] = settings
                sources.append(f"{sub}/{f.stem}")
    return {k: v for k, v in found.items() if v}, sources


def built_in_value(kind, name, key):
    """What customers have now (the generated presets already hold earlier published changes)."""
    if kind == "printers":
        folder, name = REPO / "resources/profiles/Escaloplab", "TripleF3D v80 0.4 nozzle"
    elif kind == "processes":
        folder = REPO / "resources/profiles/Escaloplab"
    else:
        folder = REPO / "resources/profiles/OrcaFilamentLibrary"
    files = {f.stem: f for f in folder.rglob("*.json")}
    while name in files:
        d = json.loads(files[name].read_text(encoding="utf-8"))
        if key in d:
            return d[key]
        name = d.get("inherits")
    return "(default)"


def show(v):
    v = v[0] if isinstance(v, list) and len(v) == 1 else v
    s = json.dumps(v, ensure_ascii=False) if not isinstance(v, str) else v
    return s if len(s) < 60 else s[:57] + "..."


def bump(text, pattern):
    """Raises the last number of a version like 01.00.06.00 (Escaloplab: third part; library: fourth)."""
    def up(m):
        parts = m.group(2).split(".")
        i = 2 if m.group(1).startswith("VERSION") else 3
        parts[i] = f"{int(parts[i]) + 1:02d}"
        if i == 2:
            parts[3] = "00"
        return f'{m.group(1)}"{".".join(parts)}"'
    new, n = re.subn(pattern, up, text, count=1)
    assert n == 1, pattern
    return new


def main():
    os.chdir(REPO)
    print("Your presets ending in '- Main':")
    owner, sources = read_owner_presets()
    for s in sources:
        print("  " + s)
    if not sources:
        sys.exit("None found. Save a preset as '<name> - Main' in TripleF3D first.")
    before = json.loads(OWNER_FILE.read_text(encoding="utf-8")) if OWNER_FILE.exists() else {}
    for _ in range(3):
        if subprocess.run(["git", "fetch", "-q", "origin", "escalop", "profiles"], cwd=REPO).returncode == 0:
            break
    if owner == before:
        version = json.loads((REPO / "resources/profiles/Escaloplab.json").read_text(encoding="utf-8"))["version"]
        live = json.loads(run("git", "show", "origin/profiles:profiles.json"))["vendors"]["Escaloplab"]
        if live == version:
            print("\nNothing changed since the last publish.")
            return
        print(f"\nNo new changes, but {version} isn't published yet: publishing it now.")
        return publish_feed(version)

    print("\nWhat customers get (now -> new):")
    for kind, title in (("printers", "All v80 printers"), ("processes", None), ("filaments", None)):
        new_all = {None: owner.get(kind, {})} if kind == "printers" else owner.get(kind, {})
        old_all = {None: before.get(kind, {})} if kind == "printers" else before.get(kind, {})
        for name in sorted(set(new_all) | set(old_all), key=str):
            new, old = new_all.get(name, {}), old_all.get(name, {})
            lines = []
            for k in sorted(set(new) | set(old)):
                if k not in new:
                    lines.append(f"    {k}: back to the standard value")
                elif new[k] != built_in_value(kind, name, k):
                    lines.append(f"    {k}: {show(built_in_value(kind, name, k))} -> {show(new[k])}")
            if lines:
                print(f"  {title or name}")
                print("\n".join(lines))

    if input("\nPublish this to every TripleF3D app now? Type yes: ").strip().lower() not in ("yes", "y"):
        print("Nothing published.")
        return

    if run("git", "branch", "--show-current") != "escalop":
        sys.exit("The workshop folder isn't on the escalop branch; nothing published.")
    run("git", "merge", "-q", "--ff-only", "origin/escalop", check=False)  # catch up when possible

    # 1. Settings and versions
    OWNER_FILE.write_text(json.dumps(owner, indent=4, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    mp = HERE / "make_profiles.py"
    text = mp.read_text(encoding="utf-8")
    text = bump(text, r'(VERSION = )"([\d.]+)"')
    text = bump(text, r'(lib\["version"\] = )"([\d.]+)"')
    mp.write_text(text, encoding="utf-8")

    # 2. Profiles and checks
    print("Making the profiles...")
    run(sys.executable, str(mp))
    run(sys.executable, "scripts/assign_vendor_setting_ids.py")
    run(sys.executable, "scripts/orca_extra_profile_check.py")
    version = json.loads((REPO / "resources/profiles/Escaloplab.json").read_text(encoding="utf-8"))["version"]

    # 3. Commit (only these files) and push escalop when nothing else is waiting
    paths = ["resources/profiles", "tools/escaloplab/make_profiles.py", "tools/escaloplab/owner_presets.json"]
    run("git", "add", "--", *paths)
    run("git", "commit", "-q", "-m", f"TripleF3D profiles {version}: the owner's presets ({', '.join(sources)}) [skip ci]", "--", *paths)
    ahead = run("git", "rev-list", "origin/escalop..HEAD").split()
    if len(ahead) == 1:
        pushed = any(subprocess.run(["git", "push", "-q", "origin", "HEAD:escalop"], cwd=REPO).returncode == 0
                     for _ in range(3))
        print("Saved to GitHub (escalop)." if pushed else "Couldn't push escalop; the profile update still goes out.")
    else:
        print(f"Not pushing escalop: {len(ahead) - 1} other commit(s) are waiting there. The profile update still goes out.")

    publish_feed(version)


def publish_feed(version):
    """The profile update every app reads at start (branch "profiles"; the older zips stay)."""
    if not FEED.exists():
        run("git", "worktree", "add", "-f", str(FEED), "profiles")
    run("git", "checkout", "-q", "profiles", cwd=FEED)
    run("git", "reset", "-q", "--hard", "origin/profiles", cwd=FEED)
    print(run(sys.executable, str(HERE / "publish_profiles.py"), str(FEED)))
    run("git", "add", "-A", cwd=FEED)
    run("git", "commit", "-q", "-m", f"Profiles {version}: the owner's presets", cwd=FEED)
    if not any(subprocess.run(["git", "push", "-q", "origin", "profiles"], cwd=FEED).returncode == 0 for _ in range(4)):
        sys.exit("Couldn't reach GitHub to publish. Run it again later: it picks up from here.")
    print(f"\nPublished: TripleF3D profiles {version}. Every TripleF3D app (1.4.0 and later) installs them at its next start.")

if __name__ == "__main__":
    main()
