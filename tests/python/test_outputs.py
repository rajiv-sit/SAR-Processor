"""Output round trips and failure atomicity using real files and raster drivers."""

import json
import os
from pathlib import Path
import struct
import subprocess

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
from PIL import Image
import pytest
import rasterio
from rasterio.crs import CRS
from rasterio.transform import Affine

from sar_processing.geography import GeoReference
from sar_processing.model import ImageGrid
from sar_processing.outputs import (
    checked_float32,
    output_transaction,
    write_formed_image,
    write_json,
    write_preview,
    write_raw,
)


def image_and_grid():
    image = np.array([[1 + 2j, -3 + 4j, 0j], [2 - 1j, -4 - 3j, 1e-6j]])
    grid = ImageGrid(np.array([-1.0, 0.0, 1.0]), np.array([-0.5, 0.5]))
    return image, grid


def reference():
    return GeoReference(CRS.from_epsg(32613), Affine(1, 0, 500_000, 0, -1, 4_000_000))


def test_output_transaction_publishes_only_after_all_files_are_complete(tmp_path):
    output = tmp_path / "parent" / "run"
    with output_transaction(output) as work:
        assert work.parent == output.parent
        assert not output.exists()
        write_json(work / "report.json", {"stage": "complete"})
        (work / "data.bin").write_bytes(b"payload")
        assert not output.exists()
    assert json.loads((output / "report.json").read_text()) == {"stage": "complete"}
    assert (output / "data.bin").read_bytes() == b"payload"
    assert list(output.parent.iterdir()) == [output]


@pytest.mark.skipif(os.name != "nt", reason="Windows directory ACL inheritance regression")
def test_published_output_inherits_parent_access_instead_of_owner_only_acl(tmp_path):
    output = tmp_path / "shared-output"
    with output_transaction(output) as work:
        (work / "frame.raw").write_bytes(b"viewer data")
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            "(Get-Acl -LiteralPath $env:SAR_TEST_ACL_DIR).AreAccessRulesProtected",
        ],
        env={**os.environ, "SAR_TEST_ACL_DIR": str(output)},
        capture_output=True,
        text=True,
        check=True,
    )
    assert result.stdout.strip() == "False", "Viewer outputs must inherit the parent's ACL"


@pytest.mark.parametrize("directory", [False, True])
def test_output_transaction_refuses_existing_target_without_modification(tmp_path, directory):
    target = tmp_path / "existing"
    if directory:
        target.mkdir()
        original = target / "keep.bin"
    else:
        original = target
    original.write_bytes(b"keep this data")
    with pytest.raises(FileExistsError, match="Output already exists"):
        with output_transaction(target):
            pytest.fail("An existing target must never be opened for output")
    assert original.read_bytes() == b"keep this data"
    assert list(tmp_path.iterdir()) == [target]


@pytest.mark.parametrize("failure", [OSError("write failed"), ValueError("invalid product")])
def test_output_transaction_rolls_back_partial_files_on_failure(tmp_path, failure):
    target = tmp_path / "run"
    with pytest.raises(type(failure), match=str(failure)):
        with output_transaction(target) as work:
            (work / "partial.bin").write_bytes(b"partial")
            raise failure
    assert not target.exists()
    assert list(tmp_path.iterdir()) == []


def test_output_transaction_propagates_commit_failure_and_cleans_temporary_files(
    tmp_path, monkeypatch
):
    def refuse_commit(self, target):
        raise OSError("rename refused")

    monkeypatch.setattr(Path, "rename", refuse_commit)
    target = tmp_path / "run"
    with pytest.raises(OSError, match="rename refused"):
        with output_transaction(target) as work:
            (work / "complete.bin").write_bytes(b"done")
    assert not target.exists()
    assert list(tmp_path.iterdir()) == []


def test_output_transaction_rejects_a_file_as_parent(tmp_path):
    parent = tmp_path / "file"
    parent.write_bytes(b"unchanged")
    with pytest.raises(OSError):
        with output_transaction(parent / "run"):
            pytest.fail("A file cannot contain a run directory")
    assert parent.read_bytes() == b"unchanged"


def test_json_preserves_structure_and_rejects_nonstandard_nonfinite_numbers(tmp_path):
    path = tmp_path / "report.json"
    expected = {"stage": "focused", "measurements": [1.5, 0.0], "units": "m"}
    write_json(path, expected)
    assert json.loads(path.read_text(encoding="utf-8")) == expected
    assert path.read_text().endswith("\n")
    for value in [np.nan, np.inf, -np.inf]:
        with pytest.raises(ValueError):
            write_json(path, {"metric": value})
        assert json.loads(path.read_text()) == expected


@pytest.mark.parametrize("complex_values", [False, True])
def test_checked_float32_preserves_dtype_and_finite_boundary_values(complex_values):
    limit = np.finfo(np.float32).max
    values = np.array([0.0, 1e-40, limit, -limit])
    if complex_values:
        values = values + 1j * values[::-1]
    result = checked_float32(values)
    assert result.dtype == (np.complex64 if complex_values else np.float32)
    assert np.isfinite(result).all()
    assert_allclose(result, values, rtol=1e-6, atol=1e-45)


@pytest.mark.parametrize(
    "values",
    [
        np.array([1e40]),
        np.array([-1e40]),
        np.array([1e40 + 1j]),
        np.array([1 + 1e40j]),
        np.array([np.nan]),
        np.array([np.inf]),
        np.array([complex(1, np.nan)]),
    ],
)
def test_checked_float32_rejects_overflow_and_nonfinite_values(values):
    with pytest.raises(ValueError, match="finite|float32"):
        checked_float32(values)


@pytest.mark.parametrize("writer", [write_raw, write_preview])
@pytest.mark.parametrize(
    "values",
    [
        np.zeros(3),
        np.zeros((0, 2)),
        np.array([[np.nan]]),
        np.array([[np.inf]]),
        np.array([[-1.0]]),
        np.array([[1 + 0j]]),
    ],
)
def test_magnitude_outputs_reject_invalid_data_before_creating_files(tmp_path, writer, values):
    target = tmp_path / "invalid"
    with pytest.raises(ValueError, match="Magnitude"):
        writer(target, values)
    assert not target.exists()


def test_raw_is_little_endian_float32_with_row_major_pixels(tmp_path):
    magnitude = np.array([[0.0, 1.25, 2.5], [3.75, 5.0, 6.25]])
    target = tmp_path / "image.raw"
    write_raw(target, magnitude)
    payload = target.read_bytes()
    assert struct.unpack("<II", payload[:8]) == (3, 2)
    assert_array_equal(np.frombuffer(payload[8:], dtype="<f4").reshape(2, 3), magnitude)


def test_raw_rejects_finite_float32_overflow_without_truncating_existing_output(tmp_path):
    target = tmp_path / "image.raw"
    target.write_bytes(b"keep")
    with pytest.raises(ValueError, match="float32"):
        write_raw(target, np.array([[1e40]]))
    assert target.read_bytes() == b"keep"


def test_preview_uses_fifty_db_amplitude_range_and_keeps_tiny_signal_visible(tmp_path):
    target = tmp_path / "image.png"
    values = np.array([[1.0, 0.1, 10 ** (-50 / 20), 0.0]])
    write_preview(target, values)
    with Image.open(target) as image:
        expected = np.asarray(image).copy()
        assert image.mode == "L"
        assert_array_equal(expected, [[255, 153, 0, 0]])
    write_preview(target, values * 1e-40)
    with Image.open(target) as image:
        assert_array_equal(np.asarray(image), expected)
    write_preview(target, np.zeros((2, 2)))
    with Image.open(target) as image:
        assert_array_equal(np.asarray(image), np.zeros((2, 2), dtype=np.uint8))


def test_preview_bounds_longest_dimension(tmp_path):
    target = tmp_path / "image.png"
    write_preview(target, np.ones((2, 3201)))
    with Image.open(target) as image:
        assert max(image.size) <= 1600
        assert np.asarray(image).min() == 255


def test_preview_reduction_retains_features_between_old_subsample_locations(tmp_path):
    values = np.zeros((4, 3200))
    values[:, 1::2] = 1
    path = tmp_path / "image.png"
    write_preview(path, values)
    with Image.open(path) as image:
        assert image.size == (1600, 2)
        assert np.asarray(image).min() > 230


@pytest.mark.parametrize("dynamic_range", [0, 121, np.nan, np.inf])
def test_preview_rejects_invalid_display_range(tmp_path, dynamic_range):
    with pytest.raises(ValueError, match="dynamic range"):
        write_preview(tmp_path / "image.png", np.ones((2, 2)), dynamic_range)


def test_adjustable_preview_range_preserves_input_and_changes_only_display(tmp_path):
    data = np.array([[1.0, 0.01]])
    original = data.copy()
    write_preview(tmp_path / "image.png", data, 40)
    with Image.open(tmp_path / "image.png") as image:
        assert_array_equal(np.asarray(image), [[255, 0]])
    assert_array_equal(data, original)


@pytest.mark.parametrize("geographic", [False, True])
def test_formed_image_preserves_complex_values_axes_and_separate_display_products(
    tmp_path, geographic
):
    image, grid = image_and_grid()
    mapping = reference() if geographic else None
    with output_transaction(tmp_path / "run") as work:
        write_formed_image(work, image, grid, mapping)
    output = tmp_path / "run"
    with np.load(output / "complex_image.npz", allow_pickle=False) as saved:
        assert_array_equal(saved["image"], image)
        assert saved["image"].dtype == np.complex128
        assert_array_equal(saved["x_m"], grid.x_m)
        assert_array_equal(saved["y_m"], grid.y_m)
        assert saved["z_m"].item() == grid.z_m
        assert saved["stage"].item() == "complex_image"
    raw = (output / "image.raw").read_bytes()
    assert struct.unpack("<II", raw[:8]) == (3, 2)
    assert_allclose(np.frombuffer(raw[8:], dtype="<f4").reshape(2, 3), np.abs(image))
    with Image.open(output / "image.png") as preview:
        assert preview.size == (3, 2)
    if geographic:
        for name, expected in [
            ("complex_native.tif", image),
            ("magnitude_native.tif", np.abs(image)),
        ]:
            with rasterio.open(output / name) as raster:
                assert raster.crs == mapping.crs
                assert raster.transform == mapping.affine
                assert_allclose(raster.read(1), expected, rtol=1e-6)
                assert np.isfinite(raster.read(1)).all()
    else:
        assert not list(output.glob("*.tif"))


@pytest.mark.parametrize(
    "bad_image",
    [
        np.ones((2, 2), complex),
        np.ones((2, 3)),
        np.full((2, 3), complex(np.nan, 0)),
        np.full((2, 3), 1e40 + 0j),
        np.full((2, 3), 1 + 1e40j),
        np.full((2, 3), 3e38 + 3e38j),
    ],
)
def test_invalid_or_unrepresentable_formed_images_create_no_files(tmp_path, bad_image):
    _, grid = image_and_grid()
    with pytest.raises(ValueError):
        write_formed_image(tmp_path, bad_image, grid, reference())
    assert list(tmp_path.iterdir()) == []


def test_invalid_georeference_is_rejected_before_writing_products(tmp_path):
    image, grid = image_and_grid()
    with pytest.raises(ValueError, match="Exactly one"):
        write_formed_image(tmp_path, image, grid, GeoReference(CRS.from_epsg(4326)))
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("failed_file", ["image.raw", "image.png", "complex_native.tif"])
def test_real_output_failure_rolls_back_every_product(tmp_path, failed_file):
    image, grid = image_and_grid()
    final = tmp_path / "run"
    with pytest.raises((OSError, rasterio.errors.RasterioError)):
        with output_transaction(final) as work:
            (work / failed_file).mkdir()
            write_formed_image(work, image, grid, reference())
    assert not final.exists()
    assert list(tmp_path.iterdir()) == []
