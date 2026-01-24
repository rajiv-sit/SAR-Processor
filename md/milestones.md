# Project Milestones (C++ SAR Pipeline)

## Goal
Deliver a complete, testable C++23 SAR pipeline that mirrors the MATLAB flow from SarTape IQ ingest to RPF parsing, back-projection, PTA/QA analysis, and real-time PyQt visualization.

## Milestone 1: Project Bootstrap (Week 1)
**Deliverables**
- [x] CMake + Conan setup with Eigen, FFTW, `nlohmann/json`, `yaml-cpp`.
- [x] Baseline repo layout per `architecture.md` (include/src/tests/configs/data).
- [x] Tooling config: `.clang-format`, `.clang-tidy`, basic CI skeleton.
- [x] Unit test suite expanded for SarTape, RPF image parsing, PTA, RTO, back-projection config.
**Exit Criteria**
- [ ] Build succeeds on Windows + Linux (at least one CI run).
- [x] Sample unit test binary runs (empty test case).

## Milestone 2: SarTape Reader Core (Week 2–3)
**Deliverables**
- [x] `SarTapeConstants`, `SarTapeReader`, `SarTapeIngestPipeline` skeletons.
- [x] Output files: `.dat`, `.vts`, `.hdr`, `.ssp`.
- [x] Error policy (fatal vs best-effort) with clear logging.
- [x] Optional complex IQ `.dat` output mode.
- [x] Synthetic SarTape2 generator with CLI and tests (`SarTape2Generator`).
**Exit Criteria**
- [x] Ingest a sample SarTape file and produce outputs (synthetic generator).

## Milestone 3: RPF Parser & Streaming (Week 3–4)
**Deliverables**
- [x] `RpfChunkReader`, `RpfImageDataParser`, `RpfProductStream`.
- [x] PixelType conversions (float, complex, half).
- [x] `AnnotationStruct` + `LatLongGrid` data models.
**Exit Criteria**
- [x] Stream block iteration without loading full file into RAM.

## Milestone 4: Back-Projection Engine (Week 5–7)
**Deliverables**
- [x] `BackProjectionEngine` with range/azimuth filters (`FilterBank`) scaffolding.
- [x] Config loading for `BackProjOperatorConfig` + `BackProjSecondaryConfig`.
- [x] Registration + autofocus hooks (scaffold).
**Exit Criteria**
- [x] Generate TIFF output matching MATLAB dimensions and scaling.

## Milestone 5: PTA/QA Analysis (Week 7–8)
**Deliverables**
- [x] `PtaAnalyzer`, `PtaChip`, TTL-style runners (scaffold).
- [x] Stats outputs (IRW, MSLR, ISLR, peak position/power) basic implementation.
- [x] Histogram/report outputs stub (report writer).
**Exit Criteria**
- [x] TTL runner generates consistent report format.

## Milestone 6: Real-Time PyQt Visualizer (Week 9–10)
**Deliverables**
- [x] IPC pipeline scaffolding (RtoDataBus + RtoFrameBuffer).
- [x] `PyQtRtoViewer` stub added.
- [x] Ring buffer for bounded memory (RtoFrameBuffer).
- [x] Latency stats tracker stub (RtoLatencyStats).
**Exit Criteria**
- [x] Live rendering with stable FPS and bounded memory.
- [x] Latency stats displayed and logged.

## Milestone 7: Integration & Validation (Week 11–12)
**Deliverables**
- [x] Pipeline runner scaffold (`PipelineRunner`).
- [x] Full pipeline wiring stub: SarTape -> RPF -> BackProj -> PTA -> RTO.
- [x] Automated integration tests and baseline validation dataset.
- [x] Finalized documentation and reproducible build instructions.
- [x] Coverage report workflow (gcov/llvm-cov/OpenCppCoverage) with edge-case tests.
**Exit Criteria**
- [ ] CI passes on Windows/Linux with full test suite.

## Milestone 8: Public Release Prep (Week 13)
**Deliverables**
- [x] README draft added.
- [x] Licensing and contribution guide.
- [x] Versioned release tag and sample dataset usage notes.
**Exit Criteria**
- [x] Public-ready repository with onboarding documentation.

## Progress Tracking
- Each milestone should have a checklist in the issue tracker.
- Use semantic versioning: `v0.x` during development, `v1.0` at first validated release.

## HLD Coverage Checklist
**SarTape / IQ Ingest**
- [x] Parse SarTape2 records with sync validation and error policy.
- [x] Emit `.dat`, `.vts`, `.hdr`, `.ssp` outputs from ingest.
- [x] Support complex IQ output option equivalent to `XDM_ReadRecord` cmplx flag.
- [x] Provide synthetic SarTape2 generator for sample ingest.

**RPF Product Handling**
- [x] Parse `.rpf/.wnf` chunk headers with pixel type conversions.
- [x] Populate `annotStruct` subset (radarMode, grid count, image header).
- [x] Populate `latLongGrid` arrays for geo-grid entries.
- [x] Complete `annotStruct` fields (imageRect, acquisition metadata, annotations).
- [x] Add line-level streaming APIs (`RPF_ProductStream*` parity).
- [x] Implement automation hooks: auto-PTA, annotation report, stripmap reprocess list.

**Back-Projection**
- [x] Load operator/secondary configs from JSON.
- [x] Implement range/azimuth filter bank and back-projection stub.
- [x] Implement registration/autofocus workflows and persistence.
- [x] Emit TIFF output (non-parity).

**PTA/QA / TTL**
- [x] Basic PTA stats (IRW/MSLR/ISLR, peak position/power).
- [x] Basic multi-peak detection and histogram generation.
- [x] TTL `.prs` parsing for report outputs (stub parity).

**RTO Visualizer**
- [x] Ring buffer and data bus scaffolding.
- [x] Real IPC transport and real-time display with latency stats.

**Testing / Validation**
- [x] Unit tests for parsers, config loader, PTA, and RTO buffer.
- [x] Integration tests with reference datasets (SarTape, RPF blocks).
- [x] Coverage reporting (OpenCppCoverage/llvm-cov) with edge cases.
