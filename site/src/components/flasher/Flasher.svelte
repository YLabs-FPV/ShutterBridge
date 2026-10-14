<script lang="ts">
  import { onMount } from "svelte";
  import type {
    ESPLoader,
    FlashOptions,
    Transport as TransportT,
  } from "esptool-js";
  import { GITHUB_URL } from "../../consts";
  import Usb from "@lucide/svelte/icons/usb";
  import Zap from "@lucide/svelte/icons/zap";
  import AlertTriangle from "@lucide/svelte/icons/triangle-alert";
  import CheckCircle from "@lucide/svelte/icons/circle-check";
  import Loader from "@lucide/svelte/icons/loader-circle";
  import ChevronDown from "@lucide/svelte/icons/chevron-down";
  import RotateCcw from "@lucide/svelte/icons/rotate-ccw";

  export let manifestUrl = "/firmware/manifest.dev.json";

  // id must match the PlatformIO env name
  // chipFamily must match esptool-js's ESPLoader chip
  type Board = { id: string; label: string; chipFamily: string };
  const BOARDS: Board[] = [
    {
      id: "esp32s3-zero",
      label: "Waveshare ESP32-S3-Zero",
      chipFamily: "ESP32-S3",
    },
    {
      id: "esp32c3-supermini",
      label: "ESP32-C3 Super Mini",
      chipFamily: "ESP32-C3",
    },
  ];

  const PART_NAMES = [
    "bootloader",
    "partitions",
    "boot_app0",
    "firmware",
    "littlefs",
  ] as const;
  const ASSET_RE = new RegExp(`^(.+)-(${PART_NAMES.join("|")})\\.bin$`);

  type Part = { path: string; offset: number };
  type FlashSpec = {
    flashMode?: FlashOptions["flashMode"];
    flashFreq?: FlashOptions["flashFreq"];
    flashSize?: FlashOptions["flashSize"];
  };
  type FirmwareVersion = FlashSpec & {
    version: string;
    board: string;
    parts: Part[];
  };
  // A manifest version entry may omit `board` and inherit the manifest-level default instead
  type ManifestVersion = Omit<FirmwareVersion, "board"> & { board?: string };
  type Manifest = FlashSpec & {
    name?: string;
    board?: string;
    version?: string;
    parts?: Part[];
    versions?: ManifestVersion[];
  };

  type GhAsset = { name: string; browser_download_url: string };
  type GhRelease = {
    tag_name?: string;
    name?: string;
    draft: boolean;
    assets: GhAsset[];
  };

  type State =
    | "checking"
    | "unsupported"
    | "idle"
    | "connecting"
    | "ready"
    | "working"
    | "done"
    | "error";

  const OFFSETS: Record<string, number> = {
    "bootloader.bin": 0x0,
    "partitions.bin": 0x8000,
    "boot_app0.bin": 0xe000,
    "firmware.bin": 0x10000,
    "littlefs.bin": 0x310000,
  };

  const repo = (() => {
    try {
      return new URL(GITHUB_URL).pathname.replace(/^\/+|\/+$/g, "");
    } catch {
      return "";
    }
  })();

  let state: State = "checking";
  let manifest: Manifest | null = null;
  let versions: FirmwareVersion[] = [];
  let detectedBoard: Board | null = null;
  let matchedVersions: FirmwareVersion[] = [];
  let selected = 0;
  let statusText = "";
  let percent = 0;
  let eraseFirst = false;
  let logLines: string[] = [];
  let logOpen = false;
  let transport: TransportT | null = null;
  let esploader: ESPLoader | null = null;
  let logEl: HTMLPreElement | null = null;

  function log(line: string) {
    logLines = [...logLines, line];
    queueMicrotask(() => logEl?.scrollTo(0, logEl.scrollHeight));
  }

  const terminal = {
    clean: () => (logLines = []),
    writeLine: (data: string) => log(data),
    write: (data: string) => {
      if (logLines.length === 0) logLines = [""];
      logLines[logLines.length - 1] += data;
      logLines = logLines;
    },
  };

  function isDevHost(): boolean {
    if (import.meta.env.DEV) return true;
    const h = location.hostname;
    return (
      h === "localhost" ||
      h === "127.0.0.1" ||
      h === "[::1]" ||
      h.endsWith(".local")
    );
  }

  onMount(async () => {
    if (!("serial" in navigator)) {
      state = "unsupported";
      return;
    }

    const param = new URLSearchParams(location.search).get("manifest");
    const override = param && param.startsWith("/") ? param : null;

    if (override || isDevHost()) {
      // Local manifest: only in dev, or when an explicit override is given.
      // Never fall back to the dev image in production.
      try {
        const res = await fetch(override ?? manifestUrl, { cache: "no-store" });
        if (res.ok) {
          manifest = await res.json();
          versions = normalize(manifest!);
        }
      } catch {}
    } else if (repo) {
      // Production: published GitHub releases only.
      try {
        versions = await loadFromGitHub(repo);
      } catch {}
    }

    state = "idle";
  });

  // Releases tagged before multi-board support shipped used flat names (e.g. "bootloader.bin")
  // and only ever built esp32s3-zero; attribute those to it so old releases keep working.
  const LEGACY_RE = new RegExp(`^(${PART_NAMES.join("|")})\\.bin$`);
  const LEGACY_BOARD = "esp32s3-zero";

  async function loadFromGitHub(r: string): Promise<FirmwareVersion[]> {
    const res = await fetch(
      `https://api.github.com/repos/${r}/releases?per_page=20`,
      {
        headers: { Accept: "application/vnd.github+json" },
        signal: AbortSignal.timeout(6000),
      },
    );
    if (!res.ok) throw new Error(`GitHub API ${res.status}`);
    const releases = (await res.json()) as GhRelease[];
    const out: FirmwareVersion[] = [];
    for (const rel of releases) {
      if (rel.draft) continue;
      const version = rel.tag_name || rel.name || "unknown";
      const byBoard = new Map<string, Part[]>();
      for (const a of rel.assets ?? []) {
        let board: string, part: string;
        const m = a.name.match(ASSET_RE);
        if (m) {
          [, board, part] = m;
        } else {
          const lm = a.name.match(LEGACY_RE);
          if (!lm) continue;
          board = LEGACY_BOARD;
          part = lm[1];
        }
        byBoard.set(board, [
          ...(byBoard.get(board) ?? []),
          {
            path: `/api/gh-asset?url=${encodeURIComponent(a.browser_download_url)}`,
            offset: OFFSETS[`${part}.bin`],
          },
        ]);
      }
      for (const [board, parts] of byBoard) {
        if (parts.length !== PART_NAMES.length) continue; // incomplete set - skip
        out.push({
          version,
          board,
          parts: parts.sort((a, b) => a.offset - b.offset),
        });
      }
    }
    return out;
  }

  function normalize(m: Manifest): FirmwareVersion[] {
    const spec: FlashSpec = {
      flashMode: m.flashMode,
      flashFreq: m.flashFreq,
      flashSize: m.flashSize,
    };
    const board = m.board ?? BOARDS[0].id;
    if (m.versions?.length) {
      return m.versions.map((v) => ({ ...spec, board, ...v }));
    }
    if (m.parts?.length) {
      return [
        { ...spec, board, version: m.version ?? "latest", parts: m.parts },
      ];
    }
    return [];
  }

  async function loadParts(parts: Part[]) {
    const manifestAbs = new URL(manifestUrl, location.href);
    const files = [];
    for (const part of parts) {
      const url = new URL(part.path, manifestAbs);
      const res = await fetch(url, { cache: "no-store" });
      if (!res.ok) {
        throw new Error(
          `Couldn't fetch ${part.path} (${res.status}). No firmware image is published yet.`,
        );
      }
      const data = new Uint8Array(await res.arrayBuffer());
      files.push({ data, address: part.offset });
    }
    return files;
  }

  async function connect() {
    state = "connecting";
    statusText = "Select your board's serial port...";
    logLines = [];

    try {
      const { ESPLoader, Transport } = await import("esptool-js");
      const port = await navigator.serial.requestPort();
      transport = new Transport(port, true);
      esploader = new ESPLoader({ transport, baudrate: 921600, terminal });

      statusText = "Connecting...";
      const description = await esploader.main();
      log(`Detected ${description}`);

      const chipFamily = esploader.chip.CHIP_NAME;
      const board = BOARDS.find((b) => b.chipFamily === chipFamily);
      if (!board)
        throw new Error(
          `Unrecognized chip (${chipFamily}) - no matching board.`,
        );

      matchedVersions = versions.filter((v) => v.board === board.id);
      if (!matchedVersions.length) {
        throw new Error(
          `No published firmware found for ${board.label} (${chipFamily}).`,
        );
      }

      detectedBoard = board;
      selected = 0;
      state = "ready";
    } catch (err) {
      state = "error";
      statusText = err instanceof Error ? err.message : String(err);
      try {
        await transport?.disconnect();
      } catch {}
      transport = null;
      esploader = null;
    }
  }

  async function flash() {
    const fw = matchedVersions[selected];
    if (!fw?.parts?.length || !transport || !esploader) {
      state = "error";
      statusText = "No firmware image available.";
      return;
    }

    state = "working";
    percent = 0;

    try {
      statusText = "Loading firmware...";
      const fileArray = await loadParts(fw.parts);
      const totalBytes = fileArray.reduce((n, f) => n + f.data.length, 0);
      const before = fileArray.map((_, i) =>
        fileArray.slice(0, i).reduce((n, f) => n + f.data.length, 0),
      );

      statusText = eraseFirst
        ? "Erasing, then writing..."
        : "Writing firmware...";
      await esploader.writeFlash({
        fileArray,
        flashMode: fw.flashMode ?? "keep",
        flashFreq: fw.flashFreq ?? "keep",
        flashSize: fw.flashSize ?? "keep",
        eraseAll: eraseFirst,
        compress: true,
        reportProgress: (i: number, written: number, total: number) => {
          percent = Math.round(
            ((before[i] + (written / total) * fileArray[i].data.length) /
              totalBytes) *
              100,
          );
        },
      });

      percent = 100;
      try {
        await transport.setDTR(false);
        await transport.setRTS(true);
        await new Promise((r) => setTimeout(r, 100));
        await transport.setRTS(false);
      } catch {}

      state = "done";
      statusText = "Done";
    } catch (err) {
      state = "error";
      statusText = err instanceof Error ? err.message : String(err);
    } finally {
      try {
        await transport?.disconnect();
      } catch {}
      transport = null;
      esploader = null;
    }
  }

  function reset() {
    state = "idle";
    percent = 0;
    statusText = "";
    detectedBoard = null;
    matchedVersions = [];
  }

  async function cancelReady() {
    try {
      await transport?.disconnect();
    } catch {}
    transport = null;
    esploader = null;
    reset();
  }
</script>

<div
  class="rounded-2xl border border-[rgb(var(--color-border))] bg-[rgb(var(--color-card-bg))] p-6 md:p-8 shadow-sm"
>
  {#if state === "unsupported"}
    <div
      class="flex items-start gap-3 rounded-xl bg-[rgba(var(--color-accent),0.1)] border border-[rgba(var(--color-accent),0.25)] p-4"
    >
      <AlertTriangle
        class="w-5 h-5 mt-0.5 shrink-0 text-[rgb(var(--color-accent))]"
      />
      <div class="text-sm">
        <p class="font-semibold text-[rgb(var(--color-accent))]">
          Web Serial isn't available here
        </p>
        <p class="mt-1 text-[rgb(var(--color-text-muted))]">
          Open this page in desktop Chrome, Edge or another Chromium browser.
          Firefox, Safari and mobile browsers can't flash.
        </p>
      </div>
    </div>
  {:else}
    <div class="flex flex-col items-center text-center">
      <div
        class="flex items-center justify-center w-14 h-14 rounded-2xl bg-[rgba(var(--color-primary),0.1)] text-[rgb(var(--color-primary))]"
      >
        {#if state === "done"}
          <CheckCircle class="w-7 h-7 text-emerald-500" />
        {:else if state === "working" || state === "checking" || state === "connecting"}
          <Loader class="w-7 h-7 animate-spin" />
        {:else}
          <Usb class="w-7 h-7" />
        {/if}
      </div>

      <h2 class="mt-4 text-2xl font-bold">
        {#if state === "done"}
          Flashed successfully
        {:else if state === "error"}
          Flashing failed
        {:else}
          Flash ShutterBridge
        {/if}
      </h2>

      <p class="mt-2 text-[rgb(var(--color-text-muted))] max-w-md min-h-6">
        {#if state === "idle"}
          {#if versions.length}
            Installs the firmware straight from your browser over USB. No app,
            no drivers. Connect your board to detect it automatically.
          {:else}
            No firmware has been published yet. Check back once the first
            release is out.
          {/if}
        {:else if state === "ready" && detectedBoard}
          Detected {detectedBoard.label}. Choose a version and flash.
        {:else if state === "done"}
          Unplug and re-plug the board, then connect to the <code
            >ShutterBridge</code
          > Wi-Fi to finish setup.
        {:else if state === "error"}
          {statusText}
        {:else if state === "checking"}
          Checking for available firmware...
        {:else}
          {statusText}
        {/if}
      </p>

      {#if state === "working"}
        <div class="mt-6 w-full max-w-sm">
          <div
            class="h-2.5 rounded-full bg-[rgb(var(--color-secondary))] overflow-hidden"
          >
            <div
              class="h-full rounded-full bg-[rgb(var(--color-primary))] transition-all duration-200"
              style={`width: ${percent}%`}
            ></div>
          </div>
          <div class="mt-1.5 text-sm text-[rgb(var(--color-text-muted))]">
            {percent}%
          </div>
        </div>
      {/if}

      <div class="flex flex-col items-center gap-3">
        {#if state === "idle" && versions.length}
          <button
            on:click={connect}
            class="inline-flex items-center gap-2 px-6 py-3 my-2 rounded-lg font-semibold text-white bg-[rgb(var(--color-primary))] hover:bg-[rgb(var(--color-primary-hover))] transition-colors"
          >
            <Usb class="w-5 h-5" />
            Connect
          </button>
        {:else if state === "ready" && detectedBoard}
          {#if matchedVersions.length > 1}
            <label
              class="flex items-center gap-2 my-2 text-sm text-[rgb(var(--color-text-muted))]"
            >
              Version
              <select
                bind:value={selected}
                class="rounded-lg border border-[rgb(var(--color-border))] bg-[rgb(var(--color-card-bg))] text-[rgb(var(--color-text))] px-3 py-1.5 text-sm focus:outline-none focus:ring-2 focus:ring-[rgb(var(--color-primary))]"
              >
                {#each matchedVersions as v, i}
                  <option value={i}>{v.version}</option>
                {/each}
              </select>
            </label>
          {/if}

          <button
            on:click={flash}
            class="inline-flex items-center gap-2 px-6 py-3 my-2 rounded-lg font-semibold text-white bg-[rgb(var(--color-primary))] hover:bg-[rgb(var(--color-primary-hover))] transition-colors"
          >
            <Zap class="w-5 h-5" />
            Install
          </button>

          <label
            class="flex items-center gap-2 text-sm text-[rgb(var(--color-text-muted))] select-none"
            title="Erase saved settings and camera pairing. Leave off to keep your config across updates."
          >
            <input
              type="checkbox"
              bind:checked={eraseFirst}
              class="h-4 w-4 accent-[rgb(var(--color-primary))]"
            />
            Full wipe (erase saved settings and pairing)
          </label>

          <button
            on:click={cancelReady}
            class="text-sm text-[rgb(var(--color-text-muted))] hover:text-[rgb(var(--color-text))] underline"
          >
            Cancel
          </button>
        {:else if state === "error" || state === "done"}
          <button
            on:click={reset}
            class="inline-flex items-center gap-2 px-6 py-4 my-2 rounded-lg font-semibold border border-[rgb(var(--color-border))] hover:bg-[rgb(var(--color-secondary))] transition-colors text-[rgb(var(--color-text))]"
          >
            <RotateCcw class="w-5 h-5" />
            {state === "error" ? "Try again" : "Flash another"}
          </button>
        {/if}
      </div>
    </div>

    {#if logLines.length > 0}
      <div class="mt-8 border-t border-[rgb(var(--color-border))] pt-4">
        <button
          on:click={() => (logOpen = !logOpen)}
          class="flex items-center gap-1.5 text-sm font-medium text-[rgb(var(--color-text-muted))] hover:text-[rgb(var(--color-text))] transition-colors"
        >
          <ChevronDown
            class={`w-4 h-4 transition-transform ${logOpen ? "rotate-180" : ""}`}
          />
          Log
        </button>
        {#if logOpen}
          <pre
            bind:this={logEl}
            class="mt-3 max-h-56 overflow-auto rounded-lg bg-[rgb(var(--color-secondary))] p-3 text-xs leading-relaxed whitespace-pre-wrap break-all">{logLines.join(
              "\n",
            )}</pre>
        {/if}
      </div>
    {/if}
  {/if}
</div>
