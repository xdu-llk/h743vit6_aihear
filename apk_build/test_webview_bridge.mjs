import { spawn } from "node:child_process";
import {
  existsSync,
  mkdtempSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, extname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(scriptDir, "..");
const htmlPath = join(scriptDir, "assets", "index.html");
const photoPath = resolve(process.argv[2] || join(repoRoot, "build", "qa-camera.jpg"));
const outputDir = resolve(process.argv[3] || join(repoRoot, "build"));
const chromeCandidates = [
  process.env.CHROME_PATH,
  "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
].filter(Boolean);
const chromePath = chromeCandidates.find(existsSync);

if (!chromePath) throw new Error("Chrome or Edge was not found");
if (!existsSync(htmlPath)) throw new Error(`App HTML not found: ${htmlPath}`);
if (!existsSync(photoPath)) throw new Error(`JPEG fixture not found: ${photoPath}`);
if (![".jpg", ".jpeg"].includes(extname(photoPath).toLowerCase())) {
  throw new Error("The bridge fixture must be a JPEG file");
}

const photo = readFileSync(photoPath);
if (photo.length < 4 || photo[0] !== 0xff || photo[1] !== 0xd8 ||
    photo.at(-2) !== 0xff || photo.at(-1) !== 0xd9) {
  throw new Error("The bridge fixture is not a complete JPEG");
}
if (photo.length > 60 * 1024) {
  throw new Error(`JPEG fixture exceeds the Android limit: ${photo.length} bytes`);
}
mkdirSync(outputDir, { recursive: true });

const profilePrefix = join(tmpdir(), "aihear-chrome-");
const profileDir = mkdtempSync(profilePrefix);
const port = 9300 + Math.floor(Math.random() * 400);
const chrome = spawn(chromePath, [
  "--headless=new",
  "--disable-gpu",
  "--no-first-run",
  "--no-default-browser-check",
  `--remote-debugging-port=${port}`,
  `--user-data-dir=${profileDir}`,
  "about:blank",
], { stdio: "ignore", windowsHide: true });

const sleep = (ms) => new Promise((resolvePromise) => setTimeout(resolvePromise, ms));

async function waitForPageTarget() {
  const deadline = Date.now() + 12_000;
  while (Date.now() < deadline) {
    try {
      const targets = await fetch(`http://127.0.0.1:${port}/json/list`).then((r) => r.json());
      const page = targets.find((target) => target.type === "page");
      if (page?.webSocketDebuggerUrl) return page;
    } catch {
      // Chrome has not opened its DevTools endpoint yet.
    }
    await sleep(100);
  }
  throw new Error("Timed out waiting for Chrome DevTools");
}

function createCdpClient(url) {
  const socket = new WebSocket(url);
  const pending = new Map();
  const listeners = new Map();
  let nextId = 1;

  socket.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (message.id) {
      const request = pending.get(message.id);
      if (!request) return;
      pending.delete(message.id);
      if (message.error) request.reject(new Error(message.error.message));
      else request.resolve(message.result);
      return;
    }
    for (const listener of listeners.get(message.method) || []) listener(message.params);
  };

  const ready = new Promise((resolveReady, rejectReady) => {
    socket.onopen = resolveReady;
    socket.onerror = () => rejectReady(new Error("Chrome DevTools WebSocket failed"));
  });

  return {
    ready,
    on(method, listener) {
      const methodListeners = listeners.get(method) || [];
      methodListeners.push(listener);
      listeners.set(method, methodListeners);
    },
    send(method, params = {}) {
      const id = nextId++;
      return new Promise((resolveRequest, rejectRequest) => {
        pending.set(id, { resolve: resolveRequest, reject: rejectRequest });
        socket.send(JSON.stringify({ id, method, params }));
      });
    },
    close() {
      socket.close();
    },
  };
}

function mockBridgeSource(dataUrl) {
  return `
    (() => {
      const deviceA = "aihear_03cb03";
      const deviceB = "aihear_876f07";
      const now = Date.now();
      window.__bridgeSnapshot = {
        eventId: "h12ab34cd",
        time: now,
        status: "ok",
        image: ${JSON.stringify(dataUrl)}
      };
      window.__bridgePublished = [];
      window.__setBridgeSnapshot = value => { window.__bridgeSnapshot = value; };
      window.Android = {
        getBoundDevices: () => JSON.stringify([deviceA, deviceB]),
        getDeviceSummaries: () => JSON.stringify([
          {deviceId: deviceA, alias: "婴儿房", lastSeen: Date.now(), todayAlerts: 1,
           lastAlert: {cls: "baby_cry", time: now - 120000}},
          {deviceId: deviceB, alias: "客厅备用", lastSeen: Date.now() - 15000,
           todayAlerts: 0}
        ]),
        getConnStatus: () => 2,
        getDeviceLastSeen: id => id === deviceA ? Date.now() : Date.now() - 15000,
        getControlState: id => JSON.stringify({deviceId: id, armed: true,
                                               music: false, uptimeMs: 360000}),
        getEnvData: id => JSON.stringify(id === deviceA ? {
          temp: 25.6, humi: 56, tempState: "normal", humiState: "normal",
          dhtValid: true, pmIndex: 102, pmState: "normal", pmReady: true,
          gasIndex: 108, gasState: "normal", mqReady: true
        } : {
          temp: 26.1, humi: 54, dhtValid: true, pmIndex: 99, pmReady: true,
          gasIndex: 104, mqReady: true
        }),
        getSnapshotData: id => id === deviceA ? JSON.stringify(window.__bridgeSnapshot) : "{}",
        getAlertsForDevice: id => JSON.stringify(id === deviceA ? [{
          id: 1, cls: "baby_cry", score: 0.93, time: now - 120000
        }] : []),
        getFeedingsForDevice: () => "[]",
        getDailyCryAndFeeding: () => "[]",
        getEnvHourly: () => "[]",
        mqttPublish: (topic, payload) => {
          window.__bridgePublished.push({topic, payload: JSON.parse(payload)});
          return true;
        },
        startService: () => true,
        stopService: () => true,
        stopAlarm: () => true,
        setMonitoredDevice: () => true,
        addBoundDevice: () => true,
        removeBoundDevice: () => true,
        setDeviceAlias: () => true,
        clearAlertsForDevice: () => true,
        addFeeding: () => true,
        addFeedingDetail: () => true,
        deleteFeeding: () => true
      };
      try {
        localStorage.clear();
        localStorage.setItem("started", "1");
        localStorage.setItem("selected_device", deviceA);
      } catch (_) {}
    })();
  `;
}

async function main() {
  const page = await waitForPageTarget();
  const cdp = createCdpClient(page.webSocketDebuggerUrl);
  const consoleErrors = [];
  cdp.on("Runtime.exceptionThrown", ({ exceptionDetails }) => {
    consoleErrors.push(exceptionDetails.exception?.description || exceptionDetails.text);
  });
  cdp.on("Log.entryAdded", ({ entry }) => {
    if (entry.level === "error") consoleErrors.push(entry.text);
  });

  await cdp.ready;
  await Promise.all([
    cdp.send("Page.enable"),
    cdp.send("Runtime.enable"),
    cdp.send("Log.enable"),
  ]);
  await cdp.send("Emulation.setDeviceMetricsOverride", {
    width: 375,
    height: 812,
    deviceScaleFactor: 1,
    mobile: true,
  });
  const dataUrl = `data:image/jpeg;base64,${photo.toString("base64")}`;
  await cdp.send("Page.addScriptToEvaluateOnNewDocument", {
    source: mockBridgeSource(dataUrl),
  });
  await cdp.send("Page.navigate", { url: pathToFileURL(htmlPath).href });

  const evaluate = async (expression) => {
    const result = await cdp.send("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.text);
    return result.result.value;
  };

  const waitUntil = async (expression, message) => {
    const deadline = Date.now() + 8_000;
    while (Date.now() < deadline) {
      if (await evaluate(expression)) return;
      await sleep(100);
    }
    throw new Error(message);
  };

  const screenshot = async (name) => {
    const result = await cdp.send("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: false,
    });
    const path = join(outputDir, name);
    writeFileSync(path, Buffer.from(result.data, "base64"));
    return path;
  };

  await waitUntil("document.readyState === 'complete'", "App page did not load");
  await waitUntil(
    "document.querySelector('#snapshotImage')?.complete && document.querySelector('#snapshotImage')?.naturalWidth > 0",
    "Bridge JPEG did not decode",
  );
  await evaluate("document.querySelector('#snapshotCard').scrollIntoView({block:'center'})");
  await sleep(150);

  const successState = await evaluate(`(() => {
    const card = document.querySelector('#snapshotCard');
    const image = document.querySelector('#snapshotImage');
    const overflow = [...document.querySelectorAll('body *')].filter(el => {
      const r = el.getBoundingClientRect();
      return r.width > 0 && (r.left < -1 || r.right > innerWidth + 1);
    }).map(el => el.id || el.className || el.tagName).slice(0, 10);
    return {
      hasImage: card.classList.contains('has-image'),
      naturalWidth: image.naturalWidth,
      naturalHeight: image.naturalHeight,
      activeDevice: document.querySelector('#deviceInfo').textContent,
      overflow
    };
  })()`);
  const successShot = await screenshot("ui-qa-photo.png");

  await evaluate(`requestSnapshot()`);
  const requestState = await evaluate(`({
    imageVisible: document.querySelector('#snapshotCard').classList.contains('has-image'),
    request: window.__bridgePublished.at(-1)
  })`);

  await evaluate(`window.__setBridgeSnapshot({eventId:'hdeadbeef', time:Date.now(),
    status:'error', reason:'unavailable'}); pollSnapshot()`);
  await sleep(100);
  const unavailableState = await evaluate(`({
    imageVisible: document.querySelector('#snapshotCard').classList.contains('has-image'),
    hasSrc: document.querySelector('#snapshotImage').hasAttribute('src'),
    text: document.querySelector('#snapshotEmptyText').textContent
  })`);
  const errorShot = await screenshot("ui-qa-unavailable.png");

  await evaluate(`window.__setBridgeSnapshot({eventId:'hfeed1234', time:Date.now(),
    status:'error', reason:'upload_failed'}); pollSnapshot()`);
  const uploadFailureState = await evaluate(
    "document.querySelector('#snapshotEmptyText').textContent",
  );

  const checks = {
    realJpegWithinLimit: photo.length <= 60 * 1024,
    jpegDecoded: successState.naturalWidth > 0 && successState.naturalHeight > 0,
    photoDisplayed: successState.hasImage,
    correctDevice: successState.activeDevice === "aihear_03cb03",
    noHorizontalOverflow: successState.overflow.length === 0,
    captureTargetsSelectedDevice:
      requestState.request?.topic === "aihear/cmd" &&
      requestState.request?.payload?.cmd === "capture" &&
      requestState.request?.payload?.device === "aihear_03cb03",
    requestClearsOldPhoto: requestState.imageVisible === false,
    unavailableClearsPhoto:
      unavailableState.imageVisible === false && unavailableState.hasSrc === false,
    unavailableMessage: unavailableState.text === "摄像头未连接",
    uploadFailureMessage: uploadFailureState === "照片上传失败",
    noConsoleErrors: consoleErrors.length === 0,
  };
  const failed = Object.entries(checks).filter(([, passed]) => !passed);
  console.log(JSON.stringify({
    fixture: { name: basename(photoPath), bytes: photo.length },
    successState,
    requestState,
    unavailableState,
    uploadFailureState,
    consoleErrors,
    screenshots: [successShot, errorShot],
    checks,
  }, null, 2));
  cdp.close();
  if (failed.length) {
    throw new Error(`WebView QA failed: ${failed.map(([name]) => name).join(", ")}`);
  }
}

try {
  await main();
} finally {
  chrome.kill();
  await sleep(200);
  const safePrefix = resolve(tmpdir(), "aihear-chrome-");
  if (resolve(profileDir).startsWith(safePrefix)) {
    rmSync(profileDir, { recursive: true, force: true });
  }
}
