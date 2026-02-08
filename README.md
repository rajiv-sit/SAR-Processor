# SAR-Processor

SAR-Processor is a C++23 synthetic aperture radar (SAR) pipeline that supports:
- SarTape2 Q/I ingest
- RPF parsing
- Back-projection image formation
- PTA/QA analysis
- Native ImGui visualization

## Concept
You can run the system in two practical ways:

1. `SarTape -> RPF -> back-projection image -> PTA/QA`
2. `RPF -> back-projection image -> PTA/QA`

For visualization, this repo provides two ImGui viewers:
- `sar_imgui_viewer_cli`: shared-memory frame viewer (for back-projected image frames)
- `sar_imgui_rpf_viewer_cli`: direct `.rpf` folder viewer

## Prerequisites
- CMake 3.20+
- C++23 compiler (MSVC v143 or GCC/Clang)
- Conan 2.x
- Python 3.10+ (for Conan tooling)
- Git

## Build (Windows)
From `SAR-Processor`:
```powershell
py -m pip install --upgrade pip conan
conan profile detect --force
conan install . --build=missing --output-folder=build `
  -pr:h=conan/profiles/msvc194-host-debug `
  -pr:b=conan/profiles/msvc194-build-release

cmake --preset conan-default
cmake --build build/build --config Debug
```

## Build (Linux)
```bash
conan profile detect --force
conan install . --build=missing -s build_type=Release -s compiler.cppstd=23 --output-folder=build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run Path 1: SarTape -> Image
From `SAR-Processor`:

1. Run full pipeline from SarTape input:
```powershell
.\build\build\Debug\pipeline_runner_cli.exe --sartape <path_to_sartape_input> --out output
```

2. Visualize generated back-projection image via shared memory:
```powershell
.\build\build\Debug\rto_visualizer_cli.exe --cached-raw output\pipeline_backproj.raw --shared-file output\rto_shared.bin --frames 300 --interval-ms 33
.\build\build\Debug\sar_imgui_viewer_cli.exe --shared-file output\rto_shared.bin
```

Outputs include `output\pta_report.json` and back-projection artifacts.

## Run Path 2: RPF -> Image
From `SAR-Processor`:

1. Run full pipeline from RPF input:
```powershell
.\build\build\Debug\pipeline_runner_cli.exe --rpf <path_to_input.rpf> --out output
```

2. Visualize back-projection image via shared memory:
```powershell
.\build\build\Debug\rto_visualizer_cli.exe --cached-raw output\pipeline_backproj.raw --shared-file output\rto_shared.bin --frames 300 --interval-ms 33
.\build\build\Debug\sar_imgui_viewer_cli.exe --shared-file output\rto_shared.bin
```

## Direct RPF Viewer (ImGui)
If you want to inspect RPF content directly (without back-projection), run:
```powershell
.\build\build\Debug\sar_imgui_rpf_viewer_cli.exe --input-dir ..\SAR-Enhanced\data\rpf\RPFAccum1 --frame 1 --frame-mode file-index --publish-sleep-ms 250 --loop
```

Controls:
- Mouse wheel: zoom
- Right-drag or middle-drag: pan
- `--frame-mode file-index`: advances frame number with file index

## Visualizer Figure
Add a screenshot of the ImGui visualizer here.
<img width="1599" height="973" alt="image" src="https://github.com/user-attachments/assets/3e5ce281-9047-4307-87af-104e3a57402f" />

## Tests
```powershell
ctest --test-dir build/build -C Debug --output-on-failure
```

## Notes
- Back-projection configs are in `configs/backproj/operator.json` and `configs/backproj/secondary.json`.
- If build fails with `LNK1168` for a viewer `.exe`, close the running viewer and rebuild.
- If viewer shows `Waiting for data`, verify the input path contains `.rpf` files.
