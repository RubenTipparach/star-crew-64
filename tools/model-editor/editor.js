// star-crew-64 model editor
//
// Tools:
//   - Grab: click a vertex to select it
//   - Translate: drag the gizmo (snaps to GRID = 0.1)
//   - + Box: spawn a new 0.2-unit cube part
//
// Persistence:
//   - Autosave to localStorage (debounced)
//   - Manual "Save / download" writes a JSON file via <a download> — works
//     on iOS Safari and Android Chrome via the OS share/Downloads flow.
//
// Model format:
//   { name, grid, vertices: [[x,y,z], ...], triangles: [[i,j,k], ...], parts?: [...] }

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { TransformControls } from "three/addons/controls/TransformControls.js";

const GRID = 0.1;
const VERT_RADIUS = 0.02;
const AUTOSAVE_KEY = "starCrew64.modelEditor.v1";

// ---------- default model: loaded from assets/models/character.json ----------
const DEFAULT_MODEL_URL = "../../assets/models/character.json";

// ---------- scene setup ----------
const viewport = document.getElementById("viewport");
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setSize(viewport.clientWidth, viewport.clientHeight);
viewport.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101218);

const camera = new THREE.PerspectiveCamera(
  45, viewport.clientWidth / viewport.clientHeight, 0.01, 100
);
const DEFAULT_CAM = new THREE.Vector3(2.2, 1.8, 2.4);
const DEFAULT_TARGET = new THREE.Vector3(0, 0.8, 0);
camera.position.copy(DEFAULT_CAM);

const orbit = new OrbitControls(camera, renderer.domElement);
orbit.target.copy(DEFAULT_TARGET);
orbit.mouseButtons = {
  LEFT: null, // leave left button free for vertex picking
  MIDDLE: THREE.MOUSE.DOLLY,
  RIGHT: THREE.MOUSE.ROTATE,
};
orbit.update();

// Grid: 2 units wide, divisions = 20 → 0.1 cells
const grid = new THREE.GridHelper(2, 20, 0x3a3e48, 0x23262f);
scene.add(grid);
scene.add(new THREE.AxesHelper(0.3));

// Lighting
scene.add(new THREE.AmbientLight(0xffffff, 0.5));
const dir = new THREE.DirectionalLight(0xffffff, 0.8);
dir.position.set(2, 3, 2);
scene.add(dir);

// ---------- default procedural texture ----------
// Keep it self-contained: a 64×64 canvas with a grid + subtle checker so
// boxes have something readable on their faces without any network fetch.
function buildDefaultTexture() {
  const size = 64;
  const cnv = document.createElement("canvas");
  cnv.width = cnv.height = size;
  const g = cnv.getContext("2d");
  // base gradient
  const grad = g.createLinearGradient(0, 0, size, size);
  grad.addColorStop(0, "#c9cfdc");
  grad.addColorStop(1, "#8f95a4");
  g.fillStyle = grad;
  g.fillRect(0, 0, size, size);
  // subtle checker
  g.fillStyle = "rgba(0,0,0,0.08)";
  const step = size / 8;
  for (let y = 0; y < 8; y++)
    for (let x = 0; x < 8; x++)
      if ((x + y) & 1) g.fillRect(x * step, y * step, step, step);
  // border
  g.strokeStyle = "rgba(30,40,60,0.75)";
  g.lineWidth = 2;
  g.strokeRect(1, 1, size - 2, size - 2);
  // center cross
  g.strokeStyle = "rgba(30,40,60,0.35)";
  g.lineWidth = 1;
  g.beginPath();
  g.moveTo(size / 2, 4); g.lineTo(size / 2, size - 4);
  g.moveTo(4, size / 2); g.lineTo(size - 4, size / 2);
  g.stroke();
  const tex = new THREE.CanvasTexture(cnv);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  return tex;
}
const DEFAULT_TEXTURE = buildDefaultTexture();

// ---------- model state ----------
let model = null;          // the JSON in memory
let meshGroup = null;       // THREE.Group holding face mesh + vertex spheres
let faceMesh = null;        // THREE.Mesh (triangles)
let vertsObj = null;        // THREE.InstancedMesh of vertex spheres (for picking)
const vertMatrix = new THREE.Matrix4();

const selectionProxy = new THREE.Object3D();
scene.add(selectionProxy);

let selectedIndex = -1;

const SPHERE_GEO = new THREE.SphereGeometry(VERT_RADIUS, 8, 6);
const SPHERE_MAT = new THREE.MeshBasicMaterial({ color: 0xf5c451 });

const FACE_MAT = new THREE.MeshLambertMaterial({
  color: 0xffffff, map: DEFAULT_TEXTURE,
  flatShading: true, side: THREE.DoubleSide,
});
const EDGE_MAT = new THREE.LineBasicMaterial({ color: 0x4b5160 });

// ---------- transform controls (the translate gizmo) ----------
const tcontrol = new TransformControls(camera, renderer.domElement);
tcontrol.setMode("translate");
tcontrol.setTranslationSnap(GRID);
tcontrol.setSpace("world");
tcontrol.size = 0.6;
tcontrol.visible = false;
tcontrol.enabled = false;
scene.add(tcontrol);

tcontrol.addEventListener("dragging-changed", (e) => {
  orbit.enabled = !e.value;
  if (!e.value) scheduleAutosave(); // save when drag ends
});
tcontrol.addEventListener("change", () => {
  if (selectedIndex < 0 || !model) return;
  // Snap absolute position to grid so initial offsets line up.
  selectionProxy.position.x = Math.round(selectionProxy.position.x / GRID) * GRID;
  selectionProxy.position.y = Math.round(selectionProxy.position.y / GRID) * GRID;
  selectionProxy.position.z = Math.round(selectionProxy.position.z / GRID) * GRID;
  model.vertices[selectedIndex] = [
    +selectionProxy.position.x.toFixed(4),
    +selectionProxy.position.y.toFixed(4),
    +selectionProxy.position.z.toFixed(4),
  ];
  rebuildGeometry();
  updateInfo();
});

// ---------- UVs ----------
// For each triangle, pick the axis (X/Y/Z) whose normal component is largest
// and project the triangle onto the other two axes so the default texture
// shows through on every face regardless of orientation.
function buildUVs(positions) {
  const uv = new Float32Array((positions.length / 3) * 2);
  for (let i = 0; i < positions.length; i += 9) {
    const ax = positions[i],     ay = positions[i + 1], az = positions[i + 2];
    const bx = positions[i + 3], by = positions[i + 4], bz = positions[i + 5];
    const cx = positions[i + 6], cy = positions[i + 7], cz = positions[i + 8];
    // face normal via cross product
    const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    const nx = Math.abs(e1y * e2z - e1z * e2y);
    const ny = Math.abs(e1z * e2x - e1x * e2z);
    const nz = Math.abs(e1x * e2y - e1y * e2x);
    let u0, v0, u1, v1, u2, v2;
    if (nx >= ny && nx >= nz) { // project onto Y,Z
      u0 = az; v0 = ay; u1 = bz; v1 = by; u2 = cz; v2 = cy;
    } else if (ny >= nx && ny >= nz) { // project onto X,Z
      u0 = ax; v0 = az; u1 = bx; v1 = bz; u2 = cx; v2 = cz;
    } else { // project onto X,Y
      u0 = ax; v0 = ay; u1 = bx; v1 = by; u2 = cx; v2 = cy;
    }
    const j = (i / 3) * 2;
    uv[j    ] = u0; uv[j + 1] = v0;
    uv[j + 2] = u1; uv[j + 3] = v1;
    uv[j + 4] = u2; uv[j + 5] = v2;
  }
  return uv;
}

// ---------- build / rebuild mesh from model ----------
function clearMesh() {
  if (meshGroup) {
    scene.remove(meshGroup);
    meshGroup.traverse((o) => {
      if (o.geometry) o.geometry.dispose();
    });
  }
  meshGroup = null;
  faceMesh = null;
  vertsObj = null;
}

function buildMesh() {
  clearMesh();
  if (!model) return;

  meshGroup = new THREE.Group();

  const positions = new Float32Array(model.triangles.length * 3 * 3);
  let p = 0;
  for (const tri of model.triangles) {
    for (const idx of tri) {
      const v = model.vertices[idx];
      positions[p++] = v[0]; positions[p++] = v[1]; positions[p++] = v[2];
    }
  }
  const faceGeo = new THREE.BufferGeometry();
  faceGeo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  faceGeo.setAttribute("uv", new THREE.BufferAttribute(buildUVs(positions), 2));
  faceGeo.computeVertexNormals();
  faceMesh = new THREE.Mesh(faceGeo, FACE_MAT);
  meshGroup.add(faceMesh);

  // Wireframe edges
  const edges = new THREE.EdgesGeometry(faceGeo, 1);
  meshGroup.add(new THREE.LineSegments(edges, EDGE_MAT));

  // Vertex spheres via InstancedMesh for cheap picking
  vertsObj = new THREE.InstancedMesh(SPHERE_GEO, SPHERE_MAT, Math.max(1, model.vertices.length));
  for (let i = 0; i < model.vertices.length; i++) {
    const v = model.vertices[i];
    vertMatrix.makeTranslation(v[0], v[1], v[2]);
    vertsObj.setMatrixAt(i, vertMatrix);
  }
  vertsObj.count = model.vertices.length;
  vertsObj.instanceMatrix.needsUpdate = true;
  meshGroup.add(vertsObj);

  scene.add(meshGroup);
}

function rebuildGeometry() {
  if (!faceMesh || !vertsObj) return;
  const pos = faceMesh.geometry.attributes.position.array;
  let p = 0;
  for (const tri of model.triangles) {
    for (const idx of tri) {
      const v = model.vertices[idx];
      pos[p++] = v[0]; pos[p++] = v[1]; pos[p++] = v[2];
    }
  }
  faceMesh.geometry.attributes.position.needsUpdate = true;
  faceMesh.geometry.computeVertexNormals();
  // Re-project UVs because moving a vertex changes face orientation.
  const uv = buildUVs(pos);
  faceMesh.geometry.setAttribute("uv", new THREE.BufferAttribute(uv, 2));

  if (selectedIndex >= 0) {
    const v = model.vertices[selectedIndex];
    vertMatrix.makeTranslation(v[0], v[1], v[2]);
    vertsObj.setMatrixAt(selectedIndex, vertMatrix);
    vertsObj.instanceMatrix.needsUpdate = true;
  }
}

// ---------- selection ----------
function selectVertex(i) {
  selectedIndex = i;
  if (i < 0 || !model || i >= model.vertices.length) {
    selectedIndex = -1;
    tcontrol.detach();
    tcontrol.visible = false;
    tcontrol.enabled = false;
    updateInfo();
    return;
  }
  const v = model.vertices[i];
  selectionProxy.position.set(v[0], v[1], v[2]);
  if (currentTool === "translate") {
    tcontrol.attach(selectionProxy);
    tcontrol.visible = true;
    tcontrol.enabled = true;
  }
  updateInfo();
}

function updateInfo() {
  const info = document.getElementById("info");
  if (selectedIndex < 0 || !model) {
    const n = model ? model.vertices.length : 0;
    const t = model ? model.triangles.length : 0;
    info.textContent = `no selection · ${n} verts, ${t} tris`;
  } else {
    const v = model.vertices[selectedIndex];
    info.textContent = `vertex #${selectedIndex}  (${v[0].toFixed(2)}, ${v[1].toFixed(2)}, ${v[2].toFixed(2)})`;
  }
}

// ---------- tool modes ----------
let currentTool = "grab";

function setTool(name) {
  currentTool = name;
  document.getElementById("tool-grab").classList.toggle("active", name === "grab");
  document.getElementById("tool-translate").classList.toggle("active", name === "translate");
  if (name === "translate" && selectedIndex >= 0) {
    tcontrol.attach(selectionProxy);
    tcontrol.visible = true;
    tcontrol.enabled = true;
  } else {
    tcontrol.detach();
    tcontrol.visible = false;
    tcontrol.enabled = false;
  }
}

// ---------- picking ----------
const raycaster = new THREE.Raycaster();
raycaster.params.Points = { threshold: VERT_RADIUS };
const ndc = new THREE.Vector2();

renderer.domElement.addEventListener("pointerdown", (ev) => {
  if (ev.button !== 0) return;         // left only
  if (currentTool !== "grab") return;  // in translate mode, clicks drive the gizmo
  if (tcontrol.dragging) return;
  if (!vertsObj) return;

  const rect = renderer.domElement.getBoundingClientRect();
  ndc.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
  ndc.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(ndc, camera);

  const hits = raycaster.intersectObject(vertsObj, false);
  if (hits.length > 0) {
    selectVertex(hits[0].instanceId);
  } else {
    selectVertex(-1);
  }
});

// ---------- keyboard ----------
window.addEventListener("keydown", (ev) => {
  if (ev.target.tagName === "INPUT") return;
  switch (ev.key.toLowerCase()) {
    case "g": setTool("grab"); break;
    case "t": setTool("translate"); break;
    case "b": addBox(); break;
    case "escape": selectVertex(-1); break;
  }
});

// ---------- box spawning ----------
// Append a 0.2-unit cube centred somewhere close to the current view target so
// the user sees it appear. Each box is registered as its own "part" entry.
let spawnCount = 0;
function addBox() {
  if (!model) {
    model = emptyModel();
  }
  const vStart = model.vertices.length;
  const s = 0.1; // half-size → 0.2-unit cube
  // Offset each new cube a bit so they don't all stack.
  const ox = Math.round((spawnCount % 4 - 1.5) * 0.3 / GRID) * GRID;
  const oz = Math.round((Math.floor(spawnCount / 4) - 1) * 0.3 / GRID) * GRID;
  const oy = 0.0;
  spawnCount++;
  const verts = [
    [-s + ox,      oy, -s + oz],
    [ s + ox,      oy, -s + oz],
    [ s + ox,      oy,  s + oz],
    [-s + ox,      oy,  s + oz],
    [-s + ox, 2 * s + oy, -s + oz],
    [ s + ox, 2 * s + oy, -s + oz],
    [ s + ox, 2 * s + oy,  s + oz],
    [-s + ox, 2 * s + oy,  s + oz],
  ];
  const tris = [
    [0, 2, 1], [0, 3, 2],
    [4, 5, 6], [4, 6, 7],
    [0, 1, 5], [0, 5, 4],
    [3, 6, 2], [3, 7, 6],
    [0, 4, 7], [0, 7, 3],
    [1, 2, 6], [1, 6, 5],
  ];
  for (const v of verts) model.vertices.push(v);
  for (const t of tris) model.triangles.push([t[0] + vStart, t[1] + vStart, t[2] + vStart]);
  if (!Array.isArray(model.parts)) model.parts = [];
  model.parts.push({
    name: `box_${model.parts.length + 1}`,
    vertex_start: vStart,
    vertex_count: 8,
  });
  buildMesh();
  selectVertex(-1);
  scheduleAutosave();
}

function emptyModel() {
  return { name: "untitled", grid: GRID, parts: [], vertices: [], triangles: [] };
}

// ---------- load / save ----------
async function loadModel() {
  // Priority: localStorage autosave → default-URL fetch → empty fallback.
  const restored = loadAutosave();
  if (restored) {
    model = restored;
    buildMesh();
    selectVertex(-1);
    setAutosaveStatus("restored");
    return;
  }
  try {
    const res = await fetch(DEFAULT_MODEL_URL);
    if (!res.ok) throw new Error(res.statusText);
    model = await res.json();
    buildMesh();
    selectVertex(-1);
    setAutosaveStatus("idle");
  } catch (err) {
    console.warn("Could not fetch default model:", err);
    model = emptyModel();
    buildMesh();
    addBox(); // give the user something to edit
    setAutosaveStatus("idle (new)");
  }
}

document.getElementById("file-load").addEventListener("change", async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  const text = await file.text();
  try {
    model = JSON.parse(text);
    if (!Array.isArray(model.vertices) || !Array.isArray(model.triangles)) {
      throw new Error("missing vertices/triangles");
    }
    buildMesh();
    selectVertex(-1);
    scheduleAutosave();
  } catch (err) {
    alert("Invalid model JSON: " + err.message);
  }
  ev.target.value = "";
});

// Manual save: writes a .json via <a download>.  On iOS/Android this
// triggers the OS download or share sheet so the user can save to Files.
document.getElementById("btn-save").addEventListener("click", () => {
  if (!model) return;
  const blob = new Blob([JSON.stringify(model, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = (model.name || "model") + ".json";
  a.rel = "noopener";
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

document.getElementById("btn-reset").addEventListener("click", () => {
  camera.position.copy(DEFAULT_CAM);
  orbit.target.copy(DEFAULT_TARGET);
  orbit.update();
});

document.getElementById("btn-add-box").addEventListener("click", addBox);

document.getElementById("btn-new-model").addEventListener("click", () => {
  if (!confirm("Start a new empty model? Your current autosave will be overwritten the next time you edit.")) return;
  model = emptyModel();
  spawnCount = 0;
  addBox(); // spawn one box so there's something visible
});

document.getElementById("tool-grab").addEventListener("click", () => setTool("grab"));
document.getElementById("tool-translate").addEventListener("click", () => setTool("translate"));

document.getElementById("chk-texture").addEventListener("change", (ev) => {
  FACE_MAT.map = ev.target.checked ? DEFAULT_TEXTURE : null;
  FACE_MAT.color.set(ev.target.checked ? 0xffffff : 0xcad0db);
  FACE_MAT.needsUpdate = true;
});

// ---------- autosave ----------
let autosaveTimer = null;
function setAutosaveStatus(msg) {
  const el = document.getElementById("autosave-status");
  if (el) el.textContent = "autosave: " + msg;
}

function scheduleAutosave() {
  if (autosaveTimer) clearTimeout(autosaveTimer);
  autosaveTimer = setTimeout(() => {
    if (!model) return;
    try {
      localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(model));
      const t = new Date();
      setAutosaveStatus("saved " + t.toTimeString().slice(0, 8));
    } catch (err) {
      setAutosaveStatus("failed: " + err.message);
    }
  }, 300);
}

function loadAutosave() {
  try {
    const raw = localStorage.getItem(AUTOSAVE_KEY);
    if (!raw) return null;
    const d = JSON.parse(raw);
    if (!Array.isArray(d.vertices) || !Array.isArray(d.triangles)) return null;
    return d;
  } catch (err) {
    console.warn("autosave load failed", err);
    return null;
  }
}

// ---------- resize ----------
window.addEventListener("resize", () => {
  camera.aspect = viewport.clientWidth / viewport.clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(viewport.clientWidth, viewport.clientHeight);
});

// ---------- render loop ----------
function tick() {
  requestAnimationFrame(tick);
  orbit.update();
  renderer.render(scene, camera);
}
tick();

loadModel();
