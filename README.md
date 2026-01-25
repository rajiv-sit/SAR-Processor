# SAR-Processor

SAR-Processor is a C++23 synthetic aperture radar (SAR) processing pipeline that moves from SarTape-style IQ ingest
through RPF parsing, back-projection imaging, PTA/QA analysis, and a real-time 2D viewer. The codebase emphasizes
deterministic results, modular components, and test coverage suitable for continuous integration.

## What this repo provides
- SarTape ingest pipeline that parses records, validates sync, and writes output products.
- RPF parsing/streaming with pixel conversions and annotation/geo-grid structures.
- Back-projection engine scaffolding with filtering, registration, and autofocus hooks.
- PTA/QA analysis utilities for basic image quality metrics and reporting.
- Real-time UDP frame publisher and PyQt viewer for 2D visualization.
- Synthetic SarTape2 generator for test data and integration tests.

## Repository layout
- `include/`: public headers for the core library modules.
- `src/`: implementation of the SAR pipeline, RPF, back-projection, PTA, and RTO modules.
- `tests/`: GoogleTest unit and integration tests.
- `configs/`: sample JSON configuration files for back-projection.
- `SarTape2Generator/`: synthetic SarTape2 generator (library + CLI).
- `visualizer/`: PyQt 2D viewer for real-time frames.
- `scripts/`: coverage scripts for Windows and Linux.
- `md/`: internal milestone tracking and notes (not required to build).

## Build prerequisites
- CMake 3.20+
- C++23 compiler (MSVC v143 or GCC/Clang with C++23)
- Conan 2.x
- Python 3.10+ (for the PyQt viewer)
- Git

### Windows (Visual Studio)
1. Open a "x64 Native Tools Command Prompt for VS 2022".
2. Install Conan and build dependencies:
```powershell
py -m pip install --upgrade pip conan
conan profile detect --force
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=23 --output-folder=build
```
3. Configure and build:
```powershell
cmake --preset conan-default
cmake --build build/build --config Debug
```

### Linux (Ubuntu)
1. Install prerequisites:
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake python3 python3-pip
python3 -m pip install --upgrade pip conan
```
2. Install Conan dependencies and build:
```bash
conan profile detect --force
conan install . --build=missing -s build_type=Release -s compiler.cppstd=23 --output-folder=build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Running tests
```powershell
ctest --test-dir build/build -C Debug --output-on-failure
```
On Linux, drop `-C Debug` if using a single-config build directory.

## Running the SarTape ingest CLI
The ingest CLI reads SarTape IQ records and emits output files:
```powershell
.\build\build\Debug\sartape_ingest_cli.exe <input_file> <output_prefix> [--complex]
```
Outputs include `.dat`, `.vts`, `.hdr`, `.ssp`, and optional complex IQ output.

## Running the synthetic SarTape2 generator
```powershell
.\build\build\Debug\sartape2_generator.exe --output <output_prefix> --records 128
```
This can be used for tests or to produce sample input data.

## Sample dataset usage
Sample SarTape2 data for tests lives in `tests/data/`. To run the ingest CLI against the sample:
```powershell
.\build\build\Debug\sartape_ingest_cli.exe tests\data\sartape2_ingest_sample.bin samples\sartape2_out
```
This writes `samples\sartape2_out.dat/.vts/.hdr/.ssp`. The reference outputs used by tests are
`tests/data/sartape2_ref.*`.

## Running the real-time 2D viewer
1. Start the PyQt viewer:
```powershell
py -3 -m pip install PyQt5
py .\visualizer\pyqt_rto_viewer.py 5000
```
2. Publish frames from the pipeline (back-projection preview):
```powershell
.\build\build\Debug\rto_visualizer_cli.exe --endpoint udp://127.0.0.1:5000 --frames 300 --interval-ms 33
```

Optional: pass a JSON file with input parameters (CPI, BW, PRF, sampling rate) for display:
```powershell
py .\visualizer\pyqt_rto_viewer.py 5000 C:\path\to\params.json
```

## Visualizer:
- Simulated:
<img width="1091" height="832" alt="image" src="https://github.com/user-attachments/assets/8e4fc59f-fb62-4e2e-8561-01024858b785" />

- Real:
  
  <img width="605" height="730" alt="image" src="https://github.com/user-attachments/assets/719af42c-e982-4ef7-8067-2ad6ecf3b6f2" />

## Configuration
Back-projection uses JSON config files in `configs/backproj/`.
- `operator.json`: image dimensions, file naming, and output options.
- `secondary.json`: filter parameters and registration/autofocus settings.

## Coverage
- Linux: `scripts/coverage_linux.sh`
- Windows: `scripts/coverage_windows.ps1`

## Notes
- Some integration tests require external data and may skip when inputs are not provided.
- The 2D viewer uses UDP and expects a single-packet frame payload (automatically downsampled if needed).

