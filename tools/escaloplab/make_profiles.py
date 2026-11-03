#!/usr/bin/env python3
"""
Generates the Escaloplab vendor profiles (resources/profiles/Escaloplab*).

The v80 comes in bed sizes and with a Bowden or direct-drive extruder; each
combination is a printer model with its own 0.4 mm nozzle preset. Print
qualities are shared by every v80. Filaments use OrcaSlicer's generic PLA,
PETG and TPU until Escaloplab's own calibrated filament profiles exist.

Starting values come from the v80's published specs (210 x 210 x 200 mm,
255 C hotend, 90 C bed, 80+ mm/s, Marlin with auto bed leveling). Values marked
CALIBRATE are first guesses to be replaced after test prints.

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
VERSION = "01.00.00.00"

# (model name, bed X, bed Y, height, drive)
MODELS = [
    ("Escaloplab v80", 210, 210, 200, "bowden"),
    ("Escaloplab v80 Direct", 210, 210, 200, "direct"),
]

# Retraction per extruder type (CALIBRATE with Orca's retraction test)
RETRACT = {"bowden": {"length": "5", "speed": "40"}, "direct": {"length": "0.8", "speed": "35"}}

START_GCODE = "\n".join([
    "; Escaloplab v80 start",
    "M140 S[bed_temperature_initial_layer_single] ; start heating the bed",
    "M104 S150 ; warm the nozzle without oozing",
    "G90 ; absolute coordinates",
    "M83 ; relative extrusion",
    "G28 ; home all axes",
    "M190 S[bed_temperature_initial_layer_single] ; wait for the bed",
    "G29 ; auto bed leveling (CALIBRATE: confirm the printer's Marlin leveling type)",
    "G1 X3 Y10 Z5 F3000",
    "M109 S[nozzle_temperature_initial_layer] ; wait for the nozzle",
    "G1 Z0.28 F240",
    "G92 E0",
    "G1 Y150 E12 F1500 ; prime line",
    "G1 X3.4 F5000",
    "G1 Y20 E12 F1200 ; second prime line",
    "G92 E0",
])

END_GCODE = "\n".join([
    "; Escaloplab v80 end",
    "G91 ; relative moves",
    "G1 E-2 F2700 ; retract",
    "G1 Z5 F600 ; lift",
    "G90",
    "{if max_layer_z < printable_height-10}G1 Z{min(max_layer_z+10, printable_height-10)} F600{endif}",
    "G1 X5 Y{print_bed_max[1]*0.85} F{travel_speed*60} ; present the print",
    "M140 S0 ; bed off",
    "M104 S0 ; nozzle off",
    "M107 ; fan off",
    "M84 X Y E ; motors off",
])


def dump(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


def machine_common():
    return {
        "type": "machine",
        "name": "fdm_machine_common",
        "from": "system",
        "instantiation": "false",
        "printer_technology": "FFF",
        "gcode_flavor": "marlin2",
        "extruder_colour": ["#F0145A"],
        "extruder_offset": ["0x0"],
        "nozzle_diameter": ["0.4"],
        "printer_variant": "0.4",
        # Motion limits (CALIBRATE: read the real values with M503)
        "machine_max_acceleration_e": ["5000"],
        "machine_max_acceleration_extruding": ["1000"],
        "machine_max_acceleration_retracting": ["1000"],
        "machine_max_acceleration_travel": ["1500"],
        "machine_max_acceleration_x": ["1000"],
        "machine_max_acceleration_y": ["1000"],
        "machine_max_acceleration_z": ["100"],
        "machine_max_speed_e": ["60"],
        "machine_max_speed_x": ["300"],
        "machine_max_speed_y": ["300"],
        "machine_max_speed_z": ["10"],
        "machine_max_jerk_e": ["5"],
        "machine_max_jerk_x": ["8"],
        "machine_max_jerk_y": ["8"],
        "machine_max_jerk_z": ["0.4"],
        "machine_min_extruding_rate": ["0"],
        "machine_min_travel_rate": ["0"],
        "max_layer_height": ["0.32"],
        "min_layer_height": ["0.08"],
        "extruder_clearance_radius": "45",
        "extruder_clearance_height_to_rod": "30",
        "extruder_clearance_height_to_lid": "120",
        "retraction_minimum_travel": ["2"],
        "retract_before_wipe": ["70%"],
        "retract_when_changing_layer": ["1"],
        "retract_length_toolchange": ["1"],
        "retract_restart_extra": ["0"],
        "retract_restart_extra_toolchange": ["0"],
        "z_hop": ["0.2"],
        "wipe": ["1"],
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
        "default_materials": "Generic PLA @System;Generic PETG @System;Generic TPU @System",
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
        "default_filament_profile": ["Generic PLA @System"],
        "printable_area": ["0x0", f"{x}x0", f"{x}x{y}", f"0x{y}"],
        "printable_height": str(h),
        "retraction_length": [r["length"]],
        "retraction_speed": [r["speed"]],
        "deretraction_speed": [r["speed"]],
        "printer_settings_id": f"{name} 0.4 nozzle",
        "machine_start_gcode": START_GCODE,
        "machine_end_gcode": END_GCODE,
    }


# Print qualities: (name, layer height, first layer, outer, inner, infill, top) in mm and mm/s
QUALITIES = [
    ("0.12mm Fine", "0.12", "0.2", "35", "60", "80", "35"),
    ("0.16mm Optimal", "0.16", "0.2", "40", "65", "85", "35"),
    ("0.20mm Standard", "0.2", "0.2", "45", "70", "90", "40"),
    ("0.28mm Draft", "0.28", "0.28", "50", "80", "100", "45"),
]


def process_common():
    return {
        "type": "process",
        "name": "fdm_process_common",
        "from": "system",
        "instantiation": "false",
        "print_settings_id": "",
        "line_width": "0.45",
        "outer_wall_line_width": "0.42",
        "inner_wall_line_width": "0.45",
        "initial_layer_line_width": "0.5",
        "sparse_infill_line_width": "0.45",
        "internal_solid_infill_line_width": "0.42",
        "top_surface_line_width": "0.4",
        "support_line_width": "0.4",
        "wall_loops": "3",
        "top_shell_layers": "5",
        "top_shell_thickness": "0.8",
        "bottom_shell_layers": "4",
        "sparse_infill_density": "15%",
        "sparse_infill_pattern": "grid",
        "top_surface_pattern": "monotonicline",
        "bottom_surface_pattern": "monotonic",
        "seam_position": "aligned",
        "wall_infill_order": "inner wall/outer wall/infill",
        "detect_overhang_wall": "1",
        "detect_thin_wall": "1",
        "elefant_foot_compensation": "0.1",
        "initial_layer_speed": "25",
        "initial_layer_infill_speed": "40",
        "internal_solid_infill_speed": "70",
        "gap_infill_speed": "40",
        "bridge_speed": "25",
        "support_speed": "60",
        "travel_speed": "150",
        "default_acceleration": "1000",
        "outer_wall_acceleration": "800",
        "inner_wall_acceleration": "1000",
        "initial_layer_acceleration": "500",
        "top_surface_acceleration": "800",
        "travel_acceleration": "1500",
        "skirt_loops": "2",
        "skirt_distance": "4",
        "brim_width": "0",
        "enable_support": "0",
        "support_type": "normal(auto)",
        "support_threshold_angle": "30",
        "enable_prime_tower": "0",
        "filename_format": "{input_filename_base}_{filament_type[initial_tool]}_{print_time}.gcode",
    }


def quality(name, lh, first, outer, inner, infill, top):
    return {
        "type": "process",
        "name": f"{name} @Escaloplab v80",
        "inherits": "fdm_process_common",
        "from": "system",
        "instantiation": "true",
        "layer_height": lh,
        "initial_layer_print_height": first,
        "outer_wall_speed": outer,
        "inner_wall_speed": inner,
        "sparse_infill_speed": infill,
        "top_surface_speed": top,
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
    """The Escaloplab mark and name near the bed's front-left corner (the viewBox spans the bed)."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    font = instantiateVariableFont(TTFont(Path(__file__).with_name("Tektur.ttf")), {"wght": 700, "wdth": 100})
    gs, cmap, hmtx = font.getGlyphSet(), font.getBestCmap(), font["hmtx"]
    cap_units = font["OS/2"].sCapHeight or 700

    W, H = 2000, round(2000 * y / x)
    s = W / x  # px per mm
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
    chev = "".join(
        f'<polyline points="{ox+46*m:.1f},{oy+(78+o)*m:.1f} {ox+100*m:.1f},{oy+(40+o)*m:.1f} {ox+154*m:.1f},{oy+(78+o)*m:.1f}" '
        f'fill="none" stroke="#9a9ba3" stroke-width="{16*m:.1f}" stroke-linecap="round" stroke-linejoin="round"/>'
        for o in (0, 33, 66))
    path.write_text(f'<?xml version="1.0" encoding="UTF-8"?><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">'
                    f'{chev}<path d="{d}" fill="#9a9ba3"/></svg>\n', encoding="utf-8")


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
    for q in QUALITIES:
        p = quality(*q)
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
    print(f"{len(machine_models)} printers, {len(processes) - 1} print qualities")


if __name__ == "__main__":
    main()
