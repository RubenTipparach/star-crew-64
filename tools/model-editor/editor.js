// star-crew-64 model editor
//
// Minimal two-tool editor:
//   - Grab: click a vertex to select it
//   - Translate: drag the gizmo (snaps to GRID = 0.1)
//
// Model format:
//   { name, grid, vertices: [[x,y,z], ...], triangles: [[i,j,k], ...], parts?: [...] }

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { TransformControls } from "three/addons/controls/TransformControls.js";

const GRID = 0.1;
const VERT_RADIUS = 0.02;

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

// ---------- model state ----------
let model = null;          // the JSON in memory
let meshGroup = null;       // THREE.Group holding face mesh + vertex spheres
let faceMesh = null;        // THREE.Mesh (triangles)
let vertsObj = null;        // THREE.InstancedMesh of vertex spheres (for picking)
let vertMatrix = new THREE.Matrix4();

const selectionProxy = new THREE.Object3D();
scene.add(selectionProxy);

let selectedIndex = -1;

const SPHERE_GEO = new THREE.SphereGeometry(VERT_RADIUS, 8, 6);
const SPHERE_MAT = new THREE.MeshBasicMaterial({ color: 0xf5c451 });
const SPHERE_MAT_SEL = new THREE.MeshBasicMaterial({ color: 0x3b82f6 });

const FACE_MAT = new THREE.MeshLambertMaterial({
  color: 0xcad0db, flatShading: true, side: THREE.DoubleSide,
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
});
tcontrol.addEventListener("change", () => {
  if (selectedIndex < 0 || !model) return;
  // Snap proxy position to grid (TransformControls snaps deltas, but we snap
  // absolute position too so initial offsets line up on the grid.)
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
  faceGeo.computeVertexNormals();
  faceMesh = new THREE.Mesh(faceGeo, FACE_MAT);
  meshGroup.add(faceMesh);

  // Wireframe edges
  const edges = new THREE.EdgesGeometry(faceGeo, 1);
  meshGroup.add(new THREE.LineSegments(edges, EDGE_MAT));

  // Vertex spheres via InstancedMesh for cheap picking
  vertsObj = new THREE.InstancedMesh(SPHERE_GEO, SPHERE_MAT, model.vertices.length);
  for (let i = 0; i < model.vertices.length; i++) {
    const v = model.vertices[i];
    vertMatrix.makeTranslation(v[0], v[1], v[2]);
    vertsObj.setMatrixAt(i, vertMatrix);
  }
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

  // Only the selected vert has moved; update that instance transform.
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
  if (i < 0) {
    tcontrol.detach();
    tcontrol.visible = false;
    tcontrol.enabled = false;
    // reset all instance colors
    if (vertsObj) vertsObj.material = SPHERE_MAT;
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
    info.textContent = "no selection";
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
    // click empty space = deselect
    selectVertex(-1);
  }
});

// ---------- keyboard ----------
window.addEventListener("keydown", (ev) => {
  if (ev.target.tagName === "INPUT") return;
  switch (ev.key.toLowerCase()) {
    case "g": setTool("grab"); break;
    case "t": setTool("translate"); break;
    case "escape": selectVertex(-1); break;
  }
});

// ---------- load / save ----------
async function loadModelFromURL(url) {
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(res.statusText);
    model = await res.json();
    buildMesh();
    selectVertex(-1);
  } catch (err) {
    console.warn("Could not fetch default model:", err);
    // fallback: empty cube so the editor is still usable when opened as file://
    model = {
      name: "untitled",
      grid: GRID,
      vertices: [
        [-0.1, 0, -0.1], [0.1, 0, -0.1], [0.1, 0, 0.1], [-0.1, 0, 0.1],
        [-0.1, 0.2, -0.1], [0.1, 0.2, -0.1], [0.1, 0.2, 0.1], [-0.1, 0.2, 0.1],
      ],
      triangles: [
        [0,2,1],[0,3,2],[4,5,6],[4,6,7],
        [0,1,5],[0,5,4],[3,6,2],[3,7,6],
        [0,4,7],[0,7,3],[1,2,6],[1,6,5],
      ],
    };
    buildMesh();
  }
}

document.getElementById("file-load").addEventListener("change", async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  const text = await file.text();
  try {
    model = JSON.parse(text);
    buildMesh();
    selectVertex(-1);
  } catch (err) {
    alert("Invalid JSON: " + err.message);
  }
  ev.target.value = "";
});

document.getElementById("btn-save").addEventListener("click", () => {
  if (!model) return;
  const blob = new Blob([JSON.stringify(model, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = (model.name || "model") + ".json";
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
});

document.getElementById("btn-reset").addEventListener("click", () => {
  camera.position.copy(DEFAULT_CAM);
  orbit.target.copy(DEFAULT_TARGET);
  orbit.update();
});

document.getElementById("tool-grab").addEventListener("click", () => setTool("grab"));
document.getElementById("tool-translate").addEventListener("click", () => setTool("translate"));

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

loadModelFromURL(DEFAULT_MODEL_URL);
