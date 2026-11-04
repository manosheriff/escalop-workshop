// Escalop Workshop (the slicer) shows the v80 see-through around its build plate.
// This writes that printer as a binary STL in the slicer's bed coordinates:
// X/Y centred on the build plate (like the bed model), Z = 0 on the plate's top, +Y towards the back.
// Left out: the bed (the slicer draws its own plate), the PSU and control board (owner's request), the
// spool and the Bowden tube (the app's preview shouldn't show filament). Meshes are simplified to stay light.
// Usage: node frame.mjs ../../public/models/v80.glb <out.stl> [max triangles]
import fs from "node:fs";
import { NodeIO } from "@gltf-transform/core";
import { ALL_EXTENSIONS } from "@gltf-transform/extensions";
import { simplifyPrimitive } from "@gltf-transform/functions";
import { MeshoptDecoder, MeshoptSimplifier } from "meshoptimizer";

const [input, output, maxTris = "160000"] = process.argv.slice(2);
await MeshoptDecoder.ready;
await MeshoptSimplifier.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ "meshopt.decoder": MeshoptDecoder });
const doc = await io.read(input);
const root = doc.getRoot();

// The group of a node is set on the top-level part (extras.group); children inherit it
const groupOf = (node) => {
  for (let n = node; n; n = n.getParentNode?.() ?? null) {
    const g = n.getExtras()?.group;
    if (g) return g;
  }
  return null;
};
// gltf-transform nodes know their parent through the scene graph
const parent = new Map();
for (const n of root.listNodes()) for (const c of n.listChildren()) parent.set(c, n);
for (const n of root.listNodes()) n.getParentNode = () => parent.get(n) ?? null;

const labelOf = (node) => {
  for (let n = node; n; n = n.getParentNode?.() ?? null) {
    const l = n.getExtras()?.label;
    if (l) return l;
  }
  return "";
};
const HIDDEN_GROUPS = new Set(["bed", "spool", "bowden"]);
const HIDDEN_LABELS = /psu|arduino|ramps|electronics tray/i;

const parts = [];
let total = 0;
for (const node of root.listNodes()) {
  const mesh = node.getMesh();
  if (!mesh) continue;
  const group = groupOf(node);
  const m = node.getWorldMatrix();
  for (const prim of mesh.listPrimitives()) {
    const idx = prim.getIndices();
    const tris = idx ? idx.getCount() / 3 : prim.getAttribute("POSITION").getCount() / 3;
    total += tris;
    parts.push({ node, prim, group, label: labelOf(node), m, tris });
  }
}

// Plate position from the bed group: its top and its centre
let bed = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
const apply = (m, p) => [
  m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
  m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
  m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
];
for (const p of parts.filter((p) => p.group === "bed")) {
  const pos = p.prim.getAttribute("POSITION");
  const v = [0, 0, 0];
  for (let i = 0; i < pos.getCount(); i++) {
    const w = apply(p.m, pos.getElement(i, v));
    for (let k = 0; k < 3; k++) {
      bed.min[k] = Math.min(bed.min[k], w[k]);
      bed.max[k] = Math.max(bed.max[k], w[k]);
    }
  }
}
if (!isFinite(bed.min[0])) throw new Error("no bed group in the model");
const bedSize = [bed.max[0] - bed.min[0], bed.max[2] - bed.min[2]];
// The model's units: the plate is about 220-240 mm wide
const scale = bedSize[0] < 5 ? 1000 : 1;
const cx = (bed.min[0] + bed.max[0]) / 2, cz = (bed.min[2] + bed.max[2]) / 2, top = bed.max[1];
console.log("bed", bedSize.map((s) => (s * scale).toFixed(1)).join(" x "), "mm; model triangles", total);

const keep = parts.filter((p) => !HIDDEN_GROUPS.has(p.group) && !HIDDEN_LABELS.test(p.label));
console.log("left out:", [...new Set(parts.filter((p) => !keep.includes(p)).map((p) => p.label))].join(", "));
const keepTris = keep.reduce((s, p) => s + p.tris, 0);
const ratio = Math.min(1, Number(maxTris) / keepTris);
const out = [];
for (const p of keep) {
  let prim = p.prim;
  if (ratio < 1 && p.tris > 200) {
    prim = prim.clone();
    simplifyPrimitive(prim, { simplifier: MeshoptSimplifier, ratio, error: 0.004, lockBorder: false });
  }
  const pos = prim.getAttribute("POSITION");
  const idx = prim.getIndices();
  const n = idx ? idx.getCount() : pos.getCount();
  const v = [0, 0, 0];
  const at = (i) => {
    const w = apply(p.m, pos.getElement(idx ? idx.getScalar(i) : i, v));
    // glTF: Y up, front +Z  ->  slicer: Z up, front -Y
    return [(w[0] - cx) * scale, -(w[2] - cz) * scale, (w[1] - top) * scale];
  };
  for (let i = 0; i + 2 < n; i += 3) out.push(at(i), at(i + 1), at(i + 2));
}

const tris = out.length / 3;
const buf = Buffer.alloc(84 + tris * 50);
buf.write("Escaloplab v80 frame for Escalop Workshop", 0, "ascii");
buf.writeUInt32LE(tris, 80);
let o = 84;
for (let t = 0; t < tris; t++) {
  const [a, b, c] = [out[t * 3], out[t * 3 + 1], out[t * 3 + 2]];
  const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
  let nx = u[1] * w[2] - u[2] * w[1], ny = u[2] * w[0] - u[0] * w[2], nz = u[0] * w[1] - u[1] * w[0];
  const l = Math.hypot(nx, ny, nz) || 1;
  for (const x of [nx / l, ny / l, nz / l, ...a, ...b, ...c]) {
    buf.writeFloatLE(x, o);
    o += 4;
  }
  o += 2;
}
fs.writeFileSync(output, buf);
console.log("wrote", output, tris, "triangles", (buf.length / 1e6).toFixed(1), "MB");
