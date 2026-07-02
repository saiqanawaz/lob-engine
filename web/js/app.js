// Drives the WASM build of the C++ matching engine.
// Flow model mirrors benchmarks/workload.hpp: ~55% limit adds around a
// drifting mid, ~35% cancels of random live orders, ~10% market orders.

import createLobModule from "./lob-engine.js";

const TICK = 0.01;               // 1 engine tick = $0.01; demo mid ~ $100
const LADDER_LEVELS = 11;
const px = (t) => (t * TICK).toFixed(2);
const fq = (q) => q.toLocaleString("en-US");

const Module = await createLobModule();

// ── deterministic rng ────────────────────────────────────────────────
function mulberry32(seed) {
  let a = seed | 0;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
let rng = mulberry32(7);
function gauss() {
  let u = 0, v = 0;
  while (!u) u = rng();
  while (!v) v = rng();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
}

// ── state ────────────────────────────────────────────────────────────
let eng = null;
let live = [];
let nextId = 1;
let mid = 10000;
let ops = 0;
let tradeCount = 0;
let tradedQty = 0;
let tape = [];
let running = true;
let speed = 120;
let manualSide = "buy";

function reset() {
  if (eng) eng.delete();
  eng = new Module.Engine();
  rng = mulberry32(7);
  live = [];
  nextId = 1;
  mid = 10000;
  ops = 0;
  tradeCount = 0;
  tradedQty = 0;
  tape = [];
  for (let i = 0; i < 600; i++) step();  // prime the book
  render();
}

function recordTrades(res, aggressorSide) {
  for (const t of res.trades) {
    tape.unshift({ price: t.price, qty: t.quantity, side: aggressorSide });
    tradeCount += 1;
    tradedQty += t.quantity;
  }
  if (tape.length > 12) tape.length = 12;
}

function step() {
  const r = rng();
  if (r < 0.55 || live.length === 0) {
    const buy = rng() < 0.5;
    const price = Math.max(1, Math.round(mid + gauss() * 25) + (buy ? -5 : 5));
    const qty = 1 + Math.floor(rng() * 100);
    const res = eng.submit(nextId, buy ? "buy" : "sell", qty, price, "limit", "gtc");
    recordTrades(res, buy ? "buy" : "sell");
    if (res.status === "accepted" || res.status === "partial") live.push(nextId);
    nextId++;
  } else if (r < 0.9) {
    const k = Math.floor(rng() * live.length);
    eng.cancel(live[k]);
    live[k] = live[live.length - 1];
    live.pop();
  } else {
    const buy = rng() < 0.5;
    const qty = 1 + Math.floor(rng() * 50);
    const res = eng.submit(nextId, buy ? "buy" : "sell", qty, 0, "market", "ioc");
    recordTrades(res, buy ? "buy" : "sell");
    nextId++;
  }
  mid += gauss() * 0.4;  // slow random-walk drift so the demo wanders
  ops++;
}

// ── rendering ────────────────────────────────────────────────────────
const $ = (id) => document.getElementById(id);
const ladderEl = $("ladder");
const quoteEl = $("quoteLine");
const statEl = $("statRow");
const tapeEl = $("tape");
const tapeCountEl = $("tapeCount");
const canvas = $("depthChart");
const ctx = canvas.getContext("2d");

function rowHtml(side, [price, qty, count], maxQty) {
  const w = Math.max(2, (qty / maxQty) * 100).toFixed(1);
  return `<div class="lrow ${side}"><i class="bar" style="width:${w}%"></i>` +
         `<span class="px">${px(price)}</span><span class="qty">${fq(qty)}</span>` +
         `<span class="cnt">${count}</span></div>`;
}

function render() {
  const d = eng.depth(LADDER_LEVELS);
  const maxQty = Math.max(1, ...d.bids.map((l) => l[1]), ...d.asks.map((l) => l[1]));

  const askRows = d.asks.slice().reverse().map((l) => rowHtml("ask", l, maxQty)).join("");
  const bidRows = d.bids.map((l) => rowHtml("bid", l, maxQty)).join("");

  let spreadHtml = `<div class="spread-row"><span>spread</span><b>&ndash;</b><span></span></div>`;
  if (d.bids.length && d.asks.length) {
    const s = d.asks[0][0] - d.bids[0][0];
    const m = (d.asks[0][0] + d.bids[0][0]) / 2;
    spreadHtml = `<div class="spread-row"><span>spread <b>${px(s)}</b></span>` +
                 `<span>mid <b>${(m * TICK).toFixed(3)}</b></span></div>`;
    quoteEl.innerHTML = `<b>${px(d.bids[0][0])}</b> &times; <b>${px(d.asks[0][0])}</b>`;
  }
  ladderEl.innerHTML = askRows + spreadHtml + bidRows;

  statEl.innerHTML =
    `<div><b>${fq(ops)}</b>ops</div>` +
    `<div><b>${fq(tradeCount)}</b>trades</div>` +
    `<div><b>${fq(d.openOrders)}</b>open orders</div>`;

  tapeEl.innerHTML = tape
    .map((t) => `<li><span class="${t.side}">${t.side === "buy" ? "&#9650;" : "&#9660;"} ${px(t.price)}</span>` +
                `<span class="tq">${fq(t.qty)}</span></li>`)
    .join("");
  tapeCountEl.textContent = `${fq(tradedQty)} filled`;

  drawDepth();
}

function drawDepth() {
  const d = eng.depth(40);
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== rect.width * dpr) {
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const W = rect.width, H = rect.height;
  ctx.clearRect(0, 0, W, H);
  if (!d.bids.length || !d.asks.length) return;

  const lo = d.bids[d.bids.length - 1][0];
  const hi = d.asks[d.asks.length - 1][0];
  const X = (p) => ((p - lo) / (hi - lo)) * W;

  let cum = 0;
  const bidPts = d.bids.map(([p, q]) => [p, (cum += q)]);
  cum = 0;
  const askPts = d.asks.map(([p, q]) => [p, (cum += q)]);
  const maxCum = Math.max(bidPts[bidPts.length - 1][1], askPts[askPts.length - 1][1]);
  const Y = (c) => H - 8 - (c / maxCum) * (H - 24);

  const drawSide = (pts, color, fill) => {
    ctx.beginPath();
    ctx.moveTo(X(pts[0][0]), H - 8);
    let prevY = Y(0);
    for (const [p, c] of pts) {
      ctx.lineTo(X(p), prevY);       // step out to the level's price
      prevY = Y(c);
      ctx.lineTo(X(p), prevY);       // then up to its cumulative size
    }
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = color;
    ctx.stroke();
    ctx.lineTo(X(pts[pts.length - 1][0]), H - 8);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
  };
  drawSide(bidPts, "rgba(13,157,109,0.9)", "rgba(13,157,109,0.12)");
  drawSide(askPts, "rgba(222,69,96,0.9)", "rgba(222,69,96,0.12)");
}

// ── controls ─────────────────────────────────────────────────────────
$("playBtn").addEventListener("click", () => {
  running = !running;
  $("playBtn").textContent = running ? "Pause" : "Play";
});
$("resetBtn").addEventListener("click", reset);
$("speed").addEventListener("input", (e) => {
  speed = Number(e.target.value);
  $("speedVal").textContent = `${speed} ops/s`;
});

for (const btn of document.querySelectorAll(".seg-btn")) {
  btn.addEventListener("click", () => {
    manualSide = btn.dataset.side;
    document.querySelectorAll(".seg-btn").forEach((b) => b.classList.toggle("active", b === btn));
  });
}

$("orderForm").addEventListener("submit", (e) => {
  e.preventDefault();
  const type = $("ordType").value;
  const tif = $("ordTif").value;
  const qty = Math.floor(Number($("ordQty").value));
  const priceTicks = Math.round(Number($("ordPrice").value) / TICK);
  const fb = $("feedback");

  if (type === "limit" && !$("ordPrice").value) {
    fb.textContent = "limit order needs a price";
    fb.className = "feedback mono err";
    return;
  }
  const id = 1e9 + (nextId++);
  const res = eng.submit(id, manualSide, qty, type === "market" ? 0 : priceTicks, type, tif);
  recordTrades(res, manualSide);
  if (res.status === "rejected") {
    fb.textContent = `rejected — ${res.reason}`;
    fb.className = "feedback mono err";
  } else if (res.trades.length) {
    const avg = res.trades.reduce((s, t) => s + t.price * t.quantity, 0) / res.filled;
    fb.textContent = `${res.status} — filled ${fq(res.filled)} @ avg ${(avg * TICK).toFixed(3)}`;
    fb.className = "feedback mono ok";
  } else {
    fb.textContent = `${res.status} — resting on the book`;
    fb.className = "feedback mono warn";
  }
  if (res.status === "accepted" || res.status === "partial") live.push(id);
  render();
});

// ── main loop ────────────────────────────────────────────────────────
reset();
$("ordPrice").value = (mid * TICK).toFixed(2);
// One visible update per second: humans watch the book, the engine
// still chews through `speed` operations per tick.
setInterval(() => {
  if (running) {
    for (let i = 0; i < speed; i++) step();
    render();
  }
}, 1000);
