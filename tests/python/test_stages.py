"""Stage previews preserve original products, complex phase, and publication integrity."""

import json
from pathlib import Path
import runpy
import struct
import sys

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
import pytest
import rasterio
from rasterio.transform import from_origin

from sar_processing import stages
from sar_processing.outputs import write_json
from sar_processing.stages import export_scan, export_stages, main


def make_frame(
    directory, *, product="geocoded_magnitude", source=None, shape=(4, 4), name="geocoded"
):
    directory.mkdir(parents=True, exist_ok=True)
    values = np.arange(np.prod(shape), dtype=np.float32).reshape(shape)
    (directory / f"{name}.raw").write_bytes(
        struct.pack("<II", shape[1], shape[0]) + values.tobytes()
    )
    path = directory / f"{name}.sarframe"
    write_json(
        path,
        {
            "schema": "sar-scientific-frame-v1",
            "width": shape[1],
            "height": shape[0],
            "data_file": f"{name}.raw",
            "source": str(source or (directory / "magnitude_geocoded.tif").resolve()),
            "product": product,
            "units": "uncalibrated magnitude DN",
        },
    )
    return path


def raster(path, values, *, mask=None, count=1):
    path.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(
        path,
        "w",
        driver="GTiff",
        width=values.shape[1],
        height=values.shape[0],
        count=count,
        dtype=values.dtype,
        crs="EPSG:32613",
        transform=from_origin(500000, 4000000, 1, 1),
    ) as dataset:
        dataset.write(values, 1)
        if mask is not None:
            dataset.write_mask(mask)


def sandia_fixture(tmp_path, name="one", *, shape=(4, 4)):
    source = tmp_path / "scientific" / name
    values = (np.arange(np.prod(shape)).reshape(shape) + 1).astype(np.float32)
    raster(source / "magnitude_native.tif", values)
    phase = np.linspace(-3.13, 3.13, values.size).reshape(shape)
    raster(source / "complex_native.tif", (values * np.exp(1j * phase)).astype(np.complex64))
    frame = make_frame(
        tmp_path / "sequence" / name,
        source=(source / "magnitude_geocoded.tif").resolve(),
        shape=shape,
    )
    return frame, source, values, phase


def gotcha_fixture(tmp_path, *, geographic=False):
    directory = tmp_path / "gotcha"
    frame = make_frame(
        directory,
        product="scene_local_magnitude",
        name="image",
        source=(directory / "complex_image.npz").resolve(),
    )
    samples = np.arange(32).reshape(4, 8).astype(np.complex128) + 1j
    np.savez(
        directory / "range_profiles.npz",
        stage="range_profiles",
        samples=samples,
        range_start_m=np.array([-4.0, -3.0, -2.0, -1.0]),
        range_step_m=np.array(0.5),
    )
    image = np.ones((4, 4), complex) * np.exp(1j * np.linspace(-3, 3, 16).reshape(4, 4))
    np.savez(directory / "complex_image.npz", stage="complex_image", image=image)
    if geographic:
        make_frame(directory)
    return frame, samples, image


def preview(directory, name):
    manifest = json.loads((directory / f"stage_{name}.sarframe").read_text())
    raw = (directory / manifest["data_file"]).read_bytes()
    assert struct.unpack("<II", raw[:8]) == (manifest["width"], manifest["height"])
    return manifest, np.frombuffer(raw[8:], dtype="<f4").reshape(
        manifest["height"], manifest["width"]
    )


def test_sandia_stage_bundle_preserves_finals_and_never_invents_compression(tmp_path):
    frame, source, magnitude, phase = sandia_fixture(tmp_path)
    original = {
        p: p.read_bytes() for p in [frame, frame.with_suffix(".raw"), *source.glob("*.tif")]
    }
    scan = json.loads(export_scan(frame, max_pixels=2).read_text())
    assert scan["schema"] == "sar-scientific-scan-v1" and scan["default_stage"] == "geocoded"
    entries = {entry["id"]: entry for entry in scan["stages"]}
    for stage in ("range", "backprojection"):
        assert entries[stage]["status"] == "unavailable"
        assert "already-focused" in entries[stage]["reason"]
        assert "manifest" not in entries[stage]
    assert entries["geocoded"]["manifest"] == frame.name
    metadata, measured = preview(frame.parent, "focused_magnitude")
    assert metadata["source_shape"] == [4, 4] and metadata["display_only"] is True
    expected = np.array(
        [[np.sqrt(np.mean(magnitude[r : r + 2, c : c + 2] ** 2)) for c in (0, 2)] for r in (0, 2)]
    )
    assert_allclose(measured, expected, rtol=1e-7)
    metadata, measured = preview(frame.parent, "focused_phase")
    assert metadata["units"] == "radians" and metadata["display_scale"] == "phase_radians"
    assert_allclose(measured, phase[np.ix_([0, 2], [0, 2])], atol=3e-7)
    for path, contents in original.items():
        assert path.read_bytes() == contents
    assert not list(frame.parent.glob(".scan-stages-*"))


def test_phase_does_not_average_wrapped_angles_and_zero_amplitude_has_undefined_phase(tmp_path):
    frame, source, _, _ = sandia_fixture(tmp_path)
    values = np.ones((4, 4), complex)
    values[:, ::2] *= np.exp(3.13j)
    values[:, 1::2] *= np.exp(-3.13j)
    values[2, 2] = 0
    raster(source / "complex_native.tif", values)
    export_scan(frame, max_pixels=2)
    _, phase = preview(frame.parent, "focused_phase")
    assert_allclose(phase[np.isfinite(phase)], 3.13, atol=1e-6)
    assert np.isnan(phase[1, 1])


def test_intensity_average_preserves_narrow_bright_feature_and_valid_zero(tmp_path):
    frame, source, _, _ = sandia_fixture(tmp_path)
    values = np.zeros((4, 4), np.float32)
    values[1, 1] = 8
    mask = np.ones((4, 4), np.uint8) * 255
    mask[2:, 2:] = 0
    raster(source / "magnitude_native.tif", values, mask=mask)
    export_scan(frame, max_pixels=2)
    _, measured = preview(frame.parent, "focused_magnitude")
    assert_array_equal(measured[:1], [[4, 0]])
    assert measured[1, 0] == 0 and np.isnan(measured[1, 1])


@pytest.mark.parametrize("shape", [(5, 7), (1, 7), (7, 1)])
def test_preview_handles_nondivisible_dimensions_and_singleton_axis(tmp_path, shape):
    frame, _, _, _ = sandia_fixture(tmp_path, shape=shape)
    export_scan(frame, max_pixels=3)
    _, values = preview(frame.parent, "focused_magnitude")
    assert max(values.shape) <= 3 and min(values.shape) >= 1


@pytest.mark.parametrize("geographic", [False, True])
def test_gotcha_stages_include_range_axes_phase_and_reused_formed_image(tmp_path, geographic):
    frame, samples, image = gotcha_fixture(tmp_path, geographic=geographic)
    original = frame.with_suffix(".raw").read_bytes()
    assert export_stages(frame.parent, max_pixels=4) == 1
    scan = json.loads((frame.parent / "scan.sarscan").read_text())
    entries = {entry["id"]: entry for entry in scan["stages"]}
    assert scan["default_stage"] == ("geocoded" if geographic else "backprojection")
    assert entries["backprojection"]["manifest"] == "image.sarframe"
    assert entries["geocoded"]["status"] == ("available" if geographic else "unavailable")
    if not geographic:
        assert "No verified Earth origin" in entries["geocoded"]["reason"]
    metadata, actual = preview(frame.parent, "range")
    assert metadata["product"] == "range_profile_magnitude"
    axis = metadata["axes"]["differential_range"]
    assert axis["units"] == "m" and axis["sample_step"] == 0.5
    assert (axis["start_min"], axis["start_max"]) == (-4, -1)
    assert (axis["end_min"], axis["end_max"]) == (-0.5, 2.5)
    expected = np.array(
        [
            [np.sqrt(np.mean(abs(samples[r : r + 1, c : c + 2]) ** 2)) for c in range(0, 8, 2)]
            for r in range(4)
        ]
    )
    assert_allclose(actual, expected, rtol=1e-7)
    metadata, phase = preview(frame.parent, "backprojected_phase")
    assert metadata["product"] == "backprojected_phase"
    assert_allclose(phase, np.angle(image), atol=2e-7)
    assert frame.with_suffix(".raw").read_bytes() == original


@pytest.mark.parametrize(
    "name", ["scan.sarscan", "stage_focused_phase.raw", "stage_focused_magnitude.sarframe"]
)
def test_colliding_outputs_are_preserved_without_new_products(tmp_path, name):
    frame, _, _, _ = sandia_fixture(tmp_path)
    existing = frame.parent / name
    existing.write_bytes(b"keep unchanged")
    before = set(frame.parent.iterdir())
    with pytest.raises(FileExistsError, match="preserved"):
        export_scan(frame, max_pixels=2)
    assert existing.read_bytes() == b"keep unchanged"
    assert set(frame.parent.iterdir()) == before


def test_publication_failure_rolls_back_only_new_files_and_scan_publishes_last(
    tmp_path, monkeypatch
):
    frame, _, _, _ = sandia_fixture(tmp_path)
    before = {p: p.read_bytes() for p in frame.parent.iterdir()}
    original = stages.os.link
    names = []

    def failing(source, target):
        names.append(Path(target).name)
        if len(names) == 3:
            raise OSError("injected full disk")
        original(source, target)

    monkeypatch.setattr(stages.os, "link", failing)
    with pytest.raises(OSError, match="full disk"):
        export_scan(frame, max_pixels=2)
    assert {p: p.read_bytes() for p in frame.parent.iterdir()} == before
    names.clear()

    def recording(source, target):
        names.append(Path(target).name)
        original(source, target)

    monkeypatch.setattr(stages.os, "link", recording)
    export_scan(frame, max_pixels=2)
    assert names[-1] == "scan.sarscan"


@pytest.mark.parametrize("maximum", [1, 513, 1.5, True])
def test_invalid_preview_maximum(tmp_path, maximum):
    with pytest.raises(ValueError, match="integer"):
        export_scan(tmp_path / "not-opened.sarframe", max_pixels=maximum)


@pytest.mark.parametrize(
    "change",
    [
        "schema",
        "dimensions",
        "source",
        "sibling",
        "short_header",
        "shape",
        "size",
        "large_manifest",
        "product",
    ],
)
def test_invalid_final_manifest_or_payload_is_rejected(tmp_path, change):
    frame, _, _, _ = sandia_fixture(tmp_path)
    metadata = json.loads(frame.read_text())
    payload = frame.with_suffix(".raw")
    if change == "schema":
        metadata["schema"] = "other"
    elif change == "dimensions":
        metadata["width"] = True
    elif change == "source":
        metadata["source"] = ""
    elif change == "sibling":
        metadata["data_file"] = "../outside.raw"
    elif change == "short_header":
        payload.write_bytes(b"1")
    elif change == "shape":
        payload.write_bytes(struct.pack("<II", 7, 7))
    elif change == "size":
        payload.write_bytes(payload.read_bytes()[:-1])
    elif change == "large_manifest":
        metadata["extra"] = "x" * 1_048_576
    else:
        metadata["product"] = "not_geocoded"
    write_json(frame, metadata)
    with pytest.raises(ValueError):
        export_scan(frame, max_pixels=2)
    assert not (frame.parent / "scan.sarscan").exists()
    assert not list(frame.parent.glob("stage_*"))


@pytest.mark.parametrize(
    "kind",
    [
        "nan",
        "negative",
        "overflow",
        "all_masked",
        "phase_real",
        "phase_zero",
        "band_count",
        "different_shape",
    ],
)
def test_invalid_native_values_are_rejected_without_touching_final(tmp_path, kind):
    frame, source, _, _ = sandia_fixture(tmp_path)
    amplitude = np.ones((4, 4), np.float64)
    if kind == "nan":
        amplitude[0, 0] = np.nan
    elif kind == "negative":
        amplitude[0, 0] = -1
    elif kind == "overflow":
        amplitude[0, 0] = 1e40
    elif kind == "phase_real":
        raster(source / "complex_native.tif", amplitude)
    elif kind == "phase_zero":
        raster(source / "complex_native.tif", np.zeros((4, 4), complex))
    elif kind == "different_shape":
        raster(source / "complex_native.tif", np.ones((3, 3), complex))
    mask = np.zeros((4, 4), np.uint8) if kind == "all_masked" else None
    raster(
        source / "magnitude_native.tif",
        amplitude,
        mask=mask,
        count=2 if kind == "band_count" else 1,
    )
    before = {p: p.read_bytes() for p in frame.parent.iterdir()}
    with pytest.raises(ValueError):
        export_scan(frame, max_pixels=2)
    assert {p: p.read_bytes() for p in frame.parent.iterdir()} == before


@pytest.mark.parametrize("change", ["stage", "real", "starts", "step"])
def test_invalid_range_archive_metadata(tmp_path, change):
    frame, samples, _ = gotcha_fixture(tmp_path)
    fields = dict(
        stage="range_profiles", samples=samples, range_start_m=np.zeros(4), range_step_m=0.5
    )
    if change == "stage":
        fields["stage"] = "raw_echo"
    elif change == "real":
        fields["samples"] = np.abs(samples)
    elif change == "starts":
        fields["range_start_m"] = np.zeros(3)
    else:
        fields["range_step_m"] = -1
    np.savez(frame.parent / "range_profiles.npz", **fields)
    with pytest.raises(ValueError, match="Range archive"):
        export_scan(frame)
    assert not (frame.parent / "scan.sarscan").exists()


@pytest.mark.parametrize("change", ["stage", "shape", "product"])
def test_invalid_formed_image_metadata(tmp_path, change):
    frame, _, image = gotcha_fixture(tmp_path)
    if change == "product":
        metadata = json.loads(frame.read_text())
        metadata["product"] = "unsupported_product"
        write_json(frame, metadata)
    else:
        np.savez(
            frame.parent / "complex_image.npz",
            stage="other" if change == "stage" else "complex_image",
            image=image if change == "stage" else np.ones((3, 3), complex),
        )
    with pytest.raises(ValueError):
        export_scan(frame)


def test_pixel_budget_includes_final_and_all_available_previews(tmp_path, monkeypatch):
    frame, _, _, _ = sandia_fixture(tmp_path)
    monkeypatch.setattr(stages, "MAX_SOURCE_PIXELS", 20)
    with pytest.raises(ValueError, match="scan stages"):
        export_scan(frame, max_pixels=2)


def test_cli_and_module_entry_support_sequence_and_file_and_report_failures(
    tmp_path, capsys, monkeypatch
):
    frame, _, _, _ = sandia_fixture(tmp_path, "a")
    sandia_fixture(tmp_path, "b")
    assert main(["--input", str(frame.parent.parent), "--max-pixels", "2"]) == 0
    assert len(list(frame.parent.parent.rglob("scan.sarscan"))) == 2
    assert main(["--input", str(frame)]) == 1
    assert "already exist" in capsys.readouterr().err
    assert main(["--input", str(tmp_path / "missing")]) == 1
    another, _, _, _ = sandia_fixture(tmp_path, "c")
    monkeypatch.setattr(sys, "argv", ["stages", "--input", str(another)])
    monkeypatch.delitem(sys.modules, "sar_processing.stages")
    with pytest.raises(SystemExit) as result:
        runpy.run_module("sar_processing.stages", run_name="__main__")
    assert result.value.code == 0


def test_reader_shape_and_source_allocation_guards():
    with pytest.raises(ValueError, match="row shape"):
        stages._preview(lambda first, last: np.zeros((3, 3)), (4, 4), 2, False)
    for shape in ((4,), (0, 3), (8001, 8001)):
        with pytest.raises(ValueError, match="2D grid"):
            stages._shape(shape, 512)


def test_range_preview_preserves_pulses_with_independently_bounded_range_axis():
    values = np.broadcast_to(np.arange(1, 118, dtype=np.float64)[:, None], (117, 2048))
    result = stages._preview(
        lambda first, last: values[first:last], values.shape, 512, False, preserve_aspect=False
    )
    assert result.shape == (117, 512)
    assert_allclose(result[:, 0], np.arange(1, 118))
    assert_allclose(result[:, -1], np.arange(1, 118))


def test_geographic_gotcha_without_image_manifest_reuses_both_original_payloads(tmp_path):
    frame, _, _ = gotcha_fixture(tmp_path, geographic=True)
    frame.unlink()  # Real CLI emits geocoded.sarframe instead of image.sarframe with an origin.
    final = frame.parent / "geocoded.sarframe"
    original = {
        p: p.read_bytes() for p in (frame.with_suffix(".raw"), final, final.with_suffix(".raw"))
    }
    assert export_stages(frame.parent, max_pixels=2) == 1
    scan = json.loads((frame.parent / "scan.sarscan").read_text())
    assert scan["default_stage"] == "geocoded"
    entries = {stage["id"]: stage for stage in scan["stages"]}
    assert entries["backprojection"]["manifest"] == "stage_backprojection.sarframe"
    assert entries["geocoded"]["manifest"] == "geocoded.sarframe"
    backprojection = json.loads((frame.parent / entries["backprojection"]["manifest"]).read_text())
    assert (
        backprojection["data_file"] == "image.raw"
        and backprojection["product"] == "backprojected_magnitude"
    )
    assert backprojection["display_only"] is False
    assert (backprojection["height"], backprojection["width"]) == (4, 4)
    for path, contents in original.items():
        assert path.read_bytes() == contents


def test_recursive_aperture_sequence_selects_one_scan_per_reconstruction(tmp_path):
    first, _, _ = gotcha_fixture(tmp_path / "az001", geographic=True)
    second, _, _ = gotcha_fixture(tmp_path / "az002")
    make_frame(tmp_path / "unrelated", product="scene_local_magnitude", name="image")
    assert export_stages(tmp_path, max_pixels=2) == 2
    assert (first.parent / "scan.sarscan").is_file() and (second.parent / "scan.sarscan").is_file()
    assert not (tmp_path / "unrelated/scan.sarscan").exists()
