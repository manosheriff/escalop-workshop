#!/usr/bin/env python3
"""
Packs resources/profiles for the profile updates of Escalop Workshop 1.3.8 and later, which need no new app version.

The app reads profiles.json from the branch "profiles" at every start. When a vendor there has a higher version than
the installed one (and the same major.minor), it downloads the zip, checks its SHA-256, loads the profiles as a test,
and installs them silently. Pictures and bed models are not included: those come with the app.

1. Change the profiles (make_profiles.py, or the files in resources/profiles) and raise the vendor's version:
   VERSION in make_profiles.py (printers and print settings), the library version in its write_filaments() (filaments).
2. python scripts/orca_extra_profile_check.py
3. python tools/escaloplab/publish_profiles.py OUT_FOLDER [--base-url URL]
   Try it: serve OUT_FOLDER (python -m http.server) with --base-url http://localhost:8000 and set
   "escalop_profiles_url" in the app's config to http://localhost:8000/profiles.json.
4. Publish (reaches every customer at their next start): commit OUT_FOLDER's files to the branch "profiles", and keep
   the older zips there.
To undo a change, publish the old settings with a higher version.
"""
import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "resources" / "profiles"
VENDORS = ["OrcaFilamentLibrary", "Escaloplab"]
MIN_APP = "1.3.8"  # raise when profiles use a setting older apps don't have
BASE_URL = "https://raw.githubusercontent.com/manosheriff/escalop-workshop/profiles"


def pack():
    """The zip: <vendor>.json and the vendor's .json files, with entries for folders (the app needs them). Same
    profiles, same bytes."""
    files, folders = {}, set()
    for vendor in VENDORS:
        files[f"{vendor}.json"] = (ROOT / f"{vendor}.json").read_bytes().replace(b"\r\n", b"\n")
        for f in (ROOT / vendor).rglob("*.json"):
            rel = f.relative_to(ROOT)
            files[rel.as_posix()] = f.read_bytes().replace(b"\r\n", b"\n")
            folders.update(p.as_posix() + "/" for p in rel.parents if p.parts)
    for vendor in VENDORS:  # every listed file must be there
        index = json.loads(files[f"{vendor}.json"])
        for key in ("machine_model_list", "machine_list", "process_list", "filament_list"):
            for entry in index.get(key, []):
                path = f"{vendor}/{entry['sub_path']}"
                assert path in files, f"{path} is missing"
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(folders) + sorted(files):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if name.endswith("/") else zipfile.ZIP_DEFLATED
            info.external_attr = (0o40755 << 16) | 0x10 if name.endswith("/") else 0o100644 << 16
            z.writestr(info, files.get(name, b""))
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--base-url", default=BASE_URL)
    args = ap.parse_args()
    versions = {v: json.loads((ROOT / f"{v}.json").read_text(encoding="utf-8"))["version"] for v in VENDORS}

    data = pack()
    sha = hashlib.sha256(data).hexdigest()
    name = f"profiles-{versions['Escaloplab']}-{versions['OrcaFilamentLibrary']}-{sha[:8]}.zip"
    args.out.mkdir(parents=True, exist_ok=True)
    old = args.out / "profiles.json"
    if old.exists():
        before = json.loads(old.read_text(encoding="utf-8"))
        key = lambda v: [int(n) for n in v.split(".")]
        for v in VENDORS:
            assert key(versions[v]) >= key(before["vendors"][v]), f"{v}: {versions[v]} is older than {before['vendors'][v]}"
            assert key(versions[v])[:2] == key(before["vendors"][v])[:2], f"{v}: the major.minor can't change"
        assert sha == before["sha256"] or versions != before["vendors"], "the profiles changed: raise a version"
    (args.out / name).write_bytes(data)
    old.write_text(json.dumps({"min_app": MIN_APP, "vendors": versions, "url": f"{args.base_url}/{name}", "sha256": sha},
                              indent=4) + "\n", encoding="utf-8", newline="\n")
    print(f"{name}: {len(data)} bytes, {versions}")


if __name__ == "__main__":
    main()
