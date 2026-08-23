# Contributing to ShutterBridge

Thanks for your interest - contributions are welcome, whether that's code, docs,
or simply reporting how ShutterBridge behaves with your camera.

## Ways to help

- **Report a camera.** Tried ShutterBridge with a camera? Tell us how it went via
  the [camera compatibility report](https://github.com/YLabs-FPV/ShutterBridge/issues/new?template=camera_report.yml)
  issue. This is genuinely useful even when everything works - it's how the
  compatibility table grows.
- **Report a bug** with the [bug report](https://github.com/YLabs-FPV/ShutterBridge/issues/new?template=bug_report.yml)
  template.
- **Suggest a feature** with the [feature request](https://github.com/YLabs-FPV/ShutterBridge/issues/new?template=feature_request.yml)
  template.
- **Improve the docs or code** with a pull request (see below).

## The golden rule: test on real hardware

ShutterBridge drives real cameras on real aircraft, and some limitations only show
up on the bench. **Any change to firmware behaviour must be tested on real
hardware before it's merged.** In your pull request, say which board and camera you
tested on and what you verified.

**AI-assisted contributions are welcome** - use whatever tools help you. The same
rule applies without exception: if it changes firmware behaviour, you must have
flashed it to a real board and confirmed it works with a real camera. Untested,
"looks correct" changes will be asked to prove themselves on hardware first.

Documentation-only or website-only changes don't need hardware testing - just make
sure the site builds (`pnpm build` in `site/`).

## Development setup

### Firmware (`firmware/`)

A [PlatformIO](https://platformio.org) project for the Waveshare ESP32-S3-Zero.

```bash
cd firmware
pio run -t upload      # build + flash the firmware over USB
pio run -t uploadfs    # build + upload the Web UI (LittleFS) image
```

`pack-localhost-dev-firmware.sh` copies the build output into the site's local web
flasher so you can test flashing end to end.

### Site (`site/`)

An [Astro](https://astro.build) project (site, docs, and the web flasher).

```bash
cd site
pnpm install
pnpm dev               # local dev server
pnpm build             # production build (also generates the sitemap)
```

## Pull request checklist

- [ ] Firmware behaviour changes are **tested on real hardware** (state the board +
      camera + what you verified).
- [ ] Code follows the style of the surrounding files (`.clang-format` for firmware).
- [ ] Docs updated if behaviour changed.
- [ ] Commits are focused and the PR description explains the "why".

## Reporting security issues

Please don't open a public issue for anything security-sensitive - contact the
maintainer privately first.

## License

By contributing, you agree that your contributions are licensed under the project's
[GPL-3.0-or-later](LICENSE) license.
