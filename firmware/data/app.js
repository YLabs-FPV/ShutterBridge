const OSD_FIELDS = ["Off", "Rec status", "Battery %", "Mode", "SD free", "Time left", "Resolution", "FPS"];
const CAM_TYPE_NAMES = ["DJI Osmo (Nano)", "DJI Action / 360", "GoPro"];
const OSMO_TYPE = 0, ACTION_TYPE = 1, GOPRO_TYPE = 2;
const CH_MIN = 900, CH_MAX = 2100, STEP = 25, GAP = 25;
const PIPS = [900, 1000, 1200, 1500, 1800, 2000, 2100];

const $ = (id) => document.getElementById(id);
const pct = (v) => ((v - CH_MIN) / (CH_MAX - CH_MIN)) * 100;
const clampCh = (v) => Math.max(CH_MIN, Math.min(CH_MAX, v));
const snap = (v) => clampCh(Math.round(v / STEP) * STEP);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const auxOptions = (cur) => {
  let o = `<option value="0"${cur === 0 ? " selected" : ""}>Disabled</option>`;
  o += `<option value="auto">Auto detect</option>`;
  for (let a = 1; a <= 14; a++)
    o += `<option value="${a}"${cur === a ? " selected" : ""}>AUX ${a}</option>`;
  return o;
};

// Auto-detect: while listening, watch for an AUX channel that moves and assign it.
let autoDetect = null;
function startAuto(i) {
  if (!chVals.length) {
    toast("No RC signal", true);
    $("m" + i + "aux").value = String(cfg.modes[i].aux);
    return;
  }
  autoDetect = { i, base: chVals.slice() };
  toast("Flip a switch to assign");
}

let cfg = null;
let chVals = [];
let boundMac = "";
let boundType = 0;

// Resolution and FPS aren't reported by the Osmo Nano
const osdFieldAvail = (k) => !(boundType === OSMO_TYPE && (k === 6 || k === 7));

// ---- Tabs ----
document.querySelectorAll(".tab").forEach((t) =>
  t.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((x) => x.classList.toggle("active", x === t));
    document.querySelectorAll(".panel").forEach((p) => (p.hidden = p.id !== "tab-" + t.dataset.tab));
  }));

async function loadConfig() {
  cfg = await (await fetch("/config.json")).json();
  boundMac = cfg.mac || "";
  boundType = +cfg.ctype || 0;
  $("ver").textContent = cfg.fw ? "v" + cfg.fw : "";

  $("osd").innerHTML = [0, 1, 2, 3]
    .map((i) => `<div class="row"><label>Custom Msg ${i}</label><div class="ctl">` +
      `<select id="s${i}">` +
      OSD_FIELDS.map((o, k) => osdFieldAvail(k) ?
        `<option value="${k}"${k === cfg.osd[i] ? " selected" : ""}>${o}</option>` : "").join("") +
      `</select></div></div>`)
    .join("");

  const svm = +cfg.svm || 0;
  const shutterExtra = (name) => name !== "Shutter" ? "" :
    `<div class="mode-vals" style="margin-top:8px">Video trigger
       <select id="svm">
         <option value="0"${svm === 0 ? " selected" : ""}>Momentary (toggle record)</option>
         <option value="1"${svm === 1 ? " selected" : ""}>2-position (hold to record)</option>
       </select></div>
     <p class="hint">Photo mode takes one photo per flick. Video mode: momentary toggles
       recording, 2-position records while the switch is held.</p>`;

  const msm = +cfg.msm || 0;
  const modeExtra = (name) => name !== "Camera Mode" ? "" :
    `<div class="mode-vals" style="margin-top:8px">Mode switch
       <select id="msm">
         <option value="0"${msm === 0 ? " selected" : ""}>Momentary (toggle photo/video)</option>
         <option value="1"${msm === 1 ? " selected" : ""}>2-position (in Photo, out Video)</option>
       </select></div>
     <p class="hint">Momentary toggles photo/video each flick. 2-position sets
       Photo in range, Video out. Action and GoPro only.</p>`;

  $("modes").innerHTML = cfg.modes
    .map((m, i) =>
      `<div class="mode" id="mode${i}">
         <div class="mode-head">
           <span class="mode-name">${m.name}</span>
           <select id="m${i}aux">${auxOptions(m.aux)}</select>
         </div>
         <div class="sl" id="sl${i}">
           <div class="sl-track"></div>
           <div class="sl-range"></div>
           <div class="sl-thumb" data-h="min"></div>
           <div class="sl-thumb" data-h="max"></div>
           <div class="sl-marker"></div>
         </div>
         <div class="sl-pips">${PIPS.map((p) => `<span style="left:${pct(p)}%">${p}</span>`).join("")}</div>
         <div class="mode-vals">Range <b id="m${i}min">${m.min}</b> to <b id="m${i}max">${m.max}</b> us</div>
         ${shutterExtra(m.name)}${modeExtra(m.name)}
       </div>`)
    .join("");
  cfg.modes.forEach((_, i) => wireSlider(i));

  $("ssid").value = cfg.ssid;
  $("pass").value = cfg.pass;
  $("dis").checked = !!cfg.dis;
  $("ble").value = cfg.ble != null ? cfg.ble : 0;
  $("clk").checked = cfg.clk !== 0;
  $("tz").value = (+cfg.tz || 0) / 60;
  $("op").value = +cfg.op || 0;
  $("rdly").value = +cfg.rdly || 0;
  $("sdly").value = +cfg.sdly || 0;
  $("op").addEventListener("change", applyOpMode);
  renderBound();
  applyOpMode();
  setDirty(false);
}

function applyOpMode() {
  const rec = +$("op").value === 1;
  $("funccard").classList.toggle("disabled", rec);
  $("controlscard").classList.toggle("disabled", rec);
  $("ophint").textContent = rec
    ? "Recording starts on arm and stops on disarm, using the delays above. Manual camera controls are off"
    : "Camera actions are mapped to the AUX switches below";
}

function renderBound() {
  $("boundModel").textContent = boundMac ? (CAM_TYPE_NAMES[boundType] || "Camera") : "No camera";
  $("bound").textContent = boundMac || "not bound";

  // Controls: mode switch for Action/GoPro, presets for GoPro only
  const canMode = boundMac && (boundType === ACTION_TYPE || boundType === GOPRO_TYPE);
  $("controlscard").hidden = !canMode;
  $("presetctl").hidden = boundType !== GOPRO_TYPE;
  $("clockcard").hidden = boundType !== GOPRO_TYPE;  // GPS clock sync is GoPro-only

  // Channels: hide functions the bound camera can't do.
  if (cfg) cfg.modes.forEach((m, i) => {
    if (m.name.startsWith("Preset")) $("mode" + i).hidden = boundType !== GOPRO_TYPE;
    if (m.name === "Camera Mode") $("mode" + i).hidden = boundType === OSMO_TYPE;
  });
}

// ---- Binding wizard ----
let wizType = 0;

function wizShow(step) {
  ["wizType", "wizScan", "wizDone"].forEach((id) => ($(id).hidden = id !== step));
}
function openWizard() {
  $("wizard").hidden = false;
  wizShow("wizType");
}
function closeWizard() {
  $("wizard").hidden = true;
}

async function wizScan() {
  wizShow("wizScan");
  $("wizList").innerHTML =
    '<div class="scanning"><div class="spinner"></div><span>Scanning for cameras</span></div>';
  let list = null;
  for (let i = 0; i < 20 && list === null; i++) {
    try {
      const j = await (await fetch("/scan")).json();
      if (Array.isArray(j)) list = j;
      else await sleep(500);
    } catch (e) { await sleep(500); }
  }
  list = (list || []).filter((d) => d.type === wizType);
  if (!list.length) {
    $("wizList").innerHTML =
      `<p class="hint">No matching camera found. Power it on, disconnect any phone app, then <a href="#" id="wizRetry">scan again</a>.</p>`;
    $("wizRetry").addEventListener("click", (e) => { e.preventDefault(); wizScan(); });
    return;
  }
  $("wizList").innerHTML = list.map((d) =>
    `<div class="scan-item" data-mac="${d.mac}">
       <div class="scan-info">
         <div class="scan-model">${d.model || "Camera"}</div>
         <div class="scan-name">${d.name || "(no name)"}</div>
       </div>
       <span class="mac">${d.mac}</span>
     </div>`).join("");
  document.querySelectorAll("#wizList .scan-item").forEach((el) =>
    el.addEventListener("click", () => wizBind(el.dataset.mac)));
}

function wizBind(mac) {
  boundMac = mac;
  boundType = wizType;
  renderBound();
  wizShow("wizDone");
  // Fire the save but don't await it: the device persists to NVS, and a slow
  // or dropped response must not freeze the countdown.
  postSave().catch(() => { });
  let n = 3;
  $("wizCount").textContent = n;
  const t = setInterval(() => {
    n--;
    $("wizCount").textContent = Math.max(n, 0);
    if (n <= 0) {
      clearInterval(t);
      closeWizard();
      rebootFlow();
    }
  }, 1000);
}

// ---- Save ----
function collectConfig() {
  const b = new URLSearchParams();
  for (let i = 0; i < 4; i++) b.set("s" + i, $("s" + i).value);
  cfg.modes.forEach((m, i) => {
    b.set("m" + i + "aux", $("m" + i + "aux").value);
    b.set("m" + i + "min", m.min);
    b.set("m" + i + "max", m.max);
  });
  b.set("ctype", boundType);
  b.set("op", $("op").value);
  if ($("svm")) b.set("svm", $("svm").value);
  if ($("msm")) b.set("msm", $("msm").value);
  b.set("rdly", $("rdly").value || 0);
  b.set("sdly", $("sdly").value || 0);
  b.set("mac", boundMac);
  b.set("ssid", $("ssid").value);
  b.set("pass", $("pass").value);
  if ($("dis").checked) b.set("dis", "1");
  b.set("ble", $("ble").value);
  if ($("clk").checked) b.set("clk", "1");
  b.set("tz", Math.round((parseFloat($("tz").value) || 0) * 60));
  return b;
}

async function postSave() {
  const r = await fetch("/save", { method: "POST", body: collectConfig() });
  if (!r.ok) throw new Error("HTTP " + r.status);
}

async function onSave() {
  const btn = $("save");
  btn.disabled = true;
  const rebootNeeded =
    boundMac !== (cfg.mac || "") || boundType !== (+cfg.ctype || 0) ||
    $("ssid").value !== cfg.ssid || $("pass").value !== cfg.pass;
  try {
    await postSave();
    if (rebootNeeded) return rebootFlow();
    cfg.mac = boundMac; cfg.ctype = boundType;
    cfg.ssid = $("ssid").value; cfg.pass = $("pass").value;
    setDirty(false);
    toast("Saved");
  } catch (e) {
    toast("Save failed", true);
  } finally {
    btn.disabled = false;
  }
}

async function rebootFlow() {
  $("rebooting").hidden = false;
  $("rebootRefresh").hidden = true;
  fetch("/reboot").catch(() => { });
  await sleep(3000);
  for (let tries = 0; ; tries++) {
    await sleep(2000);
    try {
      const r = await fetch("/status.json", { cache: "no-store" });
      if (r.ok) { location.reload(); return; }
    } catch (e) { /* AP still down, keep waiting */ }
    if (tries === 6) {  // ~15s: auto-detect may have failed, offer a manual refresh
      $("rebootText").textContent =
        "Reconnect to the ShutterBridge Wi-Fi, then tap Refresh.";
      $("rebootRefresh").hidden = false;
    }
  }
}

// ---- Live controls ----
async function setMode(m) {
  try {
    const r = await fetch("/mode?m=" + m);
    toast(r.ok ? "Mode " + m : "Mode failed (" + r.status + ")", !r.ok);
  } catch (e) { toast("Mode failed", true); }
}
async function sendPreset(params) {
  try {
    const r = await fetch("/preset?" + params);
    toast(r.ok ? "Preset loaded" : "Preset failed (" + r.status + ")", !r.ok);
  } catch (e) { toast("Preset failed", true); }
}

// ---- Range slider ----
function wireSlider(i) {
  const m = cfg.modes[i];
  const el = $("sl" + i);
  const rangeEl = el.querySelector(".sl-range");
  const thumbMin = el.querySelector('[data-h="min"]');
  const thumbMax = el.querySelector('[data-h="max"]');

  const render = () => {
    thumbMin.style.left = pct(m.min) + "%";
    thumbMax.style.left = pct(m.max) + "%";
    rangeEl.style.left = pct(m.min) + "%";
    rangeEl.style.width = pct(m.max) - pct(m.min) + "%";
    $("m" + i + "min").textContent = m.min;
    $("m" + i + "max").textContent = m.max;
    $("mode" + i).classList.toggle("off", +$("m" + i + "aux").value === 0);
  };
  $("m" + i + "aux").addEventListener("change", (e) => {
    if (e.target.value === "auto") {
      startAuto(i);
    } else {
      if (autoDetect && autoDetect.i === i) autoDetect = null;
      render();
    }
  });

  const valAt = (clientX) => {
    const r = el.getBoundingClientRect();
    return snap(CH_MIN + ((clientX - r.left) / r.width) * (CH_MAX - CH_MIN));
  };
  const dragThumb = (which) => (e) => {
    e.preventDefault();
    const move = (ev) => {
      const v = valAt(ev.clientX);
      if (which === "min") m.min = Math.min(v, m.max - GAP);
      else m.max = Math.max(v, m.min + GAP);
      render();
      setDirty(true);
    };
    const up = () => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", up);
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", up);
  };
  thumbMin.addEventListener("pointerdown", dragThumb("min"));
  thumbMax.addEventListener("pointerdown", dragThumb("max"));
  rangeEl.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    const startX = e.clientX, s0 = m.min, w = m.max - m.min;
    const move = (ev) => {
      const r = el.getBoundingClientRect();
      // Round the *delta* to STEP (not snap(), which would clamp it to the 900-2100 range).
      const raw = ((ev.clientX - startX) / r.width) * (CH_MAX - CH_MIN);
      const delta = Math.round(raw / STEP) * STEP;
      let ns = Math.max(CH_MIN, Math.min(CH_MAX - w, s0 + delta));
      m.min = ns; m.max = ns + w;
      render();
      setDirty(true);
    };
    const up = () => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", up);
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", up);
  });
  render();
}

// ---- Unsaved-changes state ----
let dirty = false;
function setDirty(v) {
  dirty = v;
  const btn = $("save");
  btn.classList.toggle("clean", !v);
  btn.textContent = v ? "Save" : "Saved";
}

function toast(msg, err) {
  const t = $("toast");
  t.textContent = msg;
  t.classList.toggle("err", !!err);
  t.classList.add("show");
  setTimeout(() => t.classList.remove("show"), 1600);
}

// ---- Status poll ----
const fmt = (s) => { s = s || 0; return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0"); };
async function tick() {
  try {
    const s = await (await fetch("/status.json")).json();
    chVals = s.ch || [];

    // Auto-detect: assign the first AUX channel (index >= 4) that moved from its baseline.
    if (autoDetect) {
      const { i, base } = autoDetect;
      for (let idx = 4; idx < chVals.length; idx++) {
        if (Math.abs((chVals[idx] || 0) - (base[idx] || 0)) > 100) {
          const auxNum = idx - 3;  // ch[4] = AUX1
          autoDetect = null;
          const sel = $("m" + i + "aux");
          sel.value = String(auxNum);
          sel.dispatchEvent(new Event("change"));
          toast("Assigned AUX " + auxNum);
          break;
        }
      }
    }

    const c = $("conn");
    const pairing = s.online && s.paired === 0;
    c.textContent = pairing ? "Accept PIN on camera" : s.online ? "Camera online" : "Camera offline";
    c.className = "pill " + (pairing ? "warn" : s.online ? "ok" : "bad");
    $("rec").style.display = s.recording ? "inline-flex" : "none";
    $("elapsed").textContent = s.online ? fmt(s.elapsed) : "--:--";
    $("mode").textContent = s.online ? (s.mode || "-").toUpperCase() : "-";
    const format = [s.res, s.fps ? s.fps + "fps" : ""].filter(Boolean).join(" ");
    $("fmt").textContent = s.online && format ? format : "-";
    $("batt").textContent = s.online ? s.batt + "%" : "--";
    $("battbar").style.width = (s.online ? s.batt : 0) + "%";
    $("sd").textContent = s.online ? (s.sd / 1024).toFixed(1) + "G" : "--";
    $("left").textContent = s.online ? fmt(s.left) : "--:--";

    if (cfg) cfg.modes.forEach((_, i) => {
      const aux = +$("m" + i + "aux").value;
      const mk = $("sl" + i).querySelector(".sl-marker");
      const v = aux > 0 ? chVals[4 + aux - 1] : 0;
      if (aux > 0 && v) { mk.style.display = "block"; mk.style.left = pct(clampCh(v)) + "%"; }
      else mk.style.display = "none";
    });
  } catch (e) { }
}

// ---- Wire up ----
$("save").addEventListener("click", onSave);
$("bind").addEventListener("click", openWizard);

// Any edit to a config field flags unsaved changes (programmatic loads don't fire these)
const wrap = document.querySelector(".wrap");
wrap.addEventListener("input", () => setDirty(true));
wrap.addEventListener("change", () => setDirty(true));

$("passtog").addEventListener("click", () => {
  const p = $("pass"), show = p.type === "password";
  p.type = show ? "text" : "password";
  const tog = $("passtog");
  tog.textContent = show ? "Hide" : "Show";
  tog.setAttribute("aria-label", show ? "Hide password" : "Show password");
});
document.querySelectorAll("#wizType .wiz-types button").forEach((b) =>
  b.addEventListener("click", () => { wizType = +b.dataset.type; wizScan(); }));
document.querySelectorAll(".wiz-cancel").forEach((b) => b.addEventListener("click", closeWizard));
$("reboot").addEventListener("click", () => { if (confirm("Reboot the bridge?")) rebootFlow(); });
$("rebootRefresh").addEventListener("click", () => location.reload());
$("modevideo").addEventListener("click", () => setMode("video"));
$("modephoto").addEventListener("click", () => setMode("photo"));
document.querySelectorAll(".preset[data-group]").forEach((b) =>
  b.addEventListener("click", () => sendPreset("group=" + b.dataset.group)));
$("presetload").addEventListener("click", () => {
  const id = $("presetid").value.trim();
  if (id) sendPreset("id=" + encodeURIComponent(id));
});
loadConfig();
setInterval(tick, 500);
tick();
