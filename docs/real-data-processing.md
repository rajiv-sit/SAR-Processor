# Real-data processing and validation

## Dataset stages and provenance

[Sandia's complex-data collection](https://www.sandia.gov/radar/pathfinder-radar-isr-and-synthetic-aperture-radar-sar-systems/complex-data-clone/) supplies complex SAR imagery. The downloaded [FARAD X-band archive](https://www.sandia.gov/files/radar/complex-data/FARAD_X_BAND.zip) contains 30 already-focused NITF 2.1 images, totaling 1,937,915,922 bytes after extraction. The archive SHA-256 is `ba4380f31089cdaafe1dd0fd37b95a6da64450bf591b6f4c4328675b46eff785`.

These NITFs store two unsigned 16-bit bands labeled `M,P`, with 16 actual bits per band. The importer reconstructs `I + jQ = magnitude * exp(j * phase_code * 2*pi/65536)`, following the [NGA generic unsigned magnitude/phase convention](https://github.com/ngageoint/sarpy/blob/master/sarpy/io/general/format_function.py). It rejects alternate bit depths, reversed bands, multiple image segments, truncation, incomplete BLOCKA coverage, and CMETAA amplitude remapping. Values are uncalibrated digital numbers, not calibrated radar cross section. Integer-to-float conversion precedes phase scaling to avoid unsigned overflow.

[AFRL GOTCHA](https://www.sdms.afrl.af.mil/index.php?collection=gotcha) supplies frequency-domain phase histories and antenna geometry. The local four-file HH subset comes from the [RITSAR example collection](https://github.com/dm6718/RITSAR/tree/0e36d2ed50a92304c9b2c92ad1e1e46e042011e5/examples/data), pass 1, azimuth files 001–004. It contains 469 pulses and 424 frequencies spanning approximately 622.36 MHz. Source hashes are recorded in every processing report. Antenna positions and reference ranges are scene-local; this subset supplies no verified Earth origin.

## Processing contracts

1. **Raw echo:** correlate each received pulse against the conjugate time-reversed supplied replica using linear convolution. The default uses the original replica and normalizes by its energy. Optional Hann/Taylor weighting produces a normalized mismatched filter, as described below. Lag zero is at that pulse's receive-window start. No circular wraparound is allowed.
2. **Frequency phase history:** fit a uniform frequency grid while tolerating GOTCHA's float32 frequency quantization. Apply the range taper, zero-pad, IFFT, and shift to signed differential-range samples. This creates range profiles from the supplied frequency measurements; it does not recover unrecorded time-domain raw echoes.
3. **Azimuth compression:** for every antenna position and image pixel, compute slant range minus the supplied reference range, interpolate the complex range profile, apply the two-way carrier correction `exp(+j*4*pi*f_min*delta_range/c)`, and sum coherently. For raw input the reference range is zero and the carrier is `carrier_hz`. Flat tiles bound temporary image arrays; complex128 phase and coherent weighting normalization are retained. Each pixel can use either a constant scene height or a verified DEM-derived height. Changing range is already included in pulse-to-pixel geometry, so no separate approximate range-cell migration correction is added.
4. **Geographic export:** preserve source complex samples on their original grid. Produce a separate projected magnitude raster with bilinear resampling and explicit nodata. PNG relative-peak logarithmic display scaling does not change numerical products.

A focused Sandia image cannot enter the raw matched-filter or phase-history path. `--operation form-image` with Sandia fails explicitly. Missing or invalid inputs never trigger synthetic substitution.

## Commands and options

After installing the package, run the two measured-data commands in the [README](../README.md). Raw input uses:

```powershell
.venv/Scripts/python.exe -m sar_processing --format raw --input data/echo.npz --output output/raw-processed --pixels 512 --scene-width 100
```

| Option | Meaning and default |
| --- | --- |
| `--window` | `hann`, `none`, or `taylor`. GOTCHA frequency weighting defaults to Hann; raw replica weighting defaults to none. Already-focused Sandia rejects range weighting. |
| `--azimuth-window` | `hann`, `none`, or `taylor` coherent pulse weighting; default Hann. |
| `--taylor-nbar`, `--taylor-sll-db` | Taylor parameters shared by the selected range/azimuth tapers; defaults 4 and 35 dB. Supported values are 2-32 and 15-120 dB. |
| `--upsample 1..128` | GOTCHA range IFFT upsampling; default 32. Raw echo input rejects this option. |
| `--interpolation` | `linear` or `sinc` complex range interpolation; default linear. Sinc uses a normalized Lanczos window. |
| `--sinc-half-width 2..32` | Sinc uses twice this many measured taps; default 8. |
| `--tile-pixels 1..1048576` | Maximum temporary image tile size; default 65536. The full complex output image remains in memory. |
| `--preview-db 1..120` | PNG display dynamic range; default 50 dB. It does not modify scientific samples. |

Every image pixel must fall inside every pulse's recorded range swath. Fractional sinc positions also require their complete measured support; integer endpoints remain exact. No clamping, circular wrapping, or extrapolation fills missing data. Sinc assumes a sufficiently sampled bandlimited range profile, so retain adequate IFFT upsampling. Reduce the scene width or supply suitable geometry if the grid exceeds the measurements.

For raw replica `s` and taper `w`, the denominator is `D = sum(abs(s)**2 * w)`, preserving the complex amplitude of an isolated replica-shaped return. White input noise with variance `sigma**2` has output variance `sigma**2 * sum(abs(s)**2 * w**2) / D**2` at full-overlap lags. This is a mismatched filter when a taper is selected: reduced sidelobes trade against resolution and matched-filter SNR. For example:

```powershell
.venv/Scripts/python.exe -m sar_processing --format raw --input data/echo.npz --output output/raw-taylor --window taylor --taylor-nbar 4 --taylor-sll-db 35 --azimuth-window taylor --interpolation sinc
```

An input directory containing both GOTCHA and Sandia is ambiguous in automatic mode; choose `--format`. Sandia batch filenames must have unique stems. Existing output directories are preserved; a failed batch removes its temporary outputs. Allow several gigabytes of output storage for the full Sandia collection.

The README's Sandia batch command uses `--skip-viewer-raw` to omit both native-grid and geocoded float32 viewer payloads. The native-grid copy alone totals approximately 1.94 GB. All complex/magnitude GeoTIFFs and PNGs are still written. Omit this flag to create viewer products during processing, or use the separate geographic sequence exporter below. Reports record `viewer_raw_written` and `scientific_viewer`.

## Interpolation and response measurements

The default remains linear interpolation. On the local GOTCHA HH pass-1 files 001-004, a 64 by 64 grid spanning -40 to +40 metres used all 469 pulses and Hann weighting in both dimensions. Each timing is the median of three backprojection runs, excluding import, range compression, export, and reference evaluation. Numerical error compares 50 distributed/strong pixels with an independent sum at the original measured frequencies:

| IFFT upsampling | Interpolation | Relative L2 error | Median backprojection time |
| --- | --- | --- | --- |
| 32 | Linear | 0.000635987 | 0.1366 s |
| 32 | Sinc, half-width 8 | 0.000104829 | 0.8297 s |
| 4 | Sinc, half-width 8 | 0.000480977 | 0.7739 s |

Sinc reduced the 32-times-upsampled numerical error by approximately sixfold while taking approximately six times longer than optimized linear interpolation. Direct uniform-grid indexing reduced the linear case from 0.3073 s to 0.1366 s without changing that error. These workstation measurements establish a numerical/performance tradeoff, not external scene accuracy or guaranteed real-time throughput. The local evidence is `output/signal-enhancement-benchmark.json`.

`sar_processing.quality.impulse_response_metrics` accepts a uniformly sampled isolated-target response cut. Half-power width uses linearly interpolated power crossings at -3.0103 dB. The default main lobe includes the nearest local power minima on both sides of the peak; callers can supply inclusive sample-index bounds. Peak sidelobe ratio (PSLR) is peak sidelobe power divided by peak main-lobe power. Integrated sidelobe ratio (ISLR) is total measured power outside the bounds divided by power inside them. Missing bounding minima or half-power crossings fail instead of producing misleading metrics. Zero sidelobes have ratio zero and a JSON `null` dB value, representing negative infinity.

GOTCHA reports include the ideal range-window impulse response and its width in slant-range metres. They do not measure scene-wide resolution, clutter suppression, or geographic accuracy. For a 424-sample ideal aperture, default Taylor weighting produced width 1.18416 nominal bins, PSLR -35.167 dB, and ISLR -27.131 dB; Hann produced 1.44399 bins, -31.467 dB, and -32.884 dB. Taylor narrows the main lobe and lowers the peak sidelobe in this comparison while increasing total sidelobe energy. Choose weighting for the actual task instead of treating one window as universally better.

The README's enhanced 512 by 512 GOTCHA run uses Taylor in both dimensions, upsampling 4, and sinc interpolation. That local run completed in 73.67 seconds, with a 50-point direct-sum relative L2 error of 0.000218770 (0.0219%). Its ideal range-window report gave a 0.284536 m half-power width, -35.1716 dB PSLR, and -27.1314 dB ISLR. These values describe that configuration's numerical consistency and ideal window response; they do not establish measured target resolution, autofocus improvement, or Earth location accuracy.

## Controlled phase correction and producer metadata

GOTCHA `--autofocus-point X Y Z` requires an independently identified isolated, stationary, phase-stable scatterer at the supplied scene coordinate. It estimates a constant phase error per pulse after geometric demodulation, then anchors corrections to the first pulse's phase. It rejects weak or frequency-incoherent reference returns. Reports record the point, phase errors, and coherence before/after. The Python API additionally exposes coherence and relative-amplitude thresholds.

One reference cannot distinguish target position error or changing scattering phase from platform error. This option does not estimate delay, frequency-dependent phase, spatially varying atmosphere, or blind scene autofocus. It is off by default. Tests inject known phase errors and noise; no independently validated reference was supplied for the measured GOTCHA scene, so no such correction is claimed there.

The importer validates and preserves per-pulse `af.r_correct` and `af.ph_correct` fields under each source's `vendor_autofocus` report. Their units, sign, and upstream application state remain unknown. They are not automatically reapplied or combined with the optional reference-point correction; field names alone are insufficient to establish a safe correction convention.

## Raw NPZ schema

NPZ archives are loaded with pickles disabled. Required entries:

| Key | Shape/type | Meaning |
| --- | --- | --- |
| `stage` | scalar string `raw_echo` | Explicitly identifies uncompressed echoes |
| `samples` | complex `(pulses, receive_samples)` | Complex baseband receive windows |
| `replica` | complex `(chirp_samples,)` | Transmitted reference samples at the same sampling rate, starting at time zero |
| `sampling_rate_hz` | positive finite scalar | Receive and replica sample rate |
| `carrier_hz` | positive finite scalar | Carrier frequency |
| `receive_start_s` | finite nonnegative `(pulses,)` | Delay of first receive sample relative to each transmit event |
| `positions_m` | finite `(pulses, 3)` | Antenna coordinates in the output grid's Cartesian scene frame |

The signal convention is `a * replica(t - 2R/c) * exp(-j*4*pi*carrier_hz*R/c)`. A recording with another demodulation sign, reference delay, or coordinate frame must be converted explicitly. Real magnitude data, focused images, and NPZ range profiles are rejected as raw input. The SarTape2 generator's phase convention differs; there is no automatic adapter.

## Geographic coordinates

Sandia georeferencing uses [GDAL's NITF driver](https://gdal.org/en/stable/drivers/raster/nitf.html), including precision BLOCKA metadata. Affine transforms or ground control points are retained. Coordinates refer to pixel **centers**, respecting the half-pixel offset from GeoTIFF corner transforms. Reports include the four native corner centers and center longitude/latitude.

Sandia reports also preserve the original `ACFTB`, `EXPLTB`, and `MENSRB` image records and normalize supported spacings, angles, positions, reference pixels, and NED direction vectors. Unit, range, vector, and consistency checks produce explicit issues for unusable optional values. MENSRB altitude/elevation values are converted from feet above MSL into metres above MSL; their specific geoid is not known. These summary records do not establish a complete image-to-range/Doppler sensor model, and zero-valued accuracy fields do not establish zero positioning error. Their normalization follows the [GDAL schema](https://github.com/OSGeo/gdal/blob/master/frmts/nitf/data/nitf_spec.xml) and [NGA's corresponding reader conventions](https://github.com/ngageoint/sarpy/blob/master/sarpy/io/complex/other_nitf.py).

Inspection of all 30 local Sandia headers found two `ANGLE_TO_NORTH=360.000` values outside the schema's accepted range. Those source values remain in the raw records and are reported as issues; the importer does not silently turn them into usable normalized angles. The local header inventory is `output/enhancement-sandia-metadata.json`.

GOTCHA/raw images require a known `--origin-lat`, `--origin-lon`, optional `--origin-height` in WGS84 ellipsoid metres, and `--x-heading` clockwise from north. Default heading 90 degrees makes scene x east and y north; z is up. Conversion uses ECEF/ENU and geographic control points. Do not assign Sandia's coordinates to the unrelated GOTCHA scene. Without an origin, images retain local metre coordinates in NPZ and the report states that geographic coordinates are unavailable.

The default destination is local WGS84 UTM; all 30 local Sandia images use EPSG:32613. `--crs` accepts a projected CRS with metre units. An explicit suitable CRS is required outside UTM latitude coverage. Masks and nodata are combined before reprojection. Valid zeros remain valid; exterior pixels are NaN. Warping uses temporary disk-backed tiles to preserve masks and explicit source geometry.

GCP-based export explicitly selects thin-plate-spline mapping through GDAL's [`SRC_METHOD=GCP_TPS` transformer option](https://gdal.org/en/stable/api/gdal_alg.html). With Rasterio 1.5.1/GDAL 3.12, an upstream warning can still report that the warp operation does not support `SRC_METHOD`: [Rasterio passes the same extra keywords to both transformer and warp options](https://github.com/rasterio/rasterio/blob/1.5.1/rasterio/_warp.pyx#L519-L572). TPS is selected by the transformer, and a nonlinear nine-GCP regression checks the resulting mapping. The warning remains visible. This GCP map warp is distinct from DEM-supported radar image-formation geometry.

Sandia export remains reprojection using source metadata. It does not add terrain orthorectification, layover correction, absolute radiometric calibration, or independently surveyed accuracy. A 0.25 m output pixel spacing is a sampling choice, not a measured spatial resolution.

### Optional DEM-supported image formation

GOTCHA/raw backprojection can use `--dem path/to/heights.tif --dem-height-reference ellipsoid` together with a verified scene origin and heading. The DEM must be a single real band with a valid horizontal CRS, an affine grid, and band units explicitly declaring metres. Its heights must be relative to the WGS84 ellipsoid; tags declaring another vertical datum are rejected. Convert orthometric/MSL heights using a suitable verified geoid before using this option. Declaring the flag does not perform that conversion.

The processor bilinearly samples bounded DEM windows and iterates the local scene surface through ECEF coordinates, accounting for heading and Earth curvature. The resulting per-pixel local-Z heights are used directly in every pulse-to-pixel range computation and are retained in `complex_image.npz`. Missing supporting data, unsupported units/datum, insufficient coverage, or failure to converge stop processing. The 0.1 mm height-closure tolerance describes numerical convergence, not geographic accuracy. The report records DEM source/hash and the applied geometry.

For a dataset whose coordinate frame and DEM have been independently established, set the variables from that evidence before running:

```powershell
.venv/Scripts/python.exe -m sar_processing --format raw --input data/echo.npz --output output/raw-terrain --origin-lat $sceneLatitude --origin-lon $sceneLongitude --origin-height $sceneEllipsoidHeight --x-heading $sceneXHeading --dem $ellipsoidDemPath --dem-height-reference ellipsoid
```

The local GOTCHA subset has no verified Earth origin, so the measured reconstruction remains scene-local. Sandia `--dem` requests fail because a calibrated sensor model and a resolved MSL/geoid relationship are still required for terrain projection of those focused images. Terrain algorithms were checked against controlled geographic/height fixtures and sloped-surface point-target sums; no measured terrain accuracy improvement is claimed for the local collections.

## Full-resolution geocoded ImGui playback

Use the existing Direct RPF ImGui executable with scientific `.sarframe` manifests. For completed Sandia processing outputs, export a fresh geographic sequence and validate it without opening a window:

```powershell
.venv/Scripts/python.exe -m sar_processing.viewer --input output/sandia-processed --output output/scientific-viewer/sandia-geocoded
.\build\Debug\sar_imgui_rpf_viewer_cli.exe --input-dir output\scientific-viewer\sandia-geocoded --validate-inputs
.\build\Debug\sar_imgui_rpf_viewer_cli.exe --input-dir output\scientific-viewer\sandia-geocoded --publish-sleep-ms 250 --loop
```

These commands assume a graphics-enabled binary under `build/Debug`; see the [native build instructions](../README.md#build-native-tools). The export command needs a new directory and reads the existing `magnitude_geocoded.tif` files without changing them. Skip export when that sequence already exists. A fresh processing run with viewer export enabled writes scientific manifests directly into its output, so the viewer can open that directory without a second export.

Each geographic frame contains the full float32 magnitude grid and NaN nodata in `geocoded.raw`, plus a `geocoded.sarframe` JSON manifest with dimensions, relative payload path, product/units, original source path, CRS, and a sampled longitude/latitude grid. The reader checks dimensions, payload size, valid sample ranges, schema, geographic mapping, and safe relative payload paths. `--validate-inputs` emits frame validation results as JSON and returns failure for invalid inputs.

The display maintains full source resolution, uses mipmaps when zoomed out, and exposes logarithmic dynamic range (10-100 dB), grayscale/inverse/Hot/Turbo color maps, a histogram, and fit-to-window. Mouse-wheel zoom and right/middle-drag pan remain available. **Pause to inspect** freezes the displayed frame while **Resume playback** picks up incoming frames. Hovering shows the geocoded raster's pixel index, unscaled magnitude, nodata state, and available latitude/longitude. Display scaling does not replace or alter scientific samples.

Longitude/latitude is interpolated from the source GeoTIFF's geographic mapping. The manifest reports a sampled cell-midpoint interpolation residual; it is neither a global error bound nor independent ground-location accuracy. Scene-local GOTCHA `.sarframe` products omit this geographic mapping and are labeled accordingly. The loop plays separate completed acquisitions; `--publish-sleep-ms 250` adds a delay between publications, with actual frame rate also limited by decoding, disk, GPU upload, and display work. It is not a live SAR-processing pipeline or a continuous acquisition timeline.

Legacy `.rpf` playback and `--frame-mode file-index` remain supported. The older Sandia RPF previews have placeholder geographic annotations; use scientific `.sarframe` products for final geocoded output and the GeoTIFF/NPZ products for numerical processing.

## Per-scan processing stages

`python -m sar_processing.stages --input <prepared-sequence-or-reconstruction>` adds a `scan.sarscan` bundle beside each existing final frame. It publishes new stage payloads and manifests first, then the scan manifest. Existing final products are reused unchanged. A collision or failure rolls back only that scan's new files; earlier completed scan bundles remain available. Stage export refuses to replace existing products.

| Dataset | Available stages | Explicitly unavailable |
| --- | --- | --- |
| Sandia FARAD | Focused native magnitude, focused native phase, final geocoded magnitude | Earlier range compression and backprojection measurements were not supplied |
| Local GOTCHA reconstruction | Range profiles, backprojected magnitude, backprojected phase | Geographic output needs a verified scene origin |

The viewer chooses `.sarscan` bundles when present so intermediate `.sarframe` files do not become unrelated playback frames. Its default 2-by-2 dashboard renders range compression, azimuth compression/backprojection, geocoordinates, and final image from one scan at once. The azimuth panel uses Sandia's producer-focused native magnitude with explicit upstream provenance; this does not claim locally rerun backprojection. Geocoordinates use the actual geographic frame and coordinate mapping. The final panel shows the scan's default completed magnitude image. Missing stages keep a visible reason, never fabricated pixels.

Single-stage inspection is available with `--stage <id>`, including phase. Selecting a stage pauses the current scan and retains an available selection on subsequent scans. If a requested stage is absent from the next scan, its default available image is displayed with an explanation. Phase values use a linear -pi to +pi scale and radians in the cursor readout; zero-magnitude complex samples have undefined phase and display as nodata.

Intermediate previews have at most 512 pixels per axis to keep scan switching and storage bounded. Magnitude reduction averages intensity over valid samples before taking the square root. Phase selects the nearest source sample to each preview bin's centre; it does not average wrapped angles. Range heatmaps retain pulse rows up to the preview limit and reduce range columns separately, because their axes have different units. Preview manifests record original dimensions, source paths, resampling, and range-axis metadata. They are display products; use the full NPZ/GeoTIFF arrays for scientific analysis. The existing final magnitude payload keeps its original resolution.

The [README](../README.md#view-each-scan-and-its-processing-stages-in-imgui) gives commands for 30 Sandia scans and four GOTCHA azimuth blocks. `--render-check-frames N` verifies actual OpenGL panel pixels before buffer swap, reports the scan and panel status, and exits after N displayed updates. With `--stage`, it checks the requested single-stage image instead. `--render-capture path.ppm` saves the first render. Read-only manifest validation does not establish that the desktop account can read files or that the graphics path draws them.

On Windows, published output directories now inherit their destination parent's access rules. Python's owner-only temporary-directory permissions previously survived publication, preventing a desktop account from reading outputs created by a different worker account. A Windows ACL regression test verifies inheritance. Existing affected generated outputs were restored to their parent's permissions; source datasets were not changed. The viewer reports directory access failures visibly instead of manufacturing a black status image.

## Outputs and resource limits

GOTCHA/raw output includes `complex_image.npz` (complex128 image and x/y/z grid), `range_profiles.npz` (complex128 samples and range/antenna metadata), `image.raw`, `image.png`, and `processing_report.json`. With a scene origin, native complex/magnitude GeoTIFFs, `magnitude_geocoded.tif`, `image_geocoded.png`, and `geocoded.sarframe`/`geocoded.raw` are added. Without an origin, `image.sarframe` identifies the scene-local `image.raw` magnitude product. DEM runs retain a per-pixel height array instead of a scalar z value.

Sandia creates a subfolder per source containing native complex/magnitude GeoTIFFs, geocoded magnitude, `image_native.png`, geocoded `image.png`, and a report, plus a batch report. Unless `--skip-viewer-raw` is selected, each subfolder also contains the legacy native `image.raw` and scientific `geocoded.sarframe`/`geocoded.raw`. The standalone sequence exporter creates numbered subdirectories containing geographic payloads/manifests and a top-level `sequence.json` with source provenance.

PNG dimensions are capped at 1600 pixels. Downsampling averages intensity before converting back to magnitude and applying logarithmic display scaling, avoiding single-pixel subsampling gaps. Raw exports preserve the entire grid as two little-endian uint32 dimensions (width, height), followed by row-major float32 magnitude. Scientific geographic raw payloads use NaN for nodata and remain separate from the legacy finite native payload. GeoTIFF complex64/float32 exports reject numeric overflow; NPZ retains full precision.

Complex matrices and padded range FFT allocations are limited to 32 million samples. Image formation is limited to 4096×4096 pixels; Sandia native rasters and geocoded outputs to 64 million pixels. Selected GOTCHA file bytes and raw NPZ file/expanded-member bytes are limited to 512 MiB. NPZ member headers and declared payload sizes are checked before loading. Numeric NPY versions 1/2 are supported; object arrays and NPY version 3 are rejected. These are individual allocation limits, not peak-memory guarantees. Compressed MAT expansion still uses SciPy's in-memory reader. Reduce aperture size/upsampling for larger datasets. Geographic warping needs temporary disk space.

## Acceptance tests

Portable tests independently verify matched-filter lag/amplitude and weighted correlation, off-center point targets, coherent phase, fractional complex interpolation, terrain-height target geometry, impulse-response measurements, injected reference-phase errors, swath boundaries, malformed input, geographic axes and centers, DEM units/coverage/nodata, metadata conventions, file round trips, and rollback. Viewer tests cover full-resolution samples, source-pixel coordinates, longitude wrapping, masks/nodata, invalid manifests and payloads, and logarithmic scaling. Every GOTCHA production run compares 25 distributed plus up to 25 strongest pixels against a direct measured-frequency sum using the selected weighting and any explicitly applied reference correction; relative L2 error must be at most 1%. This establishes numerical consistency, not external scene ground truth.

The measured checks require baseline GOTCHA and Sandia, `output/gotcha-enhanced`, the full-resolution Sandia sequence, four GOTCHA aperture runs under `output/scientific-viewer/gotcha-stages`, and all 34 `.sarscan` bundles. The README gives these preparation commands. Then run:

```powershell
$env:SAR_MEASURED = '1'
.venv/Scripts/python.exe -m pytest tests/python/test_measured.py -q
Remove-Item Env:SAR_MEASURED
```

The measured checks verify all 30 Sandia source hashes, every exported native magnitude/complex pixel, phase error, any requested native viewer payloads, BLOCKA corners, projected CRS and nodata. They also compare every full-resolution scientific Sandia frame pixel and coordinate node against its source GeoTIFF, and independently compare both baseline Hann and enhanced Taylor/sinc GOTCHA images against original measured frequency samples with their respective weighting. Raw time-domain matched filtering uses controlled chirp fixtures; the Sandia collection cannot validate that earlier acquisition stage.
