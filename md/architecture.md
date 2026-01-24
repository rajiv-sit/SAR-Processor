# C++ Architecture for SAR IQ-to-Image Pipeline

## Purpose
Document a comprehensive C++23 implementation plan for the SAR/ISAR processing pipeline currently expressed in MATLAB. This architecture mirrors the existing flow from raw radar IQ ingest -> RPF chunk parsing -> back-projection -> PTA/QA analysis, while leveraging modern C++ techniques (RAII, design patterns, Eigen numerics, Conan-managed dependencies, and CMake builds) to ensure performance, safety, and portability.

## Key Objectives
- Preserve the MATLAB data contracts (SarTape2 reads, `.rpf/.wnf` annotations, geo-grids, PTA chips/stats) while introducing typed C++ models.  
- Maintain streaming-oriented ingestion/back-projection to handle airborne SAR volumes without unbounded memory usage.  
- Provide extensible, testable modules by applying Strategy, Factory, Builder, and Pipeline patterns so new algorithms/settings can be injected without touching core logic.  
- Use Eigen for FFT/adaptive filtering, RAII for resource management (files, buffers), and Conan+CMake to maintain toolchain agnosticity across platforms.

## Architectural Decomposition

### 1. SarTape Reader Layer
**Responsibility:** Read SarTape2 Q/I data (`XDM_IngestSarTape2`, `XDM_ReadRecord`) and emit structured records/storage for downstream stages.  
**Core Classes:**  
  * `SarTapeConstants` (singleton/policy) - exposes record sizes, symbol constants.  
  * `SarTapeReader` (strategy-aware) - encapsulates file handling, uses RAII `FileHandle` wrapper, exposes `readRecord()` returning `SarTraceRecord` (header + IQ payload).  
  * `SarTapeIngestPipeline` - combines builder-configured options (`ErrorHandlingPolicy` for fatal vs best-effort) and writes `.dat`, `.vts`, `.hdr`, `.ssp`.  
**Pattern:** Builder + Strategy (error handling), RAII for `std::ifstream` + custom buffer, Observer for logging progress (per 100 lines) if needed.  
**Dependencies:** Eigen for sample conversions if complex outputs required; Boost.Exec/Asio optional for asynchronous reads.  
**Memory/Complexity:** Fixed-size buffers per record; configurable limit for queued records. Use `std::vector<int8_t>` with `reserve(recordSize)` to avoid reallocations.

### 2. RPF Product Layer
**Responsibility:** Parse `.rpf/.wnf` chunk-structured files, decode annotations, extract geo-grids/imagedata, and provide streaming/block interfaces like `RPF_ProductRead`/`RPF_BlockRead`.  
**Core Classes:**  
  * `RpfChunkReader` - uses `RpfConstants` and RAII file handles, reads chunk headers, dispatches to specific handlers (image chunk, annotation chunk, geo-grid).  
  * `RpfImageDataParser` - converts pixel bytes to Eigen arrays (float, complex, half via `halfprecision` helper).  
  * `LatLongGrid`/`AnnotationStruct` - value objects mirroring MATLAB struct fields.  
  * `RpfProductStream` - provides iterator-style access to frames/lines (Pipeline pattern) with lazy loading.  
**Pattern:** Factory (create appropriate pixel parser per `pixelType`), Strategy (image conversion strategies), Pipeline (streaming).  
**Memory:** Stream per block, avoid storing entire file in RAM; use `Eigen::MatrixXf` for per-block data, reuse buffers via `std::unique_ptr`/`std::shared_ptr` pools.

### 3. Back-Projection Config & Engine
**Responsibility:** Feed image formation with operator/secondary params (`BackProjOperatorParams`, `BackProjSecondaryParams`), compute range/azimuth filters, run quad-tree + registration/autofocus, and emit TIFF/RPF outputs.  
**Core Classes:**  
  * `BackProjOperatorConfig`, `BackProjSecondaryConfig` - immutable config objects built via Builder pattern from JSON/YAML (config factory).  
  * `FilterBank` - encapsulates range/azimuth filters, exposes Eigen-based convolution/FFT operators.  
  * `BackProjectionEngine` - orchestrates data movement, accepts `AnnotationStruct` + `LatLongGrid`, outputs images, using RAII-managed image buffers.  
  * `RegistrationManager`, `AutofocusController` - encapsulate frame-to-frame registration and autofocus selection settings (copy from `frameToFrameReg`, `selectPointsForAf`, `pgaAf`).  
**Pattern:** Strategy (range/azimuth filter strategies, autopfocus selection), Observer for registration results, Pipeline for tile processing.  
**Performance:** Use Eigen FFT (or FFTW via wrapper) for range/azimuth compression; process tiles sequentially/parallel with thread pool but keep per-thread memory bounded.

### 4. PTA/QA Analysis Layer
**Responsibility:** Analyze image chips for peaks, compute stats (IRW, MSLR, ISLR, etc.), optionally plot/print results, and feed automation reports back into RPF outputs.  
**Core Classes:**  
  * `PtaAnalyzer` (Strategy) - orchestrates `Analyze1D`, `Analyze2D`, `Stats1Dfrom2D`.  
  * `PtaChip` - holds Eigen matrices plus metadata (`magFactor`, `zpAlpha`, etc.).  
  * `TtlRunner` - reads `.prs`-like configs, runs PTA/GPU tasks, writes reports/histograms.  
**Pattern:** Strategy + Template Method to unify 1D/2D analysis, Observer for reporting/histogram updates.  
**Memory:** Chips are small (windowed tiles); reuse Eigen buffers via buffer pool pattern to minimize allocations.

### 5. Numeric Utilities
**Responsibility:** Provide half-precision conversions and other signal utilities.  
**Core Components:**  
  * `HalfPrecisionConverter` (RAII for LUTs), `ComplexSamples` wrappers, `DecimalFormatter` (for stats outputs).  
  * Leverage Eigen's Map for reinterpretation without copying (critical for half conversions).  
**Pattern:** Utility singletons or constexpr functions for constant tables.

### 6. Real-Time PyQt Visualizer (RTO)
**Responsibility:** Provide a live visualization UI for IQ inputs, intermediate tiles, and final SAR outputs in real time (RTO).  
**Core Components:**  
  * `RtoDataBus` - a lightweight publish/subscribe channel for streaming frames and metrics from C++ to Python.  
  * `RtoFrameBuffer` - bounded ring buffer to prevent unbounded memory usage while rendering.  
  * `PyQtRtoViewer` - GUI layer showing side-by-side input/output views, status, and processing latency.  
**Integration Model:**  
  * C++ publishes downsampled frames and metadata via IPC (ZeroMQ/shared memory/UDP).  
  * Python/PyQt subscribes and renders in a separate process to avoid blocking the pipeline.  
  * Optional "record to disk" mode for later replay and debugging.
**Pattern:** Observer for streaming updates, Pipeline for display stages, and RAII-backed IPC lifecycle management in C++.

## Functional Requirements
1. Support SarTape2 ingestion producing `.dat`, `.vts`, `.hdr`, `.ssp`, with retry/fatal policies.  
2. Enumerate `.rpf/.wnf` blocks, parse chunk headers, convert image pixels (float/complex/half), and provide annotation + geo-grid structures.  
3. Accept configuration via JSON/YAML for operator/secondary parameters, ensuring the back-projection engine applies range/azimuth filters, autofocus, registration, and output scaling identical to MATLAB behavior.  
4. Deliver streaming APIs (iterators/pipelines) to feed data into the back-projection engine, avoiding large memory spikes.  
5. Produce PTA stats + histograms, auto-annotate outputs, and optionally emit TTL-style reports.  
6. Keep data conversions (half precision, complex) deterministic and testable by comparing to MATLAB outputs.  
7. Manage concurrency safely for ingestion/backprojection using RAII-managed threads/resources.
8. Provide a real-time visualization pipeline (PyQt) for IQ inputs and SAR outputs with bounded buffering and explicit latency reporting.

## Non-Functional Requirements
- **Performance:** Data ingestion and back-projection must keep pace with airborne data rates; design for chunked streaming, buffer reuse, and parallel tile processing within fixed memory footprints.  
- **Memory Safety:** Use RAII (`std::unique_ptr`, `std::vector`, scope-based destructors) to control file handles/buffers; avoid GC/heap leaks.  
- **Portability:** Build via CMake + Conan; support GCC/Clang/MSVC on Windows/Linux/macOS without source changes.  
- **Maintainability:** Apply SOLID principles, prefer composition over inheritance where possible, and use standardized configuration/config builder libraries (`nlohmann/json`, `yaml-cpp`).  
- **Testability:** Each module exposes interfaces amenable to unit tests (e.g., `ISarTapeReader`, `IRpfParser`, `IBackProjEngine`).  
- **Traceability:** Log per-block/frame metadata, maintain consistent naming for outputs (mirroring MATLAB naming) to help compare test artifacts.

## Dependency & Build Strategy
- **Language Standard:** C++23 for modules, coroutines (optional for async streaming), and `std::expected`/`std::span` if available.  
- **Libraries:**  
  * Eigen (matrix math, FFTs, vector ops).  
  * FFTW or custom Eigen-based FFT for range/azimuth compression.  
  * `nlohmann/json`, `yaml-cpp` for configuration.  
  * Conan manages versions (Eigen, FFTW) and ensures consistent dependencies.  
- **Build System:** CMake for cross-platform builds; use modern target definitions (`target_link_libraries`, `target_compile_features`) to enforce warnings and standard options.  
- **Tooling:** Add clang-format/check-style config, catch2 or doctest for tests, and use CI (GitHub Actions) for platform matrix builds.  
- **Packaging:** Provide a `conanfile.py`/`conanfile.txt` referencing Eigen + FFTW; CMake fetches Conan dependencies via `cmake-conan` module to ensure reproducible builds.

## Suggested Design Patterns
- **Builder:** For constructing `BackProjOperatorConfig`/`BackProjSecondaryConfig` from JSON/YAML.  
- **Strategy:** For `PixelParser` (per `pixelType`), `SarTapeErrorPolicy`, `RangeFilter`, and PTA analysis methods.  
- **Factory:** To create `RpfChunkHandler` instances (image, annotation, geo-grid).  
- **Pipeline:** For `SarTapeReader` -> `RpfParser` -> `BackProjectionEngine` flow, enabling testing/mocking at each stage.  
- **Observer:** For instrumentation/logging (e.g., log each 100th video line, PTA histogram updates).  
- **RAII & Composition:** Always wrap files (`std::ifstream`), buffers, and threads in RAII types; prefer composing small deterministic components rather than large monoliths.

## Quality & Verification
- Compare generated `.dat`/`.rpf`/`.tif` outputs against MATLAB references (e.g., `RpfAcFi1_0001.rpf`) to validate conversions.  
- Unit tests for each parser/converter (SarTape, chunk header, geo-grid) using sample files (`RPFAccum1_files`).  
- Integration tests run a trimmed pipeline (ingest + one block read + PTA stats) in CI to ensure end-to-end compatibility.  
- Static analysis (clang-tidy, cppcheck) for safety/performance; ensure `-Wall -Wextra -Werror` are enabled in debug builds.

## Next Steps
1. Define C++ interfaces (`ISarTapeReader`, `IRpfParser`, `IBackProjectionEngine`, `IPtaAnalyzer`) and mock implementations for testing.  
2. Implement the SarTape reader + RPF parser core, validate `.dat` + annotation outputs against MATLAB.  
3. Layer in back-projection engine and PTA/TTL analysis, reusing Eigen for filters and ensuring config parity with the `.m` parameter files.  
4. Add Conan + CMake scaffolding, include CI pipelines, and document the mapping between MATLAB structs and the new C++ value objects within `architecture.md`.
## Repository Layout (Design Diagram)

```
SAR-Processor
|--- cmake/
|   `--- toolchain/      # Optional custom toolchain configs for cross-compilation
|--- configs/
|   |--- backproj/       # JSON/YAML operator+secondary parameter files
|   `--- ttl/            # PTA/QA parameter templates
|--- data/
|   |--- raw/            # SarTape2 inputs (mirrors FLT104_... folders)
|   |--- rpf/            # `.rpf/.wnf` product samples (equivalent to RPFAccum1_files)
|   `--- frame1/         # Example processing outputs (TIFFs, MAT-like serialized states)
|--- include/
|   |--- sar/            # SarTape reader interfaces, constants
|   |--- rpf/            # RPF chunk reader, annotation models
|   |--- backproj/       # Backprojection configs, filters, registration/autofocus controllers
|   |--- pta/            # PTA/IQA analysis interfaces
|   `--- utils/          # Numeric helpers (HalfPrecision, logging, builders)
|--- src/
|   |--- sar/
|   |   |--- SarTapeReader.cpp
|   |   |--- SarTraceRecord.cpp
|   |   `--- SarTapeIngestPipeline.cpp
|   |--- rpf/
|   |   |--- RpfChunkReader.cpp
|   |   |--- RpfImageDataParser.cpp
|   |   `--- RpfProductStream.cpp
|   |--- backproj/
|   |   |--- BackProjectionEngine.cpp
|   |   |--- FilterBank.cpp
|   |   `--- RegistrationManager.cpp
|   |--- pta/
|   |   |--- PtaAnalyzer.cpp
|   |   `--- TtlRunner.cpp
|   `--- utils/
|       `--- HalfPrecisionConverter.cpp
|--- tests/
|   |--- sar/
|   |--- rpf/
|   |--- backproj/
|   `--- pta/
|--- conanfile.txt
|--- conanfile.py
|--- CMakeLists.txt
|--- .clang-format
|--- .clang-tidy
`--- README.md
```

Each directory mirrors a MATLAB module: `data/raw` parallels `FLT104_H_circle_leg4_C1_rpf`, `src/rpf` mirrors `Tools/common/rpf`, etc. README/architecture describe usage; tests ensure parity with the MATLAB outputs.

## Coding Standards and Conventions

### File Naming and Organization
- **Headers:** Use `.hpp` for all header files; keep declarations, interfaces, and inline/constexpr utilities only.  
- **Sources:** Use `.cpp` for implementations, one-to-one with headers.  
- **Folders/Modules:** Use lowercase or snake_case names that mirror responsibility (e.g., `backprojection`, `rpf_parser`, `pta_analysis`).  

### Naming Conventions
- **Classes/Structs:** PascalCase nouns (e.g., `SarTapeReader`, `BackProjectionEngine`, `RpfChunkParser`).  
- **Functions/Methods:** camelCase verbs (e.g., `readRecord()`, `parseChunkHeader()`, `computeRangeFilter()`).  
- **Variables/Data Members:** camelCase with domain terms (e.g., `iqSamples`, `geoGrid`, `rangeBins`).  
- **Constants/Enums:** `kPascalCase` or `UPPER_SNAKE_CASE` (e.g., `kMaxChunkSize`, `DEFAULT_TILE_SIZE`).  

### Comments and Documentation
- **Public API:** Doxygen-style comments for all public classes/functions with purpose, parameters, returns, and assumptions.  
- **Module Headers:** Each header must describe its module responsibility and usage context.  
- **Complex Logic:** Add inline comments that explain intent and algorithmic reasoning (not low-level mechanics).  

### Code Structure and Readability
- Prefer small, single-responsibility classes and functions; refactor long methods.  
- Use explicit types and descriptive names, even if verbose.  
- Avoid deep inheritance; use composition unless polymorphic substitution is required.  

### Modern C++ Practices
- Use RAII, value semantics, and `std::unique_ptr`/`std::shared_ptr` for ownership clarity.  
- Apply const-correctness to functions, parameters, and members.  
- Avoid macros where `constexpr`, `enum class`, or templates suffice.  
- Favor `std::span`, `std::expected`, and ranges where supported to express intent safely.  

### Consistency Enforcement
- Enforce formatting via `clang-format` and static analysis via `clang-tidy`.  
- Apply these standards uniformly across production code, tests, and utilities.  
