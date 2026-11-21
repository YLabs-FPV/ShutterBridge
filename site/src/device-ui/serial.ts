// Runs the bridge's own Web UI over USB serial. API fetch() calls become
// `sb <id> <path>?<query>` lines, answered by `sb:<id> <code> <body>` lines
(() => {
  type Reply = { code: number; body: string };
  type Pending = { resolve: (r: Reply) => void; reject: (e: unknown) => void };

  const API = new Set([
    "/config.json",
    "/status.json",
    "/scan",
    "/preset",
    "/mode",
    "/shutter",
    "/bridge",
    "/save",
    "/reboot",
  ]);
  const REPLY = /sb:(\d+) (\d{3}) (.*)$/;
  const MAX_LINE = 1500; // the firmware drops longer lines
  const params = new URLSearchParams(location.search);
  const flashUrl = params.get("flash") || "/flash/";

  const realFetch = window.fetch.bind(window);
  const enc = new TextEncoder();
  const pending = new Map<number, Pending>();

  let port: SerialPort | null = null;
  let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  let reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  let nextId = 1;
  let writeChain: Promise<void> = Promise.resolve();
  let everConnected = false;
  let connecting = false;
  let markReady!: () => void;
  let ready = new Promise<void>((r) => (markReady = r));

  // Read by app.js when the bridge does not come back after a reboot
  (window as Window & { sbReconnectHint?: string }).sbReconnectHint =
    "Plug the bridge back in if it doesn't come back, then tap Refresh.";

  // ---- Connect overlay, styled with the UI's own overlay classes ----
  const ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML =
    '<div class="overlay-box">' +
    '<div class="overlay-title"></div>' +
    '<div class="overlay-text"></div>' +
    '<button class="wide primary" type="button"></button>' +
    "</div>";
  document.body.appendChild(ov);
  const ovTitle = ov.querySelector<HTMLElement>(".overlay-title")!;
  const ovText = ov.querySelector<HTMLElement>(".overlay-text")!;
  const ovBtn = ov.querySelector<HTMLButtonElement>("button")!;

  function show(title: string, html: string, button: string | null) {
    ovTitle.textContent = title;
    ovText.innerHTML = html;
    ovBtn.hidden = !button;
    if (button) ovBtn.textContent = button;
    ov.hidden = false;
  }

  const rebootOverlayUp = () => {
    const el = document.getElementById("rebooting");
    return !!el && !el.hidden;
  };

  // The Betaflight bridge serves TCP on the Wi-Fi AP, so it has no use over USB
  const bfCard = document.getElementById("bfbridge")?.closest<HTMLElement>(".card");
  if (bfCard) bfCard.style.display = "none";
  const rebootText = document.getElementById("rebootText");
  if (rebootText)
    rebootText.textContent =
      "The bridge restarts and this page reconnects on its own.";

  // ---- Transport ----
  function request(
    path: string,
    query: string,
    timeoutMs: number,
    signal?: AbortSignal | null,
  ): Promise<Reply> {
    const w = writer;
    if (!w) return Promise.reject(new Error("not connected"));
    if (signal?.aborted) return Promise.reject(signal.reason);
    const line = `sb ${nextId} ${path}${query ? "?" + query : ""}`;
    if (line.length > MAX_LINE)
      return Promise.reject(new Error("request too long"));
    const id = nextId++;

    return new Promise<Reply>((resolve, reject) => {
      const finish = <T>(fn: (v: T) => void, v: T) => {
        clearTimeout(timer);
        pending.delete(id);
        signal?.removeEventListener("abort", onAbort);
        fn(v);
      };
      const onAbort = () => finish(reject, signal?.reason);
      const timer = setTimeout(
        () => finish(reject, new Error("timeout")),
        timeoutMs,
      );
      signal?.addEventListener("abort", onAbort);
      pending.set(id, {
        resolve: (r) => finish(resolve, r),
        reject: (e) => finish(reject, e),
      });
      writeChain = writeChain
        .then(() => w.write(enc.encode(line + "\n")))
        .catch((e) => finish(reject, e));
    });
  }

  function onLine(line: string) {
    const m = REPLY.exec(line);
    if (!m) return; // log output
    pending.get(+m[1])?.resolve({ code: +m[2], body: m[3] });
  }

  async function readLoop(p: SerialPort) {
    const dec = new TextDecoder();
    let buf = "";
    try {
      reader = p.readable!.getReader();
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += dec.decode(value, { stream: true });
        let nl;
        while ((nl = buf.indexOf("\n")) >= 0) {
          onLine(buf.slice(0, nl).replace(/\r$/, ""));
          buf = buf.slice(nl + 1);
        }
        if (buf.length > 65536) buf = "";
      }
    } catch {
      /* unplugged or rebooted */
    }
    lost(p);
  }

  async function release(p: SerialPort) {
    try {
      reader?.releaseLock();
    } catch {}
    try {
      writer?.releaseLock();
    } catch {}
    reader = writer = null;
    try {
      await p.close();
    } catch {}
  }

  async function lost(p: SerialPort) {
    if (port !== p) return;
    port = null;
    for (const entry of pending.values())
      entry.reject(new Error("disconnected"));
    ready = new Promise<void>((r) => (markReady = r));
    await release(p);
    if (!rebootOverlayUp())
      show(
        "Disconnected",
        "Plug the bridge back in. This page reconnects on its own.",
        null,
      );
  }

  // Native USB ignores the baud rate; a USB-UART bridge needs the firmware's 115200
  async function connect(p: SerialPort, quiet: boolean): Promise<boolean> {
    if (connecting || port) return false;
    connecting = true;
    try {
      try {
        await p.open({ baudRate: 115200 });
      } catch {
        if (!quiet)
          show(
            "Port busy",
            "Close any other tab, serial monitor or app using this port, then try again.",
            "Connect",
          );
        return false;
      }
      // DTR on, RTS off: never the RTS-only state that holds the chip in reset
      try {
        await p.setSignals({ dataTerminalReady: true, requestToSend: false });
      } catch {}
      port = p;
      writer = p.writable!.getWriter();
      readLoop(p);

      // Right after a reboot the firmware may still be starting, so allow a few tries
      let ok = false;
      for (let i = 0; i < 3 && !ok && port === p; i++) {
        try {
          await request("/status.json", "", 1500);
          ok = true;
        } catch {}
      }
      if (!ok) {
        port = null;
        await release(p);
        if (!quiet)
          show(
            "No answer",
            "This board doesn't support setup over USB yet. Install the latest " +
              `ShutterBridge firmware with the <a href="${flashUrl}" target="_top">web flasher</a>, then try again.`,
            "Try again",
          );
        return false;
      }

      if (everConnected) {
        location.reload(); // fresh config after a reboot or replug
        return true;
      }
      everConnected = true;
      ov.hidden = true;
      markReady();
      return true;
    } finally {
      connecting = false;
    }
  }

  async function tryKnownPorts(quiet: boolean) {
    for (const p of await navigator.serial.getPorts())
      if (await connect(p, quiet)) return true;
    return false;
  }

  function waitReady(signal?: AbortSignal | null): Promise<void> {
    if (!signal) return ready;
    if (signal.aborted) return Promise.reject(signal.reason);
    return new Promise<void>((resolve, reject) => {
      signal.addEventListener("abort", () => reject(signal.reason), {
        once: true,
      });
      ready.then(resolve);
    });
  }

  window.fetch = async (input: RequestInfo | URL, init: RequestInit = {}) => {
    const url = new URL(
      input instanceof Request ? input.url : String(input),
      location.href,
    );
    if (url.origin !== location.origin || !API.has(url.pathname))
      return realFetch(input, init);
    await waitReady(init.signal);
    const query = init.body != null ? String(init.body) : url.search.slice(1);
    const r = await request(url.pathname, query, 5000, init.signal);
    const json = r.body.startsWith("{") || r.body.startsWith("[");
    return new Response(r.body, {
      status: r.code,
      headers: { "Content-Type": json ? "application/json" : "text/plain" },
    });
  };

  // ---- Start ----
  if (!("serial" in navigator)) {
    show(
      "USB setup isn't available here",
      "Open this page in desktop Chrome or Edge. Other browsers can still use the bridge's Wi-Fi setup page.",
      null,
    );
    return;
  }

  ovBtn.addEventListener("click", async () => {
    let p: SerialPort;
    try {
      p = await navigator.serial.requestPort();
    } catch {
      return; // picker closed
    }
    show("Connecting", "", null);
    await connect(p, false);
  });

  navigator.serial.addEventListener("connect", (e) => {
    if (!port) connect(e.target as SerialPort, true);
  });

  show(
    "Connect over USB",
    "Plug the bridge into this computer with a USB cable, then pick its serial port.",
    "Connect",
  );
  tryKnownPorts(true);
})();
