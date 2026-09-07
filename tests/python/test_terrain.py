"""Terrain intersection closure, pixel conventions, and datum/coverage failures."""

from contextlib import nullcontext
from types import SimpleNamespace

import numpy as np
from numpy.testing import assert_allclose
import pytest
import rasterio
from rasterio.transform import Affine, from_origin
from rasterio.warp import transform

from sar_processing.geography import SceneOrigin, scene_lonlat
from sar_processing.model import ImageGrid
from sar_processing.terrain import _sample_dem, _validate_dem, scene_height_grid


def write_dem(
    tmp_path,
    values=None,
    *,
    crs="EPSG:4326",
    affine=None,
    unit="m",
    datum="ellipsoid",
    nodata=None,
    dtype="float64",
    count=1,
):
    values = np.full((100, 100), 100.0) if values is None else np.asarray(values)
    path = tmp_path / "dem.tif"
    with rasterio.open(
        path,
        "w",
        driver="GTiff",
        width=values.shape[1],
        height=values.shape[0],
        count=count,
        dtype=dtype,
        crs=crs,
        transform=affine or from_origin(-0.02, 0.02, 0.0004, 0.0004),
        nodata=nodata,
    ) as out:
        out.write(values.astype(dtype), 1)
        if unit is not None:
            out.set_band_unit(1, unit)
        if datum is not None:
            out.update_tags(vertical_datum=datum)
    return path


def grid():
    return ImageGrid(np.array([-1000.0, 0.0, 1000.0]), np.array([-1.0, 0.0, 1.0]))


def world_for(grid, origin, z):
    xx, yy = np.meshgrid(grid.x_m, grid.y_m)
    return scene_lonlat(np.column_stack([xx.ravel(), yy.ravel(), z.ravel()]), origin)


def test_constant_ellipsoid_height_accounts_for_earth_curvature_and_preserves_grid(tmp_path):
    origin = SceneOrigin(0, 0)
    g = grid()
    heights = scene_height_grid(g, origin, write_dem(tmp_path))
    # Independent equatorial ECEF circle: heading90 makes X precisely east.
    expected = np.sqrt((6378137.0 + 100.0) ** 2 - g.x_m**2) - 6378137.0
    assert_allclose(heights[1], expected, atol=1e-6)
    assert heights[1, 0] < 99.93  # A simple DEM-minus-origin calculation is wrong.
    assert_allclose(world_for(g, origin, heights)[:, 2], 100, atol=0.0001)
    assert g.z_m == 0
    assert heights.shape == (3, 3)


@pytest.mark.parametrize("heading", [0, 35, 90, 180, 270])
def test_sloped_terrain_closes_in_geodetic_height_with_rotated_scene_heading(tmp_path, heading):
    affine = from_origin(-106.52, 35.22, 0.0004, 0.0004)
    row, col = np.mgrid[:100, :100]
    lon, lat = affine * (col + 0.5, row + 0.5)
    values = 1725 + 5000 * (lon + 106.5) + 7000 * (lat - 35.2)
    path = write_dem(tmp_path, values, affine=affine)
    origin = SceneOrigin(35.2, -106.5, 1700, heading)
    g = ImageGrid(np.linspace(-400, 400, 5), np.linspace(-400, 400, 5), 15)
    heights = scene_height_grid(g, origin, path)
    world = world_for(g, origin, heights)
    expected = 1725 + 5000 * (world[:, 0] + 106.5) + 7000 * (world[:, 1] - 35.2)
    assert_allclose(world[:, 2], expected, atol=0.0001)
    assert np.ptp(heights) > 50
    assert g.z_m == 15


def test_projected_dem_and_large_origin_height(tmp_path):
    origin = SceneOrigin(35.2, -106.5, 2000)
    east, north = transform("EPSG:4326", "EPSG:32613", [-106.5], [35.2])
    path = write_dem(
        tmp_path,
        np.full((100, 100), 3000),
        crs="EPSG:32613",
        affine=from_origin(east[0] - 2000, north[0] + 2000, 40, 40),
    )
    heights = scene_height_grid(grid(), origin, path)
    assert_allclose(world_for(grid(), origin, heights)[:, 2], 3000, atol=0.0001)
    assert abs(heights[1, 1] - 1000) < 1e-6


@pytest.mark.parametrize("unit", ["m", "metre", "metres", "meters", "M"])
def test_explicit_metre_unit_aliases_and_absent_optional_datum_tag(tmp_path, unit):
    result = scene_height_grid(
        grid(), SceneOrigin(0, 0), write_dem(tmp_path, unit=unit, datum=None)
    )
    assert_allclose(result[1, 1], 100, atol=1e-6)


def test_bounded_batches_agree_with_full_grid(tmp_path, monkeypatch):
    path = write_dem(tmp_path)
    expected = scene_height_grid(grid(), SceneOrigin(0, 0), path)
    monkeypatch.setattr("sar_processing.terrain.POINTS_PER_BATCH", 2)
    actual = scene_height_grid(grid(), SceneOrigin(0, 0), path)
    assert_allclose(actual, expected, atol=1e-8)


def test_bilinear_pixel_centres_rotation_and_scaled_integer_height(tmp_path):
    affine = Affine.translation(-1, 1) * Affine.rotation(10) * Affine.scale(0.1, -0.1)
    path = write_dem(tmp_path, np.arange(12).reshape(3, 4), dtype="int16", affine=affine)
    with rasterio.open(path, "r+") as src:
        src.scales = (2,)
        src.offsets = (10,)
    cols, rows = np.array([0.5, 1.75, 3.5]), np.array([0.5, 1.0, 2.5])
    lon, lat = affine * (cols, rows)
    with rasterio.open(path) as src:
        values = _sample_dem(src, np.column_stack([lon, lat]))
    assert_allclose(values, [10, 16.5, 32], atol=1e-12)


def test_missing_zero_weight_neighbours_do_not_invalidate_exact_pixel_centre(tmp_path):
    values = np.full((3, 3), -9999)
    values[2, 2] = 25
    affine = from_origin(0, 3, 1, 1)
    path = write_dem(tmp_path, values, nodata=-9999, affine=affine)
    with rasterio.open(path) as src:
        actual = _sample_dem(src, np.array([[2.5, 0.5]]))
        assert_allclose(actual, [25])
        with pytest.raises(ValueError, match="missing"):
            _sample_dem(src, np.array([[2.499, 0.501]]))


@pytest.mark.parametrize("unit", [None, "", "ft", "degrees"])
def test_missing_or_wrong_height_units_are_rejected(tmp_path, unit):
    with pytest.raises(ValueError, match="height units"):
        scene_height_grid(grid(), SceneOrigin(0, 0), write_dem(tmp_path, unit=unit))


@pytest.mark.parametrize("datum", ["MSL", "EGM96", "orthometric", "unknown"])
def test_unsupported_declared_vertical_datum_is_rejected(tmp_path, datum):
    with pytest.raises(ValueError, match="vertical datum"):
        scene_height_grid(grid(), SceneOrigin(0, 0), write_dem(tmp_path, datum=datum))


def test_band_datum_cannot_override_a_conflicting_dataset_datum(tmp_path):
    path = write_dem(tmp_path)
    with rasterio.open(path, "r+") as src:
        src.update_tags(1, HEIGHT_REFERENCE="MSL")
    with pytest.raises(ValueError, match="vertical datum"):
        scene_height_grid(grid(), SceneOrigin(0, 0), path)


@pytest.mark.parametrize("datum", ["MSL", "egm96", "", "unknown"])
def test_api_requires_explicit_ellipsoid_semantics(tmp_path, datum):
    with pytest.raises(ValueError, match="explicitly ellipsoidal"):
        scene_height_grid(
            grid(), SceneOrigin(0, 0), tmp_path / "absent.tif", height_reference=datum
        )


@pytest.mark.parametrize(
    "changes,match",
    [
        ({"count": 2}, "single-band"),
        ({"values": np.ones((1, 3))}, "2 by 2"),
        ({"crs": None}, "horizontal CRS"),
        ({"crs": "EPSG:4978"}, "horizontal CRS"),
        ({"dtype": "complex64"}, "real numeric"),
    ],
)
def test_invalid_dem_raster_contract(tmp_path, changes, match):
    with pytest.raises(ValueError, match=match):
        scene_height_grid(grid(), SceneOrigin(0, 0), write_dem(tmp_path, **changes))


@pytest.mark.parametrize("height", [np.nan, np.inf, -12001, 100001])
def test_invalid_or_unphysical_dem_heights(tmp_path, height):
    with pytest.raises(ValueError, match="heights"):
        scene_height_grid(
            grid(), SceneOrigin(0, 0), write_dem(tmp_path, np.full((100, 100), height))
        )


def test_masked_dem_sample_rejects_the_entire_requested_grid(tmp_path):
    path = write_dem(tmp_path)
    with rasterio.open(path, "r+") as src:
        valid = np.full((100, 100), 255, dtype=np.uint8)
        valid[49, 49] = 0
        src.write_mask(valid)
    with pytest.raises(ValueError, match="missing"):
        scene_height_grid(grid(), SceneOrigin(0, 0), path)


@pytest.mark.parametrize(
    "origin", [SceneOrigin(1, 0), SceneOrigin(0, 1), SceneOrigin(-1, 0), SceneOrigin(0, -1)]
)
def test_outside_dem_is_not_extrapolated(tmp_path, origin):
    with pytest.raises(ValueError, match="coverage"):
        scene_height_grid(grid(), origin, write_dem(tmp_path))


def test_excessive_support_window_is_rejected_before_reading(tmp_path, monkeypatch):
    monkeypatch.setattr("sar_processing.terrain.MAX_DEM_WINDOW_CELLS", 4)
    with pytest.raises(ValueError, match="supporting window"):
        scene_height_grid(grid(), SceneOrigin(0, 0), write_dem(tmp_path))


def test_nonconverging_dem_intersection_fails_instead_of_returning_approximate_heights(
    tmp_path, monkeypatch
):
    calls = []

    def oscillating_dem(source, lonlat):
        calls.append(1)
        return np.full(len(lonlat), 100 if len(calls) % 2 else 200)

    monkeypatch.setattr("sar_processing.terrain._sample_dem", oscillating_dem)
    with pytest.raises(ValueError, match="failed to converge"):
        scene_height_grid(grid(), SceneOrigin(0, 0), write_dem(tmp_path))
    assert len(calls) == 12


def metadata_source(**updates):
    attrs = {
        "count": 1,
        "width": 2,
        "height": 2,
        "crs": rasterio.crs.CRS.from_epsg(4326),
        "transform": from_origin(0, 2, 1, 1),
        "gcps": ([], None),
        "dtypes": ("float64",),
        "units": ("m",),
        "tags": lambda *args: {},
        "scales": (1,),
        "offsets": (0,),
    }
    attrs.update(updates)
    return SimpleNamespace(**attrs)


@pytest.mark.parametrize(
    "changes,match",
    [
        ({"transform": Affine(0, 0, 0, 0, 0, 0)}, "affine"),
        ({"transform": Affine(np.nan, 0, 0, 0, 1, 0)}, "affine"),
        ({"gcps": ([1], None)}, "GCP"),
        ({"scales": (0,)}, "scale"),
        ({"scales": (np.nan,)}, "scale"),
        ({"offsets": (np.inf,)}, "offset"),
    ],
)
def test_invalid_metadata_not_representable_as_geotiff(changes, match):
    with pytest.raises(ValueError, match=match):
        _validate_dem(metadata_source(**changes))


def test_nonfinite_reprojected_coordinates_fail_before_index_cast(tmp_path, monkeypatch):
    monkeypatch.setattr("sar_processing.terrain.transform", lambda *args: ([np.nan], [0]))
    with rasterio.open(write_dem(tmp_path)) as source:
        with pytest.raises(ValueError, match="coverage"):
            _sample_dem(source, np.array([[0.0, 0.0]]))


def test_per_pixel_height_initialization_is_not_mutated(tmp_path):
    initial = np.arange(9, dtype=float).reshape(3, 3)
    expected = initial.copy()
    g = ImageGrid(grid().x_m, grid().y_m, initial)
    heights = scene_height_grid(g, SceneOrigin(0, 0), write_dem(tmp_path))
    assert_allclose(initial, expected)
    assert_allclose(world_for(g, SceneOrigin(0, 0), heights)[:, 2], 100, atol=0.0001)


def test_no_read_is_attempted_after_invalid_dem_metadata(tmp_path, monkeypatch):
    source = metadata_source(units=("feet",))
    monkeypatch.setattr("sar_processing.terrain.rasterio.open", lambda *args: nullcontext(source))
    with pytest.raises(ValueError, match="metres"):
        scene_height_grid(grid(), SceneOrigin(0, 0), tmp_path / "anything.tif")
