"""Scientific frame export keeps geocoded pixels, masks, coordinates and atomicity."""

import json
import runpy
import struct
import sys

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
import pytest
import rasterio
from rasterio.control import GroundControlPoint
from rasterio.crs import CRS
from rasterio.transform import Affine

from sar_processing.geography import GeoReference, pixel_lonlat
from sar_processing.outputs import output_transaction
from sar_processing.viewer import export_sequence, geolocation_grid, main, write_geocoded_frame


def raster(path, values=None, *, nodata=np.nan, count=1, gcps=False, mask=None):
    values = np.arange(20, dtype=np.float32).reshape(4, 5) if values is None else values
    path.parent.mkdir(parents=True, exist_ok=True)
    geometry = {"crs": "EPSG:32613", "transform": Affine(1, 0, 500_000, 0, -1, 4_000_000)}
    if gcps:
        geometry = {
            "crs": "EPSG:4326",
            "gcps": [
                GroundControlPoint(row=r, col=c, x=-106 + c * 0.001, y=35 - r * 0.001, z=0)
                for r, c in ((0, 0), (0, 5), (4, 0), (4, 5))
            ],
        }
    with rasterio.open(
        path,
        "w",
        driver="GTiff",
        height=values.shape[0],
        width=values.shape[1],
        count=count,
        dtype=values.dtype,
        nodata=nodata,
        **geometry,
    ) as target:
        target.write(values, 1)
        if mask is not None:
            target.write_mask(mask)
    return path


@pytest.mark.parametrize("gcps", [False, True])
def test_frame_round_trip_preserves_geocoded_grid_and_coordinates(tmp_path, gcps):
    data = np.arange(20, dtype=np.float32).reshape(4, 5)
    data[1, 2] = np.nan
    source = raster(tmp_path / "source.tif", data, gcps=gcps)
    with output_transaction(tmp_path / "frame") as work:
        result = write_geocoded_frame(source, work)
    frame = tmp_path / "frame"
    manifest = json.loads((frame / "geocoded.sarframe").read_text())
    assert manifest["schema"] == "sar-scientific-frame-v1"
    assert manifest["source"] == str(source.resolve())
    assert result["valid_pixels"] == 19
    payload = (frame / manifest["data_file"]).read_bytes()
    assert struct.unpack("<II", payload[:8]) == (5, 4)
    assert_array_equal(np.frombuffer(payload[8:], dtype="<f4").reshape(4, 5), data)
    assert manifest["geolocation"]["interpolation_error_m"] < 0.001
    assert "absolute accuracy unknown" in manifest["geolocation"]["error_scope"]
    if not gcps:
        assert manifest["crs"] == "EPSG:32613"
        assert_allclose(manifest["geolocation"]["longitude"][0][0], -105 + 5.55e-6, atol=1e-7)


def test_explicit_validity_mask_is_exported_as_nan_and_source_label_is_stable(tmp_path):
    mask = np.ones((4, 5), dtype=np.uint8) * 255
    mask[2, 3] = 0
    source = raster(tmp_path / "source.tif", mask=mask)
    with output_transaction(tmp_path / "frame") as work:
        result = write_geocoded_frame(source, work, source_label="final/output.tif")
    assert result["source"] == "final/output.tif"
    values = np.fromfile(tmp_path / "frame/geocoded.raw", dtype="<f4", offset=8).reshape(4, 5)
    assert np.isnan(values[2, 3])
    assert values[0, 0] == 0


def test_large_grid_is_bounded_and_cell_residual_does_not_claim_source_accuracy():
    reference = GeoReference(CRS.from_epsg(32613), Affine(2, 0, 500_000, 0, -2, 4_000_000))
    grid = geolocation_grid(reference, 2000, 3000)
    assert len(grid["rows"]) == len(grid["cols"]) == 33
    assert grid["rows"][-1] == 1999
    assert grid["cols"][-1] == 2999
    expected = pixel_lonlat(reference, np.array([0.0, 1999.0]), np.array([0.0, 2999.0]))
    assert_allclose([grid["longitude"][0][0], grid["latitude"][0][0]], expected[0])
    assert_allclose([grid["longitude"][-1][-1], grid["latitude"][-1][-1]], expected[1])
    assert 0 <= grid["interpolation_error_m"] < 0.01


def test_date_line_mapping_uses_short_longitude_distance():
    reference = GeoReference(CRS.from_epsg(32660), Affine(300, 0, 790_000, 0, -300, 10_000))
    grid = geolocation_grid(reference, 40, 500)
    assert min(np.asarray(grid["longitude"]).ravel()) < -179
    assert max(np.asarray(grid["longitude"]).ravel()) > 179
    assert grid["interpolation_error_m"] < 1


@pytest.mark.parametrize("shape", [(1, 3), (3, 1), (8001, 8001)])
def test_grid_rejects_invalid_dimensions_before_allocating(shape):
    with pytest.raises(ValueError, match="2x2"):
        geolocation_grid(GeoReference(CRS.from_epsg(32613), Affine.identity()), *shape)


@pytest.mark.parametrize("value", [-1.0, np.inf, 1e40, np.nan])
def test_invalid_unmasked_magnitudes_roll_back_the_entire_export(tmp_path, value):
    values = np.ones((4, 5), dtype=np.float64)
    values[1, 1] = value
    source = raster(tmp_path / "source.tif", values, nodata=None)
    with pytest.raises(ValueError, match="magnitude"):
        export_sequence(source, tmp_path / "out")
    assert not (tmp_path / "out").exists()
    assert not list(tmp_path.glob(".out-*"))


def test_all_masked_raster_does_not_publish_a_blank_frame(tmp_path):
    source = raster(tmp_path / "source.tif", np.full((4, 5), np.nan, np.float32))
    with pytest.raises(ValueError, match="no valid"):
        export_sequence(source, tmp_path / "out")
    assert not (tmp_path / "out").exists()


@pytest.mark.parametrize("complex_data,count", [(True, 1), (False, 2)])
def test_wrong_raster_contract_is_rejected(tmp_path, complex_data, count):
    values = np.ones((4, 5), dtype=np.complex64 if complex_data else np.float32)
    source = raster(tmp_path / "source.tif", values, count=count, nodata=None)
    with pytest.raises(ValueError, match="single real"):
        export_sequence(source, tmp_path / "out")


@pytest.mark.parametrize("name", ["geocoded.raw", "geocoded.sarframe"])
def test_existing_frame_products_are_never_overwritten(tmp_path, name):
    target = tmp_path / name
    target.write_bytes(b"keep")
    with pytest.raises(FileExistsError):
        write_geocoded_frame(tmp_path / "absent.tif", tmp_path)
    assert target.read_bytes() == b"keep"


def test_sequence_orders_distinct_frames_and_fails_cleanly_on_missing_input(tmp_path):
    for name in ("b", "a"):
        raster(tmp_path / "source" / name / "magnitude_geocoded.tif")
    out = tmp_path / "frames"
    assert export_sequence(tmp_path / "source", out) == 2
    manifest = json.loads((out / "sequence.json").read_text())
    assert [f["directory"] for f in manifest["frames"]] == ["0001_a", "0002_b"]
    with pytest.raises(FileExistsError):
        export_sequence(tmp_path / "source", out)
    with pytest.raises(ValueError, match="No magnitude"):
        export_sequence(tmp_path / "missing", tmp_path / "missing-out")


def test_cli_success_failure_and_module_entry(tmp_path, capsys, monkeypatch):
    source = raster(tmp_path / "source.tif")
    argv = ["--input", str(source), "--output", str(tmp_path / "out")]
    assert main(argv) == 0
    assert main(argv) == 1
    assert "already exists" in capsys.readouterr().err
    monkeypatch.setattr(sys, "argv", ["sar-viewer-export", *argv[:-1], str(tmp_path / "out2")])
    monkeypatch.delitem(sys.modules, "sar_processing.viewer")
    with pytest.raises(SystemExit) as result:
        runpy.run_module("sar_processing.viewer", run_name="__main__")
    assert result.value.code == 0


def test_streamed_export_preserves_partial_last_tile(tmp_path):
    values = np.arange(515 * 3, dtype=np.float32).reshape(515, 3)
    source = raster(tmp_path / "source.tif", values)
    with output_transaction(tmp_path / "out") as work:
        write_geocoded_frame(source, work)
    assert_array_equal(
        np.fromfile(tmp_path / "out/geocoded.raw", dtype="<f4", offset=8), values.ravel()
    )
