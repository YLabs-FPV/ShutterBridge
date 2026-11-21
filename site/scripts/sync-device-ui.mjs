import { transformSync } from "esbuild";
import {
  cpSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";

const site = new URL("../", import.meta.url);
const src = new URL("../firmware/data/", site);
const out = new URL("public/device-ui/", site);

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
cpSync(src, out, { recursive: true });

const serial = transformSync(
  readFileSync(new URL("src/device-ui/serial.ts", site), "utf8"),
  { loader: "ts", target: "es2020", sourcefile: "serial.ts" },
);
writeFileSync(new URL("serial.js", out), serial.code);

const index = new URL("index.html", out);
const html = readFileSync(index, "utf8");
const tag = '<script src="app.js"></script>';
if (!html.includes(tag))
  throw new Error(
    `sync-device-ui: ${tag} not found in firmware/data/index.html`,
  );
writeFileSync(
  index,
  html.replace(tag, `<script src="serial.js"></script>\n    ${tag}`),
);
