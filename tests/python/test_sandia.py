"""Real GDAL NITF fixtures exercise complex import and geographic output."""

from pathlib import Path
from types import SimpleNamespace

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
import pytest
import rasterio
from rasterio.transform import from_origin

from sar_processing.sandia import decode_magnitude_phase, process_sandia, validate_sandia


def make_nitf(path: Path, *, bands=2, dtype="uint16", order="M,P", abpp=None):
    values = np.arange(1, 21, dtype=dtype).reshape(4, 5)
    with rasterio.open(
        path,
        "w",
        driver="NITF",
        width=5,
        height=4,
        count=bands,
        dtype=dtype,
        crs="EPSG:4326",
        transform=from_origin(-106.5, 35.2, 0.002, 0.002),
        ICORDS="D",
        IREP="MULTI",
        ICAT="SAR",
        ISUBCAT=order,
        **({"ABPP": abpp} if abpp is not None else {}),
    ) as dst:
        dst.write(values, 1)
        if bands == 2:
            dst.write(np.full((4, 5), min(16384, np.iinfo(dtype).max), dtype=dtype), 2)
    return values


def test_phase_quadrants_max_code_and_unsigned_overflow():
    magnitude = np.full((1, 5), 65535, np.uint16)
    codes = np.array([[0, 16384, 32768, 49152, 65535]], np.uint16)
    expected = 65535 * np.exp(1j * codes.astype(float) * (2 * np.pi / 65536))
    actual = decode_magnitude_phase(magnitude, codes)
    assert actual.dtype == np.complex64
    assert_allclose(actual, expected, rtol=1e-7, atol=1e-7)
    assert_allclose(np.abs(actual), magnitude, rtol=1e-7)
    assert_allclose(decode_magnitude_phase(magnitude.astype(">u2"), codes.astype(">u2")), actual)


@pytest.mark.parametrize(
    "magnitude,phase",
    [
        (np.ones((2, 2), np.uint16), np.ones((1, 2), np.uint16)),
        (np.ones((2, 2), np.int16), np.ones((2, 2), np.uint16)),
        (np.ones((2, 2), np.uint16), np.ones((2, 2), float)),
        (np.ones((2, 2), np.uint8), np.ones((2, 2), np.uint16)),
        (np.ones((2, 2), np.uint16), np.ones((2, 2), np.uint32)),
    ],
)
def test_invalid_band_shapes_or_encodings(magnitude, phase):
    with pytest.raises(ValueError):
        decode_magnitude_phase(magnitude, phase)


def test_real_nitf_round_trip_complex_values_geography_and_viewer(tmp_path):
    source_path = tmp_path / "sample.ntf"
    expected = make_nitf(source_path)
    work = tmp_path / "processed"
    work.mkdir()
    report = process_sandia(source_path, work, 100)
    with rasterio.open(source_path) as source, rasterio.open(work / "complex_native.tif") as iq:
        validate_sandia(source_path, source)
        assert iq.transform == source.transform
        assert iq.crs == source.crs
        assert_allclose(iq.read(1), 1j * expected, atol=1e-6)
        assert iq.tags()["stage"] == "focused_complex_image"
    with rasterio.open(work / "magnitude_native.tif") as magnitude:
        assert_array_equal(magnitude.read(1), expected)
    with rasterio.open(work / "magnitude_geocoded.tif") as geo:
        assert geo.crs == rasterio.crs.CRS.from_epsg(32613)
        assert geo.res == (100, 100)
        assert geo.read(1, masked=True).count() > 0
        assert np.isnan(geo.nodata)
    raw = (work / "image.raw").read_bytes()
    assert_array_equal(np.frombuffer(raw[:8], "<u4"), [5, 4])
    assert_array_equal(np.frombuffer(raw[8:], "<f4").reshape(4, 5), expected)
    assert (work / "image.png").read_bytes().startswith(b"\x89PNG")
    assert report["input_stage"] == "focused_complex_image"
    assert report["range_compression"] == "already performed by data producer"
    assert report["image_shape"] == [4, 5]
    assert report["source"]["bytes"] == source_path.stat().st_size
    assert len(report["native_pixel_coordinates_lon_lat"]) == 5


@pytest.mark.parametrize(
    "changes",
    [
        {"bands": 1, "order": "M"},
        {"dtype": "uint8"},
        {"order": "P,M"},
        {"abpp": 12},
    ],
)
def test_actual_wrong_nitf_band_contract(tmp_path, changes):
    path = tmp_path / "bad.ntf"
    make_nitf(path, **changes)
    with rasterio.open(path) as source, pytest.raises(ValueError):
        validate_sandia(path, source)


@pytest.mark.parametrize("content", [b"", b"NITF02.10", b"X" * 360])
def test_incomplete_or_non_nitf_header(tmp_path, content):
    path = tmp_path / "bad.ntf"
    path.write_bytes(content)
    with pytest.raises(ValueError, match="complete NITF"):
        validate_sandia(path, None)


def test_truncated_nitf_is_rejected_before_reading_samples(tmp_path):
    path = tmp_path / "truncated.ntf"
    make_nitf(path)
    path.write_bytes(path.read_bytes()[:-10])
    with pytest.raises(ValueError, match="truncated"):
        validate_sandia(path, None)


def metadata_source(**overrides):
    # Metadata variants not emitted by GDAL's public creation API.
    attrs = dict(
        driver="NITF", count=2, subdatasets=[], dtypes=("uint16", "uint16"), width=5, height=4
    )
    attrs.update(overrides)
    return SimpleNamespace(**attrs)


@pytest.mark.parametrize(
    "changes",
    [
        {"driver": "GTiff"},
        {"subdatasets": ["image2"]},
        {"width": 10000, "height": 10000},
        {"width": 0},
    ],
)
def test_unsupported_dataset_layout(tmp_path, changes):
    path = tmp_path / "source.ntf"
    make_nitf(path)
    with pytest.raises(ValueError):
        validate_sandia(path, metadata_source(**changes))


@pytest.mark.parametrize(
    "tre,rows,message",
    [
        ({"CMETAA": "unsupported mapped amplitude"}, "4", "CMETAA"),
        ({}, "3", "complete image"),
    ],
)
def test_unimplemented_mapping_and_partial_geolocation_rejected(tmp_path, tre, rows, message):
    path = tmp_path / "source.ntf"
    make_nitf(path)

    def tags(band=0, ns=None):
        if ns == "TRE":
            return tre
        if band:
            return {"NITF_ISUBCAT": "M" if band == 1 else "P"}
        return {"NITF_BLOCKA_L_LINES_01": rows, "NITF_ABPP": "16"}

    with pytest.raises(ValueError, match=message):
        validate_sandia(path, metadata_source(tags=tags))


def test_full_height_blocka_metadata_is_accepted(tmp_path):
    path = tmp_path / "source.ntf"
    make_nitf(path)

    def tags(band=0, ns=None):
        if ns == "TRE":
            return {}
        if band:
            return {"NITF_ISUBCAT": "M" if band == 1 else "P"}
        return {"NITF_BLOCKA_L_LINES_01": "4", "NITF_ABPP": "16"}

    validate_sandia(path, metadata_source(tags=tags))


def test_optional_viewer_export_saves_space_without_losing_complex_or_magnitude(tmp_path):
    path = tmp_path / "source.ntf"
    expected = make_nitf(path)
    output = tmp_path / "output"
    output.mkdir()
    report = process_sandia(path, output, 100, write_viewer_raw=False)
    assert report["viewer_raw_written"] is False
    assert not (output / "image.raw").exists()
    assert (output / "image.png").exists()
    with rasterio.open(output / "magnitude_native.tif") as magnitude:
        assert_array_equal(magnitude.read(1), expected)
    with rasterio.open(output / "complex_native.tif") as iq:
        assert_allclose(iq.read(1), 1j * expected, atol=1e-6)
