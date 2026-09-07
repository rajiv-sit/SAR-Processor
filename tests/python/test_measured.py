"""Opt-in acceptance checks for the local measured datasets and saved production runs.

Set SAR_MEASURED=1 after preparing the processing and scan-stage products described
in docs/real-data-processing.md. The stage checks additionally need the four
per-azimuth GOTCHA reconstructions and all 30 Sandia scan bundles.
Ordinary CI uses small portable fixtures and does not download gigabytes of data.
"""

import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
import pytest
import rasterio
from rasterio.windows import Window
from rasterio.warp import transform
from scipy.signal.windows import taylor

from sar_processing.gotcha import load_gotcha
from sar_processing.model import C


ROOT = Path(__file__).resolve().parents[2]
pytestmark = [
    pytest.mark.measured,
    pytest.mark.skipif(
        os.environ.get("SAR_MEASURED") != "1",
        reason="Set SAR_MEASURED=1 for local real-data checks",
    ),
]


def test_measured_gotcha_saved_image_against_original_frequency_sum():
    history = load_gotcha(ROOT / "data/gotcha")
    output = ROOT / "output/gotcha-processed"
    with np.load(output / "complex_image.npz", allow_pickle=False) as saved:
        image, x, y = saved["image"], saved["x_m"], saved["y_m"]
    assert image.shape == (512, 512)
    assert np.iscomplexobj(image) and np.isfinite(image).all() and np.any(image)
    assert history.samples.shape == (469, 424)
    report = json.loads((output / "processing_report.json").read_text())
    assert report["geocoded"]["status"].startswith("scene-local")
    assert report["validation"]["direct_sum_relative_l2_error"] < 0.01
    selected = [np.unravel_index(i, image.shape) for i in np.argsort(np.abs(image).ravel())[-10:]]
    selected += [(row, col) for row in (0, 128, 256, 511) for col in (0, 128, 256, 511)]
    # Direct measured-frequency sum, without calling the implementation's reference helper.
    weights = np.hanning(469)[:, None] * np.hanning(424)[None, :]
    reference = []
    for row, col in selected:
        distances = np.linalg.norm(history.positions_m - [x[col], y[row], 0], axis=1)
        delta = distances - history.reference_range_m
        phase = np.exp(4j * np.pi / C * delta[:, None] * history.frequency_hz)
        reference.append(np.sum(weights * history.samples * phase) / weights.sum())
    actual = np.asarray([image[row, col] for row, col in selected])
    assert np.linalg.norm(actual - reference) / np.linalg.norm(reference) < 0.01
    with np.load(output / "range_profiles.npz", allow_pickle=False) as profiles:
        assert profiles["stage"].item() == "range_profiles"
        assert profiles["samples"].dtype == np.complex128
        assert np.isfinite(profiles["samples"]).all()
        assert_array_equal(profiles["positions_m"], history.positions_m)


def test_all_thirty_sandia_outputs_preserve_measured_magnitude_phase_and_coordinates():
    sources = sorted((ROOT / "data/sandia/FARAD_X_BAND").glob("*.ntf"))
    assert len(sources) == 30
    output = ROOT / "output/sandia-processed"
    batch = json.loads((output / "processing_report.json").read_text())
    assert len(batch["images"]) == 30
    for path in sources:
        directory = output / path.stem
        report = json.loads((directory / "processing_report.json").read_text())
        with path.open("rb") as raw:
            assert report["source"]["sha256"] == hashlib.file_digest(raw, "sha256").hexdigest()
        with (
            rasterio.open(path) as source,
            rasterio.open(directory / "complex_native.tif") as iq,
            rasterio.open(directory / "magnitude_native.tif") as magnitude,
        ):
            assert iq.shape == magnitude.shape == source.shape
            assert iq.transform == source.transform == magnitude.transform
            assert iq.crs == source.crs == magnitude.crs
            viewer = None
            if report["viewer_raw_written"]:
                with (directory / "image.raw").open("rb") as raw:
                    assert struct.unpack("<II", raw.read(8)) == (source.width, source.height)
                viewer = np.memmap(
                    directory / "image.raw", dtype="<f4", mode="r", offset=8, shape=source.shape
                )
            else:
                assert not (directory / "image.raw").exists()
            for row in range(0, source.height, 256):
                window = Window(0, row, source.width, min(256, source.height - row))
                mag, phase = source.read((1, 2), window=window)
                complex_values = iq.read(1, window=window)
                assert np.isfinite(complex_values).all()
                assert_allclose(np.abs(complex_values), mag, rtol=1e-7, atol=0.005)
                phase_error = np.angle(complex_values.astype(np.complex128)) - (
                    phase.astype(np.float64) * (2 * np.pi / 65536)
                )
                phase_error = np.arctan2(np.sin(phase_error), np.cos(phase_error))
                assert np.max(np.abs(phase_error[mag > 0]), initial=0) < 1e-6
                assert_array_equal(magnitude.read(1, window=window), mag)
                if viewer is not None:
                    assert_array_equal(viewer[row : row + mag.shape[0]], mag)
            del viewer
            # Independent comparison against the precision BLOCKA corner-center strings.
            tags = source.tags()
            for index, name in enumerate(("FRFC", "FRLC", "LRLC", "LRFC")):
                location = tags[f"NITF_BLOCKA_{name}_LOC_01"]
                latitude, longitude = float(location[:10]), float(location[10:])
                assert_allclose(
                    report["native_pixel_coordinates_lon_lat"][index],
                    [longitude, latitude],
                    rtol=0,
                    atol=1e-6,
                )
        with rasterio.open(directory / "magnitude_geocoded.tif") as geo:
            assert geo.crs == rasterio.crs.CRS.from_epsg(32613)
            assert geo.res == (0.25, 0.25)
            values = geo.read(1, masked=True)
            assert values.count() > 0
            assert np.isfinite(values.compressed()).all()
            assert values.min() >= 0 and values.max() <= 65535
            assert np.isnan(geo.nodata)
        assert report["input_stage"] == "focused_complex_image"
        assert (directory / "image.png").stat().st_size > 100


def test_enhanced_gotcha_saved_image_against_independent_weighted_frequency_sum():
    history = load_gotcha(ROOT / "data/gotcha")
    output = ROOT / "output/gotcha-enhanced"
    report = json.loads((output / "processing_report.json").read_text())
    assert report["range_window"] == report["azimuth_window"] == "taylor"
    assert report["interpolation"] == "sinc"
    assert report["additional_autofocus"]["status"] == "not applied"
    assert all(not s["vendor_autofocus"]["applied_by_importer"] for s in report["sources"])
    with np.load(output / "complex_image.npz", allow_pickle=False) as saved:
        image, x, y, z = saved["image"], saved["x_m"], saved["y_m"], saved["z_m"]
    assert image.shape == (512, 512)
    selected = [np.unravel_index(i, image.shape) for i in np.argsort(np.abs(image).ravel())[-25:]]
    selected += [(row, col) for row in (0, 128, 256, 511) for col in (0, 128, 256, 511)]
    taper_options = dict(nbar=4, sll=35, norm=False, sym=True)
    weights = taylor(469, **taper_options)[:, None] * taylor(424, **taper_options)[None, :]
    reference = []
    for row, col in selected:
        distances = np.linalg.norm(history.positions_m - [x[col], y[row], float(z)], axis=1)
        delta = distances - history.reference_range_m
        phase = np.exp(4j * np.pi / C * delta[:, None] * history.frequency_hz)
        reference.append(np.sum(weights * history.samples * phase) / weights.sum())
    actual = np.asarray([image[row, col] for row, col in selected])
    assert np.linalg.norm(actual - reference) / np.linalg.norm(reference) < 0.01
    manifest = json.loads((output / "image.sarframe").read_text())
    assert manifest["product"] == "scene_local_magnitude"
    assert "geolocation" not in manifest
    values = np.fromfile(output / manifest["data_file"], dtype="<f4", offset=8).reshape(image.shape)
    assert_array_equal(values, np.abs(image).astype(np.float32))


def test_all_thirty_scientific_frames_preserve_every_geocoded_pixel_and_coordinate_nodes():
    sequence = ROOT / "output/scientific-viewer/sandia-geocoded"
    manifests = sorted(sequence.rglob("geocoded.sarframe"))
    assert len(manifests) == 30
    for path in manifests:
        manifest = json.loads(path.read_text())
        assert manifest["product"] == "geocoded_magnitude"
        with rasterio.open(manifest["source"]) as source:
            assert (manifest["height"], manifest["width"]) == source.shape
            payload = path.parent / manifest["data_file"]
            assert payload.stat().st_size == 8 + source.width * source.height * 4
            with payload.open("rb") as stream:
                assert struct.unpack("<II", stream.read(8)) == (source.width, source.height)
            values = np.memmap(payload, dtype="<f4", mode="r", offset=8, shape=source.shape)
            try:
                for row in range(0, source.height, 256):
                    window = Window(0, row, source.width, min(256, source.height - row))
                    expected = source.read(1, window=window, masked=True).filled(np.nan)
                    assert_array_equal(values[row : row + expected.shape[0]], expected)
            finally:
                del values
            grid = manifest["geolocation"]
            cols, rows = np.meshgrid(grid["cols"], grid["rows"])
            xs, ys = source.transform * (cols.ravel() + 0.5, rows.ravel() + 0.5)
            longitude, latitude = transform(source.crs, "EPSG:4326", xs, ys)
            assert_allclose(np.asarray(grid["longitude"]).ravel(), longitude, rtol=0, atol=1e-10)
            assert_allclose(np.asarray(grid["latitude"]).ravel(), latitude, rtol=0, atol=1e-10)
            assert grid["interpolation_error_m"] < 0.001


def _checked_frame_manifest(path):
    metadata = json.loads(path.read_text())
    assert metadata["schema"] == "sar-scientific-frame-v1"
    assert type(metadata["width"]) is type(metadata["height"]) is int
    assert min(metadata["width"], metadata["height"]) > 0
    assert metadata["source"] and Path(metadata["source"]).is_file()
    name = metadata["data_file"]
    assert all(char not in name for char in "/\\:") and Path(name).suffix == ".raw"
    payload = path.parent / name
    assert payload.resolve().parent == path.parent.resolve()
    assert not payload.is_symlink()
    shape = (metadata["height"], metadata["width"])
    assert payload.stat().st_size == 8 + 4 * shape[0] * shape[1]
    with payload.open("rb") as stream:
        assert struct.unpack("<II", stream.read(8)) == shape[::-1]
    return metadata, payload


def _check_three_preview_bins(metadata, values):
    """Independent reduction oracle: arithmetic bin edges and original source windows."""
    full_height, full_width = metadata["source_shape"]
    height, width = values.shape
    positions = [(0, 0), (height // 2, width // 2), (height - 1, width - 1)]
    phase = metadata["display_scale"] == "phase_radians"
    source = Path(metadata["source"])

    def compare(reader):
        for row, col in positions:
            top, bottom = row * full_height // height, (row + 1) * full_height // height
            left, right = col * full_width // width, (col + 1) * full_width // width
            if phase:
                source_row, source_col = (top + bottom - 1) // 2, (left + right - 1) // 2
                original = np.ma.asarray(
                    reader(source_row, source_row + 1, source_col, source_col + 1)
                )
                sample = original.data[0, 0]
                if np.ma.getmaskarray(original)[0, 0] or abs(sample) == 0:
                    expected = np.nan
                else:
                    expected = np.arctan2(float(sample.imag), float(sample.real))
                assert_allclose(values[row, col], expected, rtol=0, atol=4e-7, equal_nan=True)
            else:
                original = np.ma.asarray(reader(top, bottom, left, right)).compressed()
                expected = (
                    np.nan
                    if original.size == 0
                    else np.sqrt(np.mean(np.abs(original.astype(np.complex128)) ** 2))
                )
                assert_allclose(values[row, col], expected, rtol=2e-7, atol=1e-7, equal_nan=True)

    if source.suffix == ".npz":
        with np.load(source, allow_pickle=False) as archive:
            key = "samples" if metadata["product"] == "range_profile_magnitude" else "image"
            original = archive[key]
            assert original.shape == (full_height, full_width)
            compare(lambda top, bottom, left, right: original[top:bottom, left:right])
    else:
        with rasterio.open(source) as raster:
            assert raster.shape == (full_height, full_width)
            compare(
                lambda top, bottom, left, right: raster.read(
                    1, window=Window(left, top, right - left, bottom - top), masked=True
                )
            )


def test_all_34_scan_bundles_expose_102_truthful_stages_and_correct_intermediate_pixels():
    sandia = ROOT / "output/scientific-viewer/sandia-geocoded"
    gotcha = ROOT / "output/scientific-viewer/gotcha-stages"
    sandia_scans = sorted(sandia.rglob("scan.sarscan"))
    gotcha_scans = sorted(gotcha.rglob("scan.sarscan"))
    assert len(sandia_scans) == 30 and len(gotcha_scans) == 4
    available_count = 0
    phase_count = 0
    for path in sandia_scans + gotcha_scans:
        scan = json.loads(path.read_text())
        assert scan["schema"] == "sar-scientific-scan-v1"
        assert scan["label"] == path.parent.name and scan["source"]
        entries = {entry["id"]: entry for entry in scan["stages"]}
        assert len(entries) == len(scan["stages"])
        is_sandia = path in sandia_scans
        if is_sandia:
            assert set(entries) == {
                "range",
                "backprojection",
                "focused_magnitude",
                "focused_phase",
                "geocoded",
            }
            assert scan["default_stage"] == "geocoded"
            for missing in ("range", "backprojection"):
                assert entries[missing]["status"] == "unavailable"
                assert "already-focused" in entries[missing]["reason"]
                assert "manifest" not in entries[missing]
            assert entries["geocoded"]["manifest"] == "geocoded.sarframe"
        else:
            assert set(entries) == {"range", "backprojection", "backprojected_phase", "geocoded"}
            assert scan["default_stage"] == "backprojection"
            assert entries["geocoded"]["status"] == "unavailable"
            assert "No verified Earth origin" in entries["geocoded"]["reason"]
            assert "manifest" not in entries["geocoded"]
            assert entries["backprojection"]["manifest"] == "image.sarframe"
        pixels = 0
        for entry in entries.values():
            assert entry["label"]
            if entry["status"] == "unavailable":
                continue
            assert entry["status"] == "available"
            available_count += 1
            name = entry["manifest"]
            assert all(char not in name for char in "/\\:") and Path(name).suffix == ".sarframe"
            manifest, payload = _checked_frame_manifest(path.parent / name)
            height, width = manifest["height"], manifest["width"]
            pixels += height * width
            if name in {"geocoded.sarframe", "image.sarframe"}:
                # The original full-resolution finals are referenced directly,
                # never replaced by intermediate previews or copied stage payloads.
                assert manifest["data_file"] == ("geocoded.raw" if is_sandia else "image.raw")
                assert manifest["source"] == scan["source"]
                assert "display_only" not in manifest
                continue
            assert manifest["display_only"] is True and max(height, width) <= 512
            assert len(manifest["source_shape"]) == 2
            assert manifest["preview_resampling"] and manifest["source_pixel_mapping"]
            values = np.fromfile(payload, dtype="<f4", offset=8).reshape(height, width)
            assert np.isfinite(values).any() and not np.isinf(values).any()
            if manifest["display_scale"] == "phase_radians":
                phase_count += 1
                assert manifest["units"] == "radians"
                valid = values[np.isfinite(values)]
                assert valid.min() >= -np.float32(np.pi) and valid.max() <= np.float32(np.pi)
                assert np.any(valid < 0) and np.any(valid > 0)
                assert manifest["product"] == (
                    "focused_phase" if is_sandia else "backprojected_phase"
                )
            else:
                assert manifest["display_scale"] == "amplitude_db"
                assert manifest["units"] == "uncalibrated magnitude DN"
                assert np.all(values[np.isfinite(values)] >= 0)
                assert manifest["product"] == (
                    "focused_magnitude" if is_sandia else "range_profile_magnitude"
                )
            if entry["id"] == "range":
                assert height == manifest["source_shape"][0] and height in (117, 118)
                assert width == 512 and manifest["source_shape"][1] == 2048
                axes = manifest["axes"]
                assert axes["rows"]["source_extent"] == [0, height - 1]
                assert axes["columns"]["source_extent"] == [0, 2047]
                assert axes["differential_range"]["units"] == "m"
                with np.load(manifest["source"], allow_pickle=False) as archive:
                    starts = archive["range_start_m"]
                    step = float(archive["range_step_m"])
                for key, expected in {
                    "sample_step": step,
                    "start_min": starts.min(),
                    "start_max": starts.max(),
                    "end_min": starts.min() + 2047 * step,
                    "end_max": starts.max() + 2047 * step,
                }.items():
                    assert_allclose(axes["differential_range"][key], expected, rtol=1e-14)
            _check_three_preview_bins(manifest, values)
        assert pixels <= 64_000_000
    assert available_count == 102 and phase_count == 34


def test_four_gotcha_aperture_scans_match_independent_three_point_frequency_sums():
    root = ROOT / "output/scientific-viewer/gotcha-stages"
    pulse_counts = []
    for azimuth in range(1, 5):
        directory = root / f"az{azimuth:03d}"
        history = load_gotcha(ROOT / "data/gotcha", first_az=azimuth, last_az=azimuth)
        report = json.loads((directory / "processing_report.json").read_text())
        assert report["first_az"] == report["last_az"] == azimuth
        assert report["validation"]["direct_sum_relative_l2_error"] < 0.01
        assert report["range_window"] == report["azimuth_window"] == "taylor"
        assert report["interpolation"] == "sinc" and report["range_upsample"] == 4
        assert report["additional_autofocus"]["status"] == "not applied"
        assert all(
            not source["vendor_autofocus"]["applied_by_importer"] for source in report["sources"]
        )
        assert report["pulses"] == history.samples.shape[0]
        pulse_counts.append(report["pulses"])
        with np.load(directory / "complex_image.npz", allow_pickle=False) as saved:
            image, x, y, z = saved["image"], saved["x_m"], saved["y_m"], float(saved["z_m"])
        assert image.shape == (512, 512)
        peak = np.unravel_index(np.abs(image).argmax(), image.shape)
        selected = list(dict.fromkeys([peak, (256, 256), (0, 0), (511, 511)]))[:3]
        assert len(selected) == 3
        # Physical two-way phase sum uses measured frequencies and geometry directly,
        # independently of the processor's FFT, interpolation, or reference helper.
        options = dict(
            nbar=report["taylor"]["nbar"],
            sll=report["taylor"]["sidelobe_setting_db"],
            norm=False,
            sym=True,
        )
        weights = (
            taylor(history.samples.shape[0], **options)[:, None]
            * taylor(history.samples.shape[1], **options)[None, :]
        )
        expected = []
        for row, col in selected:
            delta = (
                np.linalg.norm(history.positions_m - [x[col], y[row], z], axis=1)
                - history.reference_range_m
            )
            rotation = np.exp(4j * np.pi / C * delta[:, None] * history.frequency_hz)
            expected.append(np.sum(history.samples * weights * rotation) / weights.sum())
        actual = np.asarray([image[row, col] for row, col in selected])
        assert np.linalg.norm(actual - expected) / np.linalg.norm(expected) < 0.01
        manifest, payload = _checked_frame_manifest(directory / "image.sarframe")
        assert manifest["product"] == "scene_local_magnitude" and "geolocation" not in manifest
        magnitude = np.fromfile(payload, dtype="<f4", offset=8).reshape(image.shape)
        assert_array_equal(magnitude, np.abs(image).astype(np.float32))
    assert pulse_counts == [117, 117, 118, 117]
