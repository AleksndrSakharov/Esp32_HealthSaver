const listEl = document.getElementById("measurement-list");
const detailEl = document.getElementById("detail");
const refreshButton = document.getElementById("refresh");
const filterDevice = document.getElementById("filter-device");
const filterSensor = document.getElementById("filter-sensor");
const wsStatus = document.getElementById("ws-status");

let chart;
let selectedId = null;
let latestIndex = 0;
let currentSampleRate = 1;

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.json();
}

function formatDate(value) {
  const date = new Date(value);
  return date.toLocaleString();
}

function renderList(items) {
  listEl.innerHTML = "";
  items.forEach(item => {
    const el = document.createElement("div");
    el.className = "list-item" + (item.id === selectedId ? " active" : "");
    el.innerHTML = `
      <div><strong>${item.sensorType}</strong></div>
      <div class="meta">${item.deviceId} | ${formatDate(item.startTimeUtc)}</div>
      <div class="meta">${item.status}</div>
    `;
    el.addEventListener("click", () => selectMeasurement(item.id));
    listEl.appendChild(el);
  });
}

function renderDetail(detail) {
  detailEl.innerHTML = `
    <div><strong>Device:</strong> ${detail.deviceId}</div>
    <div><strong>Sensor:</strong> ${detail.sensorType}</div>
    <div><strong>Status:</strong> ${detail.status}</div>
    <div><strong>Samples:</strong> ${detail.sampleCount}</div>
    <div><strong>Rate:</strong> ${detail.sampleRateHz} Hz</div>
    <div><strong>Unit:</strong> ${detail.unit || "n/a"}</div>
    <div><strong>Start:</strong> ${formatDate(detail.startTimeUtc)}</div>
  `;
}

function ensureChart(points, sampleRate) {
  const ctx = document.getElementById("chart");
  const labels = points.map(p => (p.index / sampleRate).toFixed(2));
  const data = points.map(p => p.value);

  if (!chart) {
    chart = new Chart(ctx, {
      type: "line",
      data: {
        labels,
        datasets: [
          {
            label: "Signal",
            data,
            borderColor: "#4cc9f0",
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.2
          }
        ]
      },
      options: {
        responsive: true,
        scales: {
          x: {
            title: { display: true, text: "Time (s)" }
          },
          y: {
            title: { display: true, text: "Value" }
          }
        }
      }
    });
  } else {
    chart.data.labels = labels;
    chart.data.datasets[0].data = data;
    chart.update();
  }

  latestIndex = points.length > 0 ? points[points.length - 1].index : 0;
  currentSampleRate = sampleRate || 1;
}

function appendLivePoints(points, sampleRate) {
  if (!chart || points.length === 0) {
    return;
  }

  points.forEach(point => {
    latestIndex = Math.max(latestIndex, point.index);
    chart.data.labels.push((point.index / sampleRate).toFixed(2));
    chart.data.datasets[0].data.push(point.value);
  });

  chart.update("none");
}

async function loadMeasurements() {
  const query = new URLSearchParams();
  if (filterDevice.value.trim()) {
    query.set("deviceId", filterDevice.value.trim());
  }
  if (filterSensor.value.trim()) {
    query.set("sensorType", filterSensor.value.trim());
  }

  const items = await fetchJson(`/api/measurements?${query.toString()}`);
  renderList(items);
}

async function selectMeasurement(id) {
  selectedId = id;
  await loadMeasurements();

  const detail = await fetchJson(`/api/measurements/${id}`);
  renderDetail(detail);

  const series = await fetchJson(`/api/measurements/${id}/series?maxPoints=1200`);
  ensureChart(series.points, series.sampleRateHz);
}

refreshButton.addEventListener("click", () => loadMeasurements());

const scheme = location.protocol === "https:" ? "wss" : "ws";
const ws = new WebSocket(`${scheme}://${location.host}/ws/live`);

ws.addEventListener("open", () => {
  wsStatus.textContent = "WS: connected";
});

ws.addEventListener("close", () => {
  wsStatus.textContent = "WS: disconnected";
});

ws.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.measurementId !== selectedId) {
    return;
  }

  appendLivePoints(message.points, currentSampleRate || 1);
});

loadMeasurements();
