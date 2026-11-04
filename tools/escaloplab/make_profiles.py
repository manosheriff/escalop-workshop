#!/usr/bin/env python3
"""
Generates the Escaloplab vendor profiles (resources/profiles/Escaloplab*).

The v80 (210 x 210 x 200 mm bed, Marlin with auto bed leveling) comes with a
Bowden or a direct-drive extruder; each is a printer model with a 0.4 mm nozzle
preset. At the owner's request the motion limits, retraction and print
qualities are the Prusa MK3S 0.4 ones, read from tools/escaloplab/prusa_snapshot
(copied from OrcaSlicer 2.4.2). Prusa-firmware-only G-code (M862, G80, M907) is replaced by Marlin
equivalents. The Bowden version keeps a longer retraction, which a Bowden tube
needs. Filaments: Patron and Lynx (PLA, PETG, TPU, ABS) first, added to the
OrcaFilamentLibrary with generic settings, then OrcaSlicer's generic ones.
The see-through printer (<model>_frame.stl) comes from the website's 3D model:
    node tools/escaloplab/frame.mjs <v80.glb> resources/profiles/Escaloplab/escaloplab_v80_frame.stl 80000
(run from the website's tools/model folder, which has the glTF packages).

Run from the repo root:
    python tools/escaloplab/make_profiles.py
    python scripts/assign_vendor_setting_ids.py
    python scripts/orca_extra_profile_check.py
"""
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "resources" / "profiles"
VENDOR = "Escaloplab"
VERSION = "01.00.02.00"

# (model name, bed X, bed Y, height, drive)
MODELS = [
    ("Escaloplab v80", 210, 210, 200, "bowden"),
    ("Escaloplab v80 Direct", 210, 210, 200, "direct"),
]

PRUSA_PRINTER = "Prusa MK3S 0.4 nozzle"
# The MK3S print qualities for a 0.4 mm nozzle, with the name they get on the v80
PRUSA_QUALITIES = [
    ("0.10mm Detail @MK3S 0.4", "0.10mm Detail"),
    ("0.15mm Quality @MK3S 0.4", "0.15mm Quality"),
    ("0.20mm Standard @MK3S 0.4", "0.20mm Standard"),
    ("0.20mm Speed @MK3S 0.4", "0.20mm Speed"),
    ("0.30mm Draft @MK3S 0.4", "0.30mm Draft"),
]
# Copied from the MK3S printer preset
PRUSA_MACHINE_KEYS = [
    "machine_max_acceleration_e", "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
    "machine_max_acceleration_travel", "machine_max_acceleration_x", "machine_max_acceleration_y",
    "machine_max_acceleration_z", "machine_max_speed_e", "machine_max_speed_x", "machine_max_speed_y",
    "machine_max_speed_z", "machine_max_jerk_e", "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z",
    "max_layer_height", "min_layer_height", "retraction_minimum_travel", "retract_before_wipe",
    "retract_when_changing_layer", "retract_length_toolchange", "retract_restart_extra",
    "retract_restart_extra_toolchange", "z_hop", "wipe", "layer_change_gcode", "before_layer_change_gcode",
]
META_KEYS = {"type", "name", "inherits", "from", "setting_id", "instantiation", "compatible_printers",
             "compatible_printers_condition", "print_settings_id", "renamed_from"}


def prusa(kind, name):
    """A Prusa preset with everything it inherits merged in."""
    files = {}
    # The MK3S presets this needs, copied from OrcaSlicer 2.4.2 (the other brands are not shipped)
    for f in (Path(__file__).parent / "prusa_snapshot" / kind).glob("*.json"):
        d = json.loads(f.read_text(encoding="utf-8"))
        files[d["name"]] = d

    def resolve(n):
        d = files[n]
        out = resolve(d["inherits"]) if d.get("inherits") in files else {}
        out.update(d)
        return out

    return resolve(name)


# Retraction: direct drive = the MK3S (also direct drive); a Bowden tube needs more
RETRACT = {"bowden": {"length": "5", "speed": "40"}, "direct": {"length": "0.8", "speed": "30"}}

# The MK3S start sequence for Marlin: G29 auto bed leveling instead of Prusa's G80
START_GCODE = "\n".join([
    "; Escaloplab v80 start (Prusa MK3S sequence, Marlin commands)",
    "G90 ; use absolute coordinates",
    "M83 ; extruder relative mode",
    "M104 S[first_layer_temperature] ; set extruder temp",
    "M140 S[first_layer_bed_temperature] ; set bed temp",
    "M190 S[first_layer_bed_temperature] ; wait for bed temp",
    "M109 S[first_layer_temperature] ; wait for extruder temp",
    "G28 ; home all axes",
    "G29 ; auto bed leveling",
    "G1 Z0.2 F720",
    "G1 X0 Y1 F3000 ; front edge of the bed",
    "G92 E0",
    "G1 X60 E9 F1000 ; intro line",
    "G1 X100 E12.5 F1000 ; intro line",
    "G92 E0",
    "M221 S{if layer_height<0.075}100{else}95{endif}",
])

END_GCODE = "\n".join([
    "{if max_layer_z < max_print_height}G1 Z{z_offset+min(max_layer_z+1, max_print_height)} F720 ; Move print head up{endif}",
    "G1 X0 Y200 F3600 ; park",
    "{if max_layer_z < max_print_height}G1 Z{z_offset+min(max_layer_z+49, max_print_height)} F720 ; Move print head further up{endif}",
    "G4 ; wait",
    "M221 S100 ; reset flow",
    "M104 S0 ; turn off temperature",
    "M140 S0 ; turn off heatbed",
    "M107 ; turn off fan",
    "M84 ; disable motors",
    "; max_layer_z = [max_layer_z]",
])


def dump(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


def machine_common():
    mk3s = prusa("machine", PRUSA_PRINTER)
    return {
        **{k: mk3s[k] for k in PRUSA_MACHINE_KEYS},
        "type": "machine",
        "name": "fdm_machine_common",
        "from": "system",
        "instantiation": "false",
        "printer_technology": "FFF",
        "gcode_flavor": "marlin2",
        "extruder_colour": ["#FF2E8A"],
        "extruder_offset": ["0x0"],
        "nozzle_diameter": ["0.4"],
        "printer_variant": "0.4",
        "extruder_clearance_radius": "45",
        "extruder_clearance_height_to_rod": "30",
        "extruder_clearance_height_to_lid": "120",
        "single_extruder_multi_material": "1",
        "change_filament_gcode": "M600",
        "machine_pause_gcode": "M0",
        "before_layer_change_gcode": ";BEFORE_LAYER_CHANGE\n;[layer_z]\nG92 E0\n",
        "layer_change_gcode": "",
        # Send prints over Wi-Fi to the printer's ESP3D module (the address is entered once per printer)
        "host_type": "esp3d",
        "printer_settings_id": "",
        "default_print_profile": "",
        "machine_start_gcode": "",
        "machine_end_gcode": "",
    }


def model_file(name, slug):
    return {
        "type": "machine_model",
        "name": name,
        "model_id": slug,
        "nozzle_diameter": "0.4",
        "machine_tech": "FFF",
        "family": VENDOR,
        "bed_model": f"{slug}_buildplate_model.stl",
        "bed_texture": f"{slug}_buildplate_texture.svg",
        "hotend_model": "",
        "default_materials": ";".join(filament_names() + ["Generic PLA @System", "Generic PETG @System"]),
    }


def printer_preset(name, x, y, h, drive):
    r = RETRACT[drive]
    return {
        "type": "machine",
        "name": f"{name} 0.4 nozzle",
        "inherits": "fdm_machine_common",
        "from": "system",
        "instantiation": "true",
        "printer_model": name,
        "printer_variant": "0.4",
        "nozzle_diameter": ["0.4"],
        "default_print_profile": "0.20mm Standard @Escaloplab v80",
        "default_filament_profile": [f"{FILAMENT_BRANDS[0]} PLA @System"],
        "printable_area": ["0x0", f"{x}x0", f"{x}x{y}", f"0x{y}"],
        "printable_height": str(h),
        "retraction_length": [r["length"]],
        "retraction_speed": [r["speed"]],
        "deretraction_speed": [r["speed"]],
        "printer_settings_id": f"{name} 0.4 nozzle",
        "machine_start_gcode": START_GCODE,
        "machine_end_gcode": END_GCODE,
    }


def process_common():
    return {
        "type": "process",
        "name": "fdm_process_common",
        "from": "system",
        "instantiation": "false",
        "print_settings_id": "",
    }


def quality(prusa_name, name):
    """An MK3S print quality, renamed for the v80 and made compatible with it."""
    settings = {k: v for k, v in prusa("process", prusa_name).items() if k not in META_KEYS}
    return {
        "type": "process",
        "name": f"{name} @Escaloplab v80",
        "inherits": "fdm_process_common",
        "from": "system",
        "instantiation": "true",
        **settings,
        "compatible_printers": [f"{m[0]} 0.4 nozzle" for m in MODELS],
    }


def bed_stl(path: Path, x, y, radius=8, margin=3, steps=6):
    """A thin plate with rounded corners, centered like OrcaSlicer's bed models (z -1..0)."""
    w, d = x / 2 + margin, y / 2 + margin
    pts = []
    for cx, cy, a0 in [(w - radius, d - radius, 0), (-w + radius, d - radius, 90), (-w + radius, -d + radius, 180), (w - radius, -d + radius, 270)]:
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    tris = []
    for i in range(len(pts)):
        p, q = pts[i], pts[(i + 1) % len(pts)]
        tris += [((0, 0, 0), (*p, 0), (*q, 0)), ((0, 0, -1), (*q, -1), (*p, -1))]
        tris += [((*p, 0), (*p, -1), (*q, -1)), ((*p, 0), (*q, -1), (*q, 0))]
    with open(path, "wb") as f:
        f.write(b"Escaloplab v80 build plate".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            f.write(struct.pack("<3f", 0, 0, 0))
            for v in t:
                f.write(struct.pack("<3f", *v))
            f.write(b"\0\0")


def bed_texture(path: Path, x, y):
    """The plate as a textured gold PEI sheet (owner's request): a warm bronze base with a fine
    random speckle like powder-coated PEI, and the Escaloplab mark and name etched near the
    front-left corner. The viewBox spans the bed. Only plain shapes: the slicer's SVG reader
    (nanosvg) has no filters."""
    import random
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    font = instantiateVariableFont(TTFont(Path(__file__).with_name("Tektur.ttf")), {"wght": 700, "wdth": 100})
    gs, cmap, hmtx = font.getGlyphSet(), font.getBestCmap(), font["hmtx"]
    cap_units = font["OS/2"].sCapHeight or 700

    W, H = 2000, round(2000 * y / x)
    s = W / x  # px per mm
    rnd = random.Random(80)  # same speckle every run
    # Sheet: bronze with a soft light falloff, rounded corners like a spring-steel sheet
    out = [f'<defs><linearGradient id="pei" x1="0" y1="0" x2="1" y2="1">'
           f'<stop offset="0" stop-color="#d2a752"/><stop offset="0.55" stop-color="#bf9142"/>'
           f'<stop offset="1" stop-color="#a87b33"/></linearGradient></defs>',
           f'<rect x="0" y="0" width="{W}" height="{H}" rx="{3*s:.1f}" fill="url(#pei)"/>']
    # Speckle: tiny light and dark grains
    for _ in range(4200):
        light = rnd.random() < 0.5
        out.append(f'<circle cx="{rnd.uniform(0, W):.0f}" cy="{rnd.uniform(0, H):.0f}" r="{rnd.uniform(1.2, 3.2):.1f}" '
                   f'fill="{"#e6c97f" if light else "#8f6526"}" fill-opacity="{rnd.uniform(0.25, 0.6):.2f}"/>')
    # Etched mark and name
    cap = 9 * s  # 9 mm tall letters
    k = cap / cap_units
    d, cx, base = "", 24 * s, H - 14 * s
    for ch in "ESCALOPLAB v80":
        if ch == " ":
            cx += cap * 0.5
            continue
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (k, 0, 0, -k, cx, base)))
        d += pen.getCommands()
        cx += hmtx[g][0] * k + cap * 0.08
    m = 12 * s / 200  # 12 mm chevron mark
    ox, oy = 8 * s, H - 26 * s
    etch = "#7d5620"
    out += [f'<polyline points="{ox+46*m:.1f},{oy+(78+o)*m:.1f} {ox+100*m:.1f},{oy+(40+o)*m:.1f} {ox+154*m:.1f},{oy+(78+o)*m:.1f}" '
            f'fill="none" stroke="{etch}" stroke-opacity="0.8" stroke-width="{16*m:.1f}" stroke-linecap="round" stroke-linejoin="round"/>'
            for o in (0, 33, 66)]
    out.append(f'<path d="{d}" fill="{etch}" fill-opacity="0.8"/>')
    path.write_text(f'<?xml version="1.0" encoding="UTF-8"?><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">'
                    + "".join(out) + "</svg>\n", encoding="utf-8")


# Egyptian filament brands, listed first (owner's choice). Settings start from OrcaSlicer's
# generic ones for each material until Escaloplab calibrates them.
FILAMENT_BRANDS = ["Patron", "Lynx"]
FILAMENT_TYPES = [("PLA", "fdm_filament_pla"), ("PETG", "fdm_filament_pet"), ("TPU", "fdm_filament_tpu"), ("ABS", "fdm_filament_abs")]
LIBRARY = OUT / "OrcaFilamentLibrary"


def filament_names():
    # PLA of both brands first, then PETG, ...
    return [f"{b} {t} @System" for t, _ in FILAMENT_TYPES for b in FILAMENT_BRANDS]


def write_filaments():
    """Adds the brands to OrcaSlicer's filament library, so they work with every printer."""
    lib_json = OUT / "OrcaFilamentLibrary.json"
    lib = json.loads(lib_json.read_text(encoding="utf-8"))
    entries = [e for e in lib["filament_list"] if not any(e["name"].startswith(b + " ") for b in FILAMENT_BRANDS)]
    added = []
    for brand in FILAMENT_BRANDS:
        folder = LIBRARY / "filament" / brand
        if folder.exists():
            for f in folder.glob("*.json"):
                f.unlink()
        for t, base in FILAMENT_TYPES:
            name = f"{brand} {t} @System"
            dump(folder / f"{name}.json", {
                "type": "filament",
                "name": name,
                "inherits": base,
                "from": "system",
                "instantiation": "true",
                "filament_vendor": [brand],
                "compatible_printers": [],
            })
            added.append({"name": name, "sub_path": f"filament/{brand}/{name}.json"})
    lib["filament_list"] = entries + added
    # Newer than OrcaSlicer 2.4.2's library, so installed copies are replaced
    lib["version"] = "02.04.00.10"
    dump(lib_json, lib)
    return len(added)


def main():
    base = OUT / VENDOR
    for sub in ("machine", "process"):
        if (base / sub).exists():
            for f in (base / sub).glob("*.json"):
                f.unlink()
    dump(base / "machine" / "fdm_machine_common.json", machine_common())
    dump(base / "process" / "fdm_process_common.json", process_common())

    machine_models = []
    machines = [{"name": "fdm_machine_common", "sub_path": "machine/fdm_machine_common.json"}]
    processes = [{"name": "fdm_process_common", "sub_path": "process/fdm_process_common.json"}]
    for name, x, y, h, drive in MODELS:
        slug = name.lower().replace(" ", "_")
        dump(base / "machine" / f"{name}.json", model_file(name, slug))
        dump(base / "machine" / f"{name} 0.4 nozzle.json", printer_preset(name, x, y, h, drive))
        machine_models.append({"name": name, "sub_path": f"machine/{name}.json"})
        machines.append({"name": f"{name} 0.4 nozzle", "sub_path": f"machine/{name} 0.4 nozzle.json"})
        bed_stl(base / f"{slug}_buildplate_model.stl", x, y)
        bed_texture(base / f"{slug}_buildplate_texture.svg", x, y)
    for prusa_name, name in PRUSA_QUALITIES:
        p = quality(prusa_name, name)
        dump(base / "process" / f"{p['name']}.json", p)
        processes.append({"name": p["name"], "sub_path": f"process/{p['name']}.json"})

    dump(OUT / f"{VENDOR}.json", {
        "name": VENDOR,
        "url": "https://escaloplab.com",
        "version": VERSION,
        "force_update": "0",
        "description": "Escaloplab v80 configurations",
        "machine_model_list": machine_models,
        "process_list": processes,
        "filament_list": [],
        "machine_list": machines,
    })
    n = write_filaments()
    print(f"{len(machine_models)} printers, {len(processes) - 1} print qualities, {n} brand filaments")


if __name__ == "__main__":
    main()
