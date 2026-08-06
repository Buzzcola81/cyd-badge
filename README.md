# CYD Badge

CYD Badge is a starter CYD firmware project for ESP32 Cheap Yellow Display boards.

## What Is Included

- PlatformIO project scaffold (`cyd` + `native` environments).
- GitHub Actions CI workflow for firmware build and native tests.
- GitHub Pages deployment workflow for a web flasher site.
- Minimal docs landing page showing the project name.

## Project Structure

- `.github/workflows/ci.yml`
- `.github/workflows/deploy-pages.yml`
- `docs/index.html`
- `docs/manifest.json`
- `include/app_logic.h`
- `src/main.cpp`
- `test/test_state_logic/test_main.cpp`
- `platformio.ini`

## Build

```bash
pio run -e cyd
```

## Test

```bash
pio test -e native
```

## Flash to Device

```bash
pio run -e cyd -t upload
```

## GitHub Pages

The deployment workflow publishes `docs/` plus built firmware images to GitHub Pages.

One-time setup in GitHub repo settings:

1. Open **Settings -> Pages**.
2. Set **Source** to **GitHub Actions**.