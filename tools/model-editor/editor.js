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
const AUTOSAVE_KEY = "starCrew64.modelEditor.v2"; // v2: multi-tab

// Paths to the in-repo asset catalogs (the workflow copies these into _site/).
const MODELS_MANIFEST_URL   = "../../assets/models/models.json";
const TEXTURES_MANIFEST_URL = "../../assets/textures/textures.json";
const MODEL_DIR_URL         = "../../assets/models/";
const TEXTURE_DIR_URL       = "../../assets/textures/";

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
// Each tab = { id, name, model, textureKey } where textureKey is "default",
// "none", or the name of a texture in the textures manifest.
// `model` is a live alias pointing at the active tab's model so the rest of
// the editor continues to work against a single reference.
let tabs = [];
let activeTabId = null;
let model = null;           // the active tab's model
let meshGroup = null;       // THREE.Group holding face mesh + vertex spheres
let faceMesh = null;        // THREE.Mesh (triangles)
let vertsObj = null;        // THREE.InstancedMesh of vertex spheres (for picking)
const vertMatrix = new THREE.Matrix4();

function newTabId() {
  return "tab_" + Date.now().toString(36) + "_" + Math.random().toString(36).slice(2, 6);
}
function newTab(name, modelPayload, textureKey = "default") {
  return {
    id: newTabId(),
    name: name || "Untitled",
    model: modelPayload || emptyModel(name),
    textureKey,
  };
}
function activeTab() { return tabs.find(t => t.id === activeTabId) || null; }

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

// Return a Float32Array of UVs sized triangles.length*3*2. If the model
// already carries a hand-edited `uvs` array, use that verbatim; otherwise
// fall back to the planar projection above.
function getUVsFor(model, positions) {
  const n = model.triangles.length * 3;
  if (Array.isArray(model.uvs) && model.uvs.length >= n) {
    const uv = new Float32Array(n * 2);
    for (let i = 0; i < n; i++) {
      const p = model.uvs[i];
      uv[i * 2    ] = p && typeof p[0] === "number" ? p[0] : 0;
      uv[i * 2 + 1] = p && typeof p[1] === "number" ? p[1] : 0;
    }
    return uv;
  }
  return buildUVs(positions);
}

// Populate model.uvs from the current computed/stored UVs so subsequent
// edits have somewhere to write. Called lazily on the first UV edit.
function ensureModelUVs() {
  if (!model) return;
  if (Array.isArray(model.uvs) && model.uvs.length >= model.triangles.length * 3) return;
  const positions = new Float32Array(model.triangles.length * 3 * 3);
  let p = 0;
  for (const tri of model.triangles) {
    for (const idx of tri) {
      const v = model.vertices[idx];
      positions[p++] = v[0]; positions[p++] = v[1]; positions[p++] = v[2];
    }
  }
  const uv = buildUVs(positions);
  model.uvs = [];
  for (let i = 0; i < uv.length; i += 2) {
    model.uvs.push([+uv[i].toFixed(4), +uv[i + 1].toFixed(4)]);
  }
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
  faceGeo.setAttribute("uv", new THREE.BufferAttribute(getUVsFor(model, positions), 2));
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
  // If the UV editor is open, repaint it against the newly-built attribute.
  if (typeof drawUV === "function") drawUV();
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
  // If the model has stored UVs, leave them alone (they represent authored
  // texture mapping). Otherwise re-project from the new positions.
  if (!Array.isArray(model.uvs) || model.uvs.length < model.triangles.length * 3) {
    const uv = buildUVs(pos);
    faceMesh.geometry.setAttribute("uv", new THREE.BufferAttribute(uv, 2));
  }

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
// Append a 0.2-unit cube; offset so repeated spawns don't stack. Registered
// as its own "part" entry so the N64 build can draw it independently.
function addBox() {
  if (!model) return;
  const vStart = model.vertices.length;
  const s = 0.1;
  const parts = Array.isArray(model.parts) ? model.parts : (model.parts = []);
  const n = parts.length; // use the part count as the deterministic offset key
  const ox = Math.round((n % 4 - 1.5) * 0.3 / GRID) * GRID;
  const oz = Math.round((Math.floor(n / 4) - 1) * 0.3 / GRID) * GRID;
  const verts = [
    [-s + ox,     0, -s + oz], [ s + ox,     0, -s + oz],
    [ s + ox,     0,  s + oz], [-s + ox,     0,  s + oz],
    [-s + ox, 2 * s, -s + oz], [ s + ox, 2 * s, -s + oz],
    [ s + ox, 2 * s,  s + oz], [-s + ox, 2 * s,  s + oz],
  ];
  const tris = [
    [0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
    [0, 1, 5], [0, 5, 4], [3, 6, 2], [3, 7, 6],
    [0, 4, 7], [0, 7, 3], [1, 2, 6], [1, 6, 5],
  ];
  for (const v of verts) model.vertices.push(v);
  for (const t of tris) model.triangles.push([t[0] + vStart, t[1] + vStart, t[2] + vStart]);
  parts.push({ name: `box_${n + 1}`, vertex_start: vStart, vertex_count: 8 });
  // If the model already carries authored UVs, keep them in sync by
  // appending planar-projected UVs for the new triangles.
  if (Array.isArray(model.uvs)) {
    const extraPos = new Float32Array(tris.length * 3 * 3);
    let p = 0;
    for (const t of tris) {
      for (const idx of t) {
        const v = verts[idx];
        extraPos[p++] = v[0]; extraPos[p++] = v[1]; extraPos[p++] = v[2];
      }
    }
    const extraUV = buildUVs(extraPos);
    for (let i = 0; i < extraUV.length; i += 2) {
      model.uvs.push([+extraUV[i].toFixed(4), +extraUV[i + 1].toFixed(4)]);
    }
  }
  buildMesh();
  selectVertex(-1);
  scheduleAutosave();
}

function emptyModel(name) {
  return {
    name: name || "untitled",
    grid: GRID,
    parts: [],
    vertices: [],
    triangles: [],
  };
}

// ---------- textures ----------
// Built-in textures from /assets/textures/textures.json. Cached by name so we
// only decode each PNG once per session.
const TEXTURE_CACHE = new Map();
let textureManifest = [];
// Current texture's pixel size, drives UV-editor snap grid.
// Default texture is 64×64; unknown / missing → 16×16 fallback.
const DEFAULT_TEX_SIZE = { w: 64, h: 64 };
const NO_TEX_SIZE = { w: 16, h: 16 };
let currentTexSize = { ...DEFAULT_TEX_SIZE };
// Listeners fire whenever the bound texture or its pixel size changes so
// the UV editor can re-draw without owning the three.js material state.
const textureChangeListeners = new Set();
function notifyTextureChanged() { for (const fn of textureChangeListeners) fn(); }

function getTextureByKey(key) {
  if (key === "none") return null;
  if (key === "default" || !key) return DEFAULT_TEXTURE;
  if (TEXTURE_CACHE.has(key)) return TEXTURE_CACHE.get(key);
  const entry = textureManifest.find(t => t.name === key);
  if (!entry) return DEFAULT_TEXTURE;
  const loader = new THREE.TextureLoader();
  const tex = loader.load(TEXTURE_DIR_URL + entry.source, (t) => {
    t.needsUpdate = true;
    // If this texture is currently bound, capture its true pixel size so
    // the UV editor can snap to its texel grid.
    if (FACE_MAT.map === t && t.image) {
      currentTexSize = { w: t.image.width || 64, h: t.image.height || 64 };
      notifyTextureChanged();
    }
  });
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  TEXTURE_CACHE.set(key, tex);
  return tex;
}

function applyTextureKey(key) {
  const tex = getTextureByKey(key);
  FACE_MAT.map = tex;
  FACE_MAT.color.set(tex ? 0xffffff : 0xcad0db);
  FACE_MAT.needsUpdate = true;
  const sel = document.getElementById("texture-select");
  if (sel && sel.value !== key) sel.value = key;
  // Update cached pixel size for UV snapping.
  if (!tex) {
    currentTexSize = { ...NO_TEX_SIZE };
  } else if (tex === DEFAULT_TEXTURE) {
    currentTexSize = { ...DEFAULT_TEX_SIZE };
  } else if (tex.image && tex.image.width) {
    currentTexSize = { w: tex.image.width, h: tex.image.height };
  } else {
    // Image may still be decoding. Use 64×64 until the loader callback fires.
    currentTexSize = { w: 64, h: 64 };
  }
  notifyTextureChanged();
}

async function populateTextureSelect() {
  const sel = document.getElementById("texture-select");
  try {
    const res = await fetch(TEXTURES_MANIFEST_URL);
    if (!res.ok) throw new Error(res.statusText);
    const manifest = await res.json();
    textureManifest = Array.isArray(manifest.textures) ? manifest.textures : [];
    for (const t of textureManifest) {
      const opt = document.createElement("option");
      opt.value = t.name;
      opt.textContent = t.name;
      sel.appendChild(opt);
    }
  } catch (err) {
    console.warn("textures manifest fetch failed", err);
  }
  // The initial model load may have completed before the manifest arrived —
  // re-apply so named textures (e.g. "character") actually bind now.
  const t = activeTab();
  if (t) applyTextureKey(t.textureKey || "default");
}

// ---------- game models ----------
let modelManifest = [];
async function populateGameModels() {
  const sel = document.getElementById("game-model-select");
  try {
    const res = await fetch(MODELS_MANIFEST_URL);
    if (!res.ok) throw new Error(res.statusText);
    const manifest = await res.json();
    modelManifest = Array.isArray(manifest.models) ? manifest.models : [];
    sel.innerHTML = "";
    for (const m of modelManifest) {
      const opt = document.createElement("option");
      opt.value = m.name;
      opt.textContent = m.name + (m.description ? ` — ${m.description}` : "");
      sel.appendChild(opt);
    }
    if (!sel.options.length) {
      sel.innerHTML = '<option value="">(no models listed)</option>';
    }
  } catch (err) {
    console.warn("models manifest fetch failed", err);
    sel.innerHTML = '<option value="">(offline — no manifest)</option>';
  }
}

async function openGameModel(name) {
  const entry = modelManifest.find(m => m.name === name);
  if (!entry) return;
  try {
    const res = await fetch(MODEL_DIR_URL + entry.source);
    if (!res.ok) throw new Error(res.statusText);
    const payload = await res.json();
    payload.name = payload.name || entry.name;
    addTab(newTab(entry.name, payload, entry.texture || "default"));
  } catch (err) {
    alert("Load failed: " + err.message);
  }
}

// ---------- tabs ----------
function addTab(tab, { activate = true } = {}) {
  tabs.push(tab);
  if (activate) switchTab(tab.id);
  else renderTabs();
  scheduleAutosave();
}

function switchTab(id) {
  const next = tabs.find(t => t.id === id);
  if (!next) return;
  activeTabId = id;
  model = next.model;
  applyTextureKey(next.textureKey || "default");
  renderTabs();
  buildMesh();
  selectVertex(-1);
  scheduleAutosave();
}

function closeTab(id) {
  const idx = tabs.findIndex(t => t.id === id);
  if (idx < 0) return;
  tabs.splice(idx, 1);
  if (tabs.length === 0) {
    const blank = newTab("Untitled", emptyModel("Untitled"), "default");
    tabs.push(blank);
    activeTabId = blank.id;
    model = blank.model;
  } else if (activeTabId === id) {
    const next = tabs[Math.min(idx, tabs.length - 1)];
    activeTabId = next.id;
    model = next.model;
  }
  const active = activeTab();
  applyTextureKey(active ? (active.textureKey || "default") : "default");
  renderTabs();
  buildMesh();
  selectVertex(-1);
  scheduleAutosave();
}

function renameTab(id, name) {
  const t = tabs.find(x => x.id === id);
  if (!t || !name) return;
  t.name = name;
  if (t.model) t.model.name = name;
  renderTabs();
  scheduleAutosave();
}

function renderTabs() {
  const bar = document.getElementById("tabs-bar");
  if (!bar) return;
  bar.innerHTML = "";
  for (const t of tabs) {
    const el = document.createElement("button");
    el.className = "tab" + (t.id === activeTabId ? " active" : "");
    el.title = "Click to switch · double-click to rename";
    const label = document.createElement("span");
    label.textContent = t.name;
    el.appendChild(label);
    if (tabs.length > 1) {
      const close = document.createElement("span");
      close.className = "close";
      close.textContent = "×";
      close.title = "Close tab";
      close.addEventListener("click", (ev) => {
        ev.stopPropagation();
        closeTab(t.id);
      });
      el.appendChild(close);
    }
    el.addEventListener("click", () => switchTab(t.id));
    el.addEventListener("dblclick", () => {
      const n = prompt("Rename tab", t.name);
      if (n && n.trim()) renameTab(t.id, n.trim());
    });
    bar.appendChild(el);
  }
  const add = document.createElement("button");
  add.className = "new-tab";
  add.textContent = "+";
  add.title = "New empty tab";
  add.addEventListener("click", () => {
    const n = `Untitled ${tabs.length + 1}`;
    addTab(newTab(n, emptyModel(n), "default"));
    addBox();
  });
  bar.appendChild(add);
}

// ---------- load / save ----------
async function loadInitial() {
  // Priority: autosave (multi-tab) → default-URL fetch → empty fallback.
  if (loadAutosave()) {
    const t = activeTab();
    if (t) {
      applyTextureKey(t.textureKey || "default");
      buildMesh();
      selectVertex(-1);
    }
    renderTabs();
    setAutosaveStatus("restored");
    return;
  }
  try {
    const res = await fetch(MODEL_DIR_URL + "character.json");
    if (!res.ok) throw new Error(res.statusText);
    const payload = await res.json();
    addTab(newTab(payload.name || "character", payload, "character"));
    setAutosaveStatus("idle");
  } catch (err) {
    console.warn("Could not fetch default model:", err);
    addTab(newTab("Untitled", emptyModel("Untitled"), "default"));
    addBox();
    setAutosaveStatus("idle (new)");
  }
}

document.getElementById("file-load").addEventListener("change", async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  const text = await file.text();
  try {
    const payload = JSON.parse(text);
    if (!Array.isArray(payload.vertices) || !Array.isArray(payload.triangles)) {
      throw new Error("missing vertices/triangles");
    }
    const name = payload.name || file.name.replace(/\.json$/i, "") || "Imported";
    addTab(newTab(name, payload, "default"));
  } catch (err) {
    alert("Invalid model JSON: " + err.message);
  }
  ev.target.value = "";
});

// Manual save: writes the active tab's model as .json via <a download>. On
// iOS/Android this triggers the OS download or share sheet so the user can
// save to Files.
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
  const n = `Untitled ${tabs.length + 1}`;
  addTab(newTab(n, emptyModel(n), "default"));
  addBox();
});

document.getElementById("btn-load-game-model").addEventListener("click", () => {
  const sel = document.getElementById("game-model-select");
  if (sel.value) openGameModel(sel.value);
});

document.getElementById("tool-grab").addEventListener("click", () => setTool("grab"));
document.getElementById("tool-translate").addEventListener("click", () => setTool("translate"));

document.getElementById("texture-select").addEventListener("change", (ev) => {
  const t = activeTab();
  if (t) t.textureKey = ev.target.value;
  applyTextureKey(ev.target.value);
  scheduleAutosave();
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
    if (tabs.length === 0) return;
    try {
      const payload = {
        version: 2,
        activeId: activeTabId,
        tabs: tabs.map(t => ({
          id: t.id,
          name: t.name,
          textureKey: t.textureKey || "default",
          model: t.model,
        })),
      };
      localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(payload));
      const d = new Date();
      setAutosaveStatus("saved " + d.toTimeString().slice(0, 8));
    } catch (err) {
      setAutosaveStatus("failed: " + err.message);
    }
  }, 300);
}

function loadAutosave() {
  try {
    const raw = localStorage.getItem(AUTOSAVE_KEY);
    if (!raw) return false;
    const payload = JSON.parse(raw);
    if (payload && payload.version === 2 && Array.isArray(payload.tabs) && payload.tabs.length > 0) {
      tabs = payload.tabs
        .filter(t => t && t.model && Array.isArray(t.model.vertices) && Array.isArray(t.model.triangles))
        .map(t => ({
          id: t.id || newTabId(),
          name: t.name || "Untitled",
          textureKey: t.textureKey || "default",
          model: t.model,
        }));
      if (tabs.length === 0) return false;
      activeTabId = payload.activeId && tabs.find(t => t.id === payload.activeId)
        ? payload.activeId : tabs[0].id;
      model = tabs.find(t => t.id === activeTabId).model;
      return true;
    }
    // v1 fallback: a single raw model payload.
    if (payload && Array.isArray(payload.vertices) && Array.isArray(payload.triangles)) {
      const t = newTab(payload.name || "Restored", payload, "default");
      tabs = [t];
      activeTabId = t.id;
      model = t.model;
      return true;
    }
    return false;
  } catch (err) {
    console.warn("autosave load failed", err);
    return false;
  }
}

// ---------- UV editor ----------
// A 2D panel mirroring a unit-square UV space, with pixel-grid snap. The
// bound FACE_MAT.map (texture image) is painted as the background so the
// user can see where each UV point sits relative to the texture's pixels.
// Storage: on first edit we call ensureModelUVs() to convert whatever UVs
// the mesh was using (authored or planar-projected) into model.uvs, then
// mutate model.uvs in place and push updates into the three.js attribute.
const uvCanvas = document.getElementById("uv-canvas");
const uvCtx = uvCanvas.getContext("2d");
const uvPanel = document.getElementById("uv-panel");
const uvInfo = document.getElementById("uv-info");

let uvSelected = -1;   // corner index (triIdx*3 + corner) or -1
let uvDragging = false;

function uvPanelOpen() { return uvPanel.classList.contains("open"); }

function toggleUVPanel(force) {
  const next = typeof force === "boolean" ? force : !uvPanelOpen();
  uvPanel.classList.toggle("open", next);
  if (next) drawUV();
}

// UV → canvas coordinates. v is flipped because three.js UV origin is
// bottom-left but 2D canvas origin is top-left.
const UV_CANVAS_SIZE = 320;
function uvToCanvas(u, v) {
  return { x: u * UV_CANVAS_SIZE, y: (1 - v) * UV_CANVAS_SIZE };
}
function canvasToUV(x, y) {
  return { u: x / UV_CANVAS_SIZE, v: 1 - y / UV_CANVAS_SIZE };
}
function snapUV(u, v) {
  const w = Math.max(1, currentTexSize.w | 0);
  const h = Math.max(1, currentTexSize.h | 0);
  return { u: Math.round(u * w) / w, v: Math.round(v * h) / h };
}

// Read the current UVs to draw from. Prefer model.uvs if present (authored);
// otherwise read directly from the three.js attribute (planar-projected).
function readCurrentUVs() {
  if (!model) return null;
  const n = model.triangles.length * 3;
  if (Array.isArray(model.uvs) && model.uvs.length >= n) {
    return (i) => model.uvs[i];
  }
  if (faceMesh && faceMesh.geometry && faceMesh.geometry.attributes.uv) {
    const a = faceMesh.geometry.attributes.uv.array;
    return (i) => [a[i * 2], a[i * 2 + 1]];
  }
  return null;
}

function drawUV() {
  if (!uvPanelOpen()) return;
  const cw = UV_CANVAS_SIZE, ch = UV_CANVAS_SIZE;
  uvCtx.setTransform(1, 0, 0, 1, 0, 0);
  uvCtx.clearRect(0, 0, cw, ch);
  uvCtx.fillStyle = "#0d0e14";
  uvCtx.fillRect(0, 0, cw, ch);

  // Background: the texture image scaled to fill the canvas. For CanvasTexture
  // the `image` is an HTMLCanvasElement; TextureLoader results are Image.
  const tex = FACE_MAT.map;
  const img = tex && tex.image;
  if (img && img.width) {
    uvCtx.imageSmoothingEnabled = false;
    try { uvCtx.drawImage(img, 0, 0, cw, ch); } catch (_) { /* tainted? ignore */ }
  }

  // Pixel grid — only if each texel is visible enough (>= 4 canvas pixels).
  const texW = Math.max(1, currentTexSize.w | 0);
  const texH = Math.max(1, currentTexSize.h | 0);
  const cellW = cw / texW, cellH = ch / texH;
  if (cellW >= 4 && cellH >= 4) {
    uvCtx.strokeStyle = "rgba(120,160,255,0.25)";
    uvCtx.lineWidth = 1;
    uvCtx.beginPath();
    for (let x = 0; x <= texW; x++) {
      uvCtx.moveTo(x * cellW + 0.5, 0);
      uvCtx.lineTo(x * cellW + 0.5, ch);
    }
    for (let y = 0; y <= texH; y++) {
      uvCtx.moveTo(0, y * cellH + 0.5);
      uvCtx.lineTo(cw, y * cellH + 0.5);
    }
    uvCtx.stroke();
  }

  // Unit-square border
  uvCtx.strokeStyle = "rgba(255,255,255,0.3)";
  uvCtx.lineWidth = 1;
  uvCtx.strokeRect(0.5, 0.5, cw - 1, ch - 1);

  const get = readCurrentUVs();
  if (!model || !get) return;

  // UV triangles
  uvCtx.strokeStyle = "rgba(255,255,255,0.45)";
  uvCtx.lineWidth = 1;
  const triCount = model.triangles.length;
  for (let t = 0; t < triCount; t++) {
    const a = get(t * 3), b = get(t * 3 + 1), c = get(t * 3 + 2);
    if (!a || !b || !c) continue;
    const A = uvToCanvas(a[0], a[1]);
    const B = uvToCanvas(b[0], b[1]);
    const C = uvToCanvas(c[0], c[1]);
    uvCtx.beginPath();
    uvCtx.moveTo(A.x, A.y);
    uvCtx.lineTo(B.x, B.y);
    uvCtx.lineTo(C.x, C.y);
    uvCtx.closePath();
    uvCtx.stroke();
  }

  // UV points
  for (let i = 0; i < triCount * 3; i++) {
    const uv = get(i);
    if (!uv) continue;
    const P = uvToCanvas(uv[0], uv[1]);
    const selected = i === uvSelected;
    uvCtx.fillStyle = selected ? "#3b82f6" : "#f5c451";
    uvCtx.strokeStyle = "rgba(0,0,0,0.7)";
    uvCtx.lineWidth = 1;
    uvCtx.beginPath();
    uvCtx.arc(P.x, P.y, selected ? 5 : 3, 0, Math.PI * 2);
    uvCtx.fill();
    uvCtx.stroke();
  }
}

// Nearest-UV picker. Returns the corner index, or -1 if nothing within
// UV_PICK_RADIUS canvas pixels of (x,y).
const UV_PICK_RADIUS = 10;
function pickUVAt(x, y) {
  if (!model) return -1;
  const get = readCurrentUVs();
  if (!get) return -1;
  let best = -1, bestDist = UV_PICK_RADIUS * UV_PICK_RADIUS;
  for (let i = 0; i < model.triangles.length * 3; i++) {
    const uv = get(i);
    if (!uv) continue;
    const P = uvToCanvas(uv[0], uv[1]);
    const dx = P.x - x, dy = P.y - y;
    const d = dx * dx + dy * dy;
    if (d < bestDist) { bestDist = d; best = i; }
  }
  return best;
}

// Translate a pointer event to canvas-local coordinates (respects CSS scale).
function uvCanvasCoords(ev) {
  const rect = uvCanvas.getBoundingClientRect();
  const scaleX = uvCanvas.width / rect.width;
  const scaleY = uvCanvas.height / rect.height;
  return {
    x: (ev.clientX - rect.left) * scaleX,
    y: (ev.clientY - rect.top) * scaleY,
  };
}

// Commit a UV edit: snap, write to model.uvs, push into the geometry
// attribute so the 3D view reflects the change live, schedule autosave.
function commitUVEdit(cornerIndex, u, v) {
  const snapped = snapUV(u, v);
  ensureModelUVs();
  model.uvs[cornerIndex] = [+snapped.u.toFixed(4), +snapped.v.toFixed(4)];
  if (faceMesh && faceMesh.geometry && faceMesh.geometry.attributes.uv) {
    const arr = faceMesh.geometry.attributes.uv.array;
    arr[cornerIndex * 2    ] = snapped.u;
    arr[cornerIndex * 2 + 1] = snapped.v;
    faceMesh.geometry.attributes.uv.needsUpdate = true;
  }
  uvInfo.textContent = `corner ${cornerIndex} · uv (${snapped.u.toFixed(4)}, ${snapped.v.toFixed(4)}) · snap 1/${currentTexSize.w}×1/${currentTexSize.h}`;
  scheduleAutosave();
}

uvCanvas.addEventListener("pointerdown", (ev) => {
  if (ev.button !== 0) return;
  const { x, y } = uvCanvasCoords(ev);
  const hit = pickUVAt(x, y);
  uvSelected = hit;
  if (hit >= 0) {
    uvDragging = true;
    uvCanvas.setPointerCapture(ev.pointerId);
    const { u, v } = canvasToUV(x, y);
    commitUVEdit(hit, u, v);
  } else {
    uvInfo.textContent = "click a UV point · drag to move (pixel-snap)";
  }
  drawUV();
});

uvCanvas.addEventListener("pointermove", (ev) => {
  if (!uvDragging || uvSelected < 0) return;
  const { x, y } = uvCanvasCoords(ev);
  const { u, v } = canvasToUV(x, y);
  commitUVEdit(uvSelected, u, v);
  drawUV();
});

function endUVDrag(ev) {
  if (uvDragging && ev && uvCanvas.hasPointerCapture && uvCanvas.hasPointerCapture(ev.pointerId)) {
    uvCanvas.releasePointerCapture(ev.pointerId);
  }
  uvDragging = false;
}
uvCanvas.addEventListener("pointerup", endUVDrag);
uvCanvas.addEventListener("pointercancel", endUVDrag);

document.getElementById("btn-uv-toggle").addEventListener("click", () => toggleUVPanel());
document.getElementById("btn-uv-close").addEventListener("click", () => toggleUVPanel(false));
document.getElementById("btn-uv-fit").addEventListener("click", () => { uvSelected = -1; drawUV(); });

// Redraw on texture swap + whenever the user does anything that could move UVs.
textureChangeListeners.add(drawUV);
window.addEventListener("keydown", (ev) => {
  if (ev.key === "u" || ev.key === "U") {
    if (ev.target && (ev.target.tagName === "INPUT" || ev.target.tagName === "SELECT")) return;
    toggleUVPanel();
  }
});

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

// Kick off async manifest + initial-model loads in parallel; they're all
// independent. loadInitial handles both the autosave-restore and the
// default-character-fetch paths.
populateTextureSelect();
populateGameModels();
loadInitial();
