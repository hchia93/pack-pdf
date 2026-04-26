# PackPDF

**English** | [中文](README-cn.md)

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Platform: Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)
![Version: v0.1](https://img.shields.io/badge/Version-v0.1-orange.svg)

<div align="center">

![GUI](doc/screenshots/gui.png)

</div>

A Windows desktop tool that assembles PDFs as a timeline. Drop files onto the window, reorder rows, set per-row page ranges or image options, click Pack. Fully offline, with a planned CLI so AI agents can drive the same pipeline.

## What it solves

Existing tools all hit at least one of: upload required, paywalled, no timeline, or not scriptable.

<div align="center">

| Tool          | Offline    | Free      | Timeline  | Notes                              |
|---------------|------------|-----------|-----------|------------------------------------|
| Smallpdf      | ✗          | Limited   | ✗         | Uploads files, 2 tasks/day, 20-file cap |
| iLovePDF      | ✗          | Limited   | ✗         | Uploads files, 25-file cap         |
| PDFsam Basic  | ✓          | ✓         | ✗         | Per-file dialogs                   |
| PDF24 Creator | ✓          | ✓         | ✗         | Per-file dialogs                   |
| Stirling-PDF  | Self-hosted| ✓         | Partial   | Server architecture, not a desktop app |
| Adobe Acrobat | ✓          | ✗         | ✓         | Merge gated by subscription        |
| **PackPDF**   | **✓**      | **✓**     | **✓**     | Windows, no-install exe, agent-callable |

</div>

Typical case: `A.pdf{1-7}` → screenshot → `A.pdf{12-14}` → two landscape photos stacked on one A4. One screen, done.

## Who it's for

- **People who merge PDFs often.** If you assemble reports, scanned forms, or case materials every week, the per-file dialog model gets old fast. PackPDF treats the whole assembly as one editable list
- **People with sensitive files.** Anything you don't want to upload — contracts, medical records, financial statements, internal documents — has to stay local. Browser tools and SaaS uploaders are off the table by policy or by instinct. PackPDF never touches the network
- **People hitting paywalls.** Merging a few PDFs and pasting in a few screenshots is not a premium feature in any reasonable world, but several mainstream tools rate-limit or upsell it. PackPDF is just an exe
- **People who'd rather hand this to an AI agent.** You can ask an LLM to merge PDFs for you, but every round of "no, swap pages 5 and 6, drop page 12" burns context and time, and conversation can't precisely control ordering. The planned PackPDF CLI (see roadmap) is the agent-callable version of this pipeline: deterministic syntax, one command, no round-trips

## GUI usage

1. **Drop files onto the window.** PDFs, JPEGs, and PNGs are accepted. Each file becomes one row on the timeline, tagged with a colored badge (`PDF` / `JPG` / `PNG`)
2. **Reorder.** Use the up/down buttons on each row, or the X button to remove it. The output order is exactly the row order
3. **Set per-row options.**
   - PDF rows: choose `All`, `Range [a-b]`, or `Exclude [a-b]` for the page selection (1-indexed, inclusive)
   - Image rows: pick `Portrait` or `Landscape` inline; click the gear icon to open a popup with `Reverse 180°` (for head-down photos), `Padding` (0.5-inch white margin), `Scale` (`Fit page` default — small images upscale to fill A4; `Original size` keeps native pixel size with A4 surrounding it as natural margin), and `Auto Merge` (landscape only — stack two landscape images onto one A4 portrait sheet). The gear shows a small accent dot when any option is non-default
4. **Set output folder and filename.** Read and written in place at `<exe>/userdata/config.ini` (a baseline copy is checked into the repo and ships in every release zip)
5. **Click Pack.** When it succeeds, a notice appears with an `Open` button that opens the resulting file
6. **Theme menu** at the top: Photoshop Dark, Walnut, Monokai, ImGui Dark. Also persisted

The compose pass produces A4-portrait pages for images, caps oversized PDF pages to A4, and writes one output file via PDFium.

## CLI usage (planned, v0.5)

This section describes the **target syntax**. The CLI is on the v0.5 roadmap; PackPDF is currently v0.1, GUI-only. None of the commands below exist yet.

```
packpdf add A.pdf{1-7,9,12-15}      # multiple non-overlapping ranges in one shot
packpdf add B.png                    # image as a page
packpdf remove A.pdf{2-3}            # subtract pages from a previously-added segment
packpdf list                         # show the current plan
packpdf compose -o out.pdf           # write the output

# Stateless one-shot form, suited for AI / scripting:
packpdf compose "A.pdf{1-7},B.png,A.pdf{8-10}" -o out.pdf
```

Range syntax: `{a-b,c,d-e}`, all 1-indexed, ranges within a single segment must not overlap. Validation happens at parse time so a typo is rejected before any file is read.

The stateless form is the one that matters for agent use: an LLM can build the comma-separated plan from the user's request and invoke `packpdf compose` once, with no shell session and no incremental state. The same plan is reproducible and diffable.

## Build

### Prerequisites

- **Git**
- **Visual Studio 2026** with the C++ workload (the bundled CMake is used, no separate install needed). VS 2022 also works
- **vcpkg** at any path, exposed via the `VCPKG_ROOT` environment variable. One-time setup if you don't have one:
  ```bat
  git clone https://github.com/microsoft/vcpkg.git <path>\vcpkg
  <path>\vcpkg\bootstrap-vcpkg.bat -disableMetrics
  setx VCPKG_ROOT <path>\vcpkg
  ```

### First time / clean slate

Run `generate.bat` from a **Visual Studio Developer Command Prompt**. It deletes `.vs/`, `.vscode/`, `build/`, then runs `cmake --preset windows-x64`. Run it again any time you want a fresh build state.

### Visual Studio

`File` → `Open` → `Folder...` → select the repo root. Pick `pack-pdf.exe` in the startup-item dropdown, then **F5**. VS reads `CMakePresets.json` and manages the build out-of-tree under `build/windows-x64/`.

### Command line

```bat
cmake --build --preset debug
cmake --build --preset release
```

Executable: `build/windows-x64/bin/<Config>/pack-pdf.exe` (with `pdfium.dll` + `glfw3.dll` copied next to it).

### Output layout

All generated content lands under `build/` (gitignored, GNU-style), driven by `CMakePresets.json`:

```
build/
├── windows-x64/   # CMake cache + .vcxproj + bin/Debug/ + bin/Release/   (binaryDir)
├── fetched/       # ImGui sources + PDFium prebuilt                      (FETCHCONTENT_BASE_DIR)
└── vcpkg/         # vcpkg_installed                                      (VCPKG_INSTALLED_DIR)
```

`fetched/` and `vcpkg/` are siblings of the per-preset CMake dir, so wiping `build/windows-x64/` does not re-download PDFium (~10 MB) or reinstall vcpkg ports. To wipe everything: run `generate.bat` (or manually `rm -rf build/`).

## Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui) — UI (FetchContent, no install)
- [GLFW3](https://www.glfw.org/) — windowing (vcpkg)
- [PDFium](https://pdfium.googlesource.com/pdfium/) — PDF read / render / write, via [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) prebuilt (FetchContent, no source build)
- [stb_image](https://github.com/nothings/stb) — JPEG / PNG decode for image segments and hover previews

## Roadmap

- v0.1 — window, drag-drop ingest, timeline list, per-row PDF range / image options, compose pass for PDF + JPEG + PNG, output folder / filename, theme menu, config persistence **(current)**
- v0.5 — CLI, the same `Composer` engine driven by a stateless command: `packpdf compose "A.pdf{1-7},B.png,A.pdf{8-10}" -o out.pdf`
- v1.0 — portable zip release (pack-pdf.exe + pdfium.dll + glfw3.dll, unzip and run, no installer)

## License

[MIT](LICENSE). © 2026 Hyrex Chia.
