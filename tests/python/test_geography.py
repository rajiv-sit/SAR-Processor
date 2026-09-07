"""Geographic tests using known WGS84 geometry and actual GDAL raster warps."""

from types import SimpleNamespace

import numpy as np
import pytest
import rasterio
from numpy.testing import assert_allclose, assert_array_equal
from rasterio.control import GroundControlPoint
from rasterio.crs import CRS
from rasterio.transform import Affine
from rasterio.enums import Resampling
from rasterio.warp import reproject

from sar_processing.geography import (
    GeoReference,
    SceneOrigin,
    dataset_reference,
    geocode_magnitude,
    pixel_lonlat,
    scene_lonlat,
    scene_reference,
)
from sar_processing.model import ImageGrid

WGS84 = CRS.from_epsg(4326)
UTM13N = CRS.from_epsg(32613)
NORTH_UP = Affine(1, 0, 500_000, 0, -1, 4_000_000)


def corner_gcps(width=4, height=4):
    return [
        GroundControlPoint(row=0.5, col=0.5, x=500_000.5, y=3_999_999.5, z=0),
        GroundControlPoint(row=0.5, col=width - 0.5, x=500_000 + width - 0.5, y=3_999_999.5, z=0),
        GroundControlPoint(
            row=height - 0.5,
            col=width - 0.5,
            x=500_000 + width - 0.5,
            y=4_000_000 - height + 0.5,
            z=0,
        ),
        GroundControlPoint(row=height - 0.5, col=0.5, x=500_000.5, y=4_000_000 - height + 0.5, z=0),
    ]


def write_raster(path, values, reference=None, mask=None, nodata=np.nan):
    reference = reference or GeoReference(UTM13N, NORTH_UP)
    with rasterio.open(
        path,
        "w",
        driver="GTiff",
        width=values.shape[1],
        height=values.shape[0],
        count=1,
        dtype="float32",
        nodata=nodata,
        **reference.writer_options(),
    ) as destination:
        destination.write(values.astype(np.float32), 1)
        if mask is not None:
            destination.write_mask(mask)


@pytest.mark.parametrize(
    "latitude,longitude,height", [(0, 0, 0), (35.155, -106.55, 1646), (-40, 175, 32)]
)
def test_scene_origin_maps_to_declared_wgs84_location(latitude, longitude, height):
    actual = scene_lonlat(np.zeros((1, 3)), SceneOrigin(latitude, longitude, height))
    assert_allclose(actual[0], [longitude, latitude, height], atol=2e-6)


def test_equatorial_east_and_up_displacements_match_independent_wgs84_geometry():
    semimajor = 6_378_137.0
    points = np.array([[1000.0, 0.0, 0.0], [0.0, 0.0, 125.0]])
    world = scene_lonlat(points, SceneOrigin(0.0, 0.0))
    expected_longitude = np.rad2deg(np.arctan2(1000.0, semimajor))
    expected_height = np.hypot(semimajor, 1000.0) - semimajor
    assert_allclose(world[0], [expected_longitude, 0.0, expected_height], atol=1e-8)
    assert_allclose(world[1], [0.0, 0.0, 125.0], atol=1e-8)


@pytest.mark.parametrize(
    "heading,x_direction,y_direction",
    [(0, (0, 1), (-1, 0)), (90, (1, 0), (0, 1)), (180, (0, -1), (1, 0)), (270, (-1, 0), (0, -1))],
)
def test_heading_is_clockwise_from_north_and_scene_y_is_left(heading, x_direction, y_direction):
    world = scene_lonlat(
        np.array([[100.0, 0, 0], [0, 100.0, 0]]), SceneOrigin(0, 0, x_heading_deg=heading)
    )
    for point, expected_direction in zip(world, [x_direction, y_direction], strict=True):
        for coordinate, direction in zip(point[:2], expected_direction, strict=True):
            if direction == 0:
                assert abs(coordinate) < 1e-12
            else:
                assert coordinate * direction > 0


def test_local_vertical_preserves_geodetic_latitude_longitude():
    origin = SceneOrigin(35.155, -106.55, 1646)
    world = scene_lonlat(np.array([[0.0, 0.0, 100.0]]), origin)
    assert_allclose(world[0], [-106.55, 35.155, 1746], atol=2e-6)


@pytest.mark.parametrize(
    "origin, message",
    [
        (SceneOrigin(np.nan, 0), "finite"),
        (SceneOrigin(0, np.inf), "finite"),
        (SceneOrigin(0, 0, x_heading_deg=np.inf), "finite"),
        (SceneOrigin(91, 0), "WGS84 bounds"),
        (SceneOrigin(0, -181), "WGS84 bounds"),
    ],
)
def test_scene_origin_validation(origin, message):
    with pytest.raises(ValueError, match=message):
        origin.validate()


@pytest.mark.parametrize("points", [np.ones(3), np.ones((2, 2)), np.array([[0, 0, np.inf]])])
def test_invalid_scene_points(points):
    with pytest.raises(ValueError, match="finite scene points"):
        scene_lonlat(points, SceneOrigin(0, 0))


@pytest.mark.parametrize("axis", [0, 1, 2])
def test_complex_scene_coordinates_cannot_be_silently_cast_to_real(axis):
    points = np.zeros((1, 3), dtype=np.complex128)
    points[0, axis] = 1 + 100j
    with pytest.raises(ValueError, match="real coordinates"):
        scene_lonlat(points, SceneOrigin(0, 0))


@pytest.mark.parametrize("field", ["latitude", "longitude", "height_m", "x_heading_deg"])
def test_complex_scene_origin_components_are_rejected_before_gdal_conversion(field):
    origin = SceneOrigin(0, 0)
    setattr(origin, field, np.complex128(10 + 20j))
    with pytest.raises(ValueError, match="real and finite"):
        scene_lonlat(np.zeros((1, 3)), origin)


def test_affine_rotation_shear_and_corner_pixel_centers():
    # World centers independently specified for the four pixels of a rotated/sheared grid.
    reference = GeoReference(WGS84, Affine(0.02, 0.003, -106, 0.004, -0.01, 35))
    rows = np.array([[0.0, 0.0], [1.0, 1.0]])
    cols = np.array([[0.0, 1.0], [0.0, 1.0]])
    expected = [[-105.9885, 34.997], [-105.9685, 35.001], [-105.9855, 34.987], [-105.9655, 34.991]]
    assert_allclose(pixel_lonlat(reference, rows, cols), expected, atol=1e-12)
    options = reference.writer_options()
    assert options == {"crs": WGS84, "transform": reference.affine}


def test_gcp_corner_centers_are_not_shifted_by_an_additional_half_pixel():
    corners = [
        GroundControlPoint(row=0.5, col=0.5, x=-106.0, y=35.0),
        GroundControlPoint(row=0.5, col=3.5, x=-105.97, y=35.006),
        GroundControlPoint(row=2.5, col=3.5, x=-105.966, y=34.986),
        GroundControlPoint(row=2.5, col=0.5, x=-105.996, y=34.98),
    ]
    reference = GeoReference(WGS84, gcps=corners)
    rows = np.array([0.0, 0.0, 2.0, 2.0, 1.0])
    cols = np.array([0.0, 3.0, 3.0, 0.0, 1.5])
    expected = [
        [-106, 35],
        [-105.97, 35.006],
        [-105.966, 34.986],
        [-105.996, 34.98],
        [-105.983, 34.993],
    ]
    assert_allclose(pixel_lonlat(reference, rows, cols), expected, atol=1e-10)
    assert reference.writer_options() == {"crs": WGS84, "gcps": corners}


@pytest.mark.parametrize(
    "reference, message",
    [
        (GeoReference(None, NORTH_UP), "Exactly one"),
        (GeoReference(WGS84), "Exactly one"),
        (GeoReference(UTM13N, NORTH_UP, corner_gcps()), "Exactly one"),
        (GeoReference(WGS84, Affine(0, 0, 0, 0, 0, 0)), "degenerate"),
        (GeoReference(WGS84, Affine(np.nan, 0, 0, 0, 1, 0)), "non-finite"),
        (GeoReference(UTM13N, gcps=corner_gcps()[:3]), "four finite"),
        (
            GeoReference(UTM13N, gcps=[GroundControlPoint(row=np.nan, col=0, x=0, y=0)] * 4),
            "four finite",
        ),
    ],
)
def test_invalid_georeference(reference, message):
    with pytest.raises(ValueError, match=message):
        reference.validate()


@pytest.mark.parametrize(
    "rows,cols",
    [
        (np.ones(2), np.ones(3)),
        (np.array([np.nan]), np.zeros(1)),
        (np.zeros(1), np.array([np.inf])),
    ],
)
def test_invalid_pixel_coordinates(rows, cols):
    with pytest.raises(ValueError, match="matching shapes and finite"):
        pixel_lonlat(GeoReference(UTM13N, NORTH_UP), rows, cols)


@pytest.mark.parametrize("complex_axis", ["rows", "cols"])
def test_complex_pixel_coordinates_are_rejected_before_gdal_conversion(complex_axis):
    coordinates = {"rows": np.zeros(1), "cols": np.zeros(1)}
    coordinates[complex_axis] = np.array([100j])
    with pytest.raises(ValueError, match="must be real"):
        pixel_lonlat(GeoReference(UTM13N, NORTH_UP), **coordinates)


@pytest.mark.parametrize("field", ["row", "col", "x", "y", "z"])
def test_complex_gcp_components_cannot_be_silently_cast_to_real(field):
    reference = GeoReference(UTM13N, gcps=corner_gcps())
    point = reference.gcps[0]
    setattr(point, field, np.complex128(getattr(point, field) + 2j))
    with pytest.raises(ValueError, match="real coordinates"):
        pixel_lonlat(reference, np.zeros(1), np.zeros(1))


def test_invalid_transformed_latitude_is_rejected():
    reference = GeoReference(WGS84, Affine(1, 0, 0, 0, -1, 95))
    with pytest.raises(ValueError, match="invalid longitude/latitude"):
        pixel_lonlat(reference, np.zeros(1), np.zeros(1))


@pytest.mark.parametrize("longitude", [-181.0, -180.000001, 180.000001, 181.0])
def test_out_of_bounds_transformed_longitudes_are_rejected(longitude):
    reference = GeoReference(WGS84, Affine(1, 0, longitude - 0.5, 0, -1, 1))
    with pytest.raises(ValueError, match="invalid longitude/latitude"):
        pixel_lonlat(reference, np.zeros(1), np.zeros(1))


@pytest.mark.parametrize("longitude", [-180.0, 180.0])
def test_longitude_boundaries_are_valid_without_wrapping(longitude):
    reference = GeoReference(WGS84, Affine(1, 0, longitude - 0.5, 0, -1, 1))
    actual = pixel_lonlat(reference, np.zeros(1), np.zeros(1))
    assert_allclose(actual, [[longitude, 0.5]], rtol=0, atol=1e-12)


@pytest.mark.parametrize("crs,affine", [(None, NORTH_UP), (WGS84, Affine.identity())])
def test_dataset_reference_does_not_guess_absent_georeferencing(crs, affine):
    source = SimpleNamespace(gcps=([], None), crs=crs, transform=affine)
    with pytest.raises(ValueError, match="no usable geographic coordinates"):
        dataset_reference(source)


@pytest.mark.parametrize("gcps", [False, True])
def test_dataset_reference_reads_actual_geotiff_affine_or_gcps(tmp_path, gcps):
    expected = GeoReference(UTM13N, gcps=corner_gcps()) if gcps else GeoReference(UTM13N, NORTH_UP)
    path = tmp_path / "input.tif"
    write_raster(path, np.ones((4, 4)), expected)
    with rasterio.open(path) as source:
        actual = dataset_reference(source)
    assert actual.crs == UTM13N
    assert bool(actual.gcps) == gcps
    if gcps:
        assert_allclose(
            [[g.col, g.row, g.x, g.y] for g in actual.gcps],
            [[g.col, g.row, g.x, g.y] for g in expected.gcps],
        )
    else:
        assert actual.affine == NORTH_UP


def test_scene_plane_reference_anchors_origin_and_keeps_image_y_direction():
    grid = ImageGrid(np.linspace(-20, 20, 9), np.linspace(-10, 10, 5), z_m=12.0)
    reference = scene_reference(grid, SceneOrigin(35, -106, height_m=1600, x_heading_deg=90))
    assert reference.crs == WGS84
    assert len(reference.gcps) == 25
    middle = next(g for g in reference.gcps if g.row == 2.5 and g.col == 4.5)
    assert_allclose([middle.x, middle.y, middle.z], [-106, 35, 1612], atol=2e-6)
    positions = pixel_lonlat(reference, np.array([2, 2, 4]), np.array([4, 8, 4]))
    assert_allclose(positions[0], [-106, 35], atol=1e-10)
    assert positions[1, 0] > positions[0, 0]
    assert positions[2, 1] > positions[0, 1]


@pytest.mark.parametrize("gcp_mapping", [False, True])
def test_actual_gdal_reprojection_preserves_nodata_and_valid_zero(tmp_path, gcp_mapping):
    values = np.arange(16, dtype=np.float32).reshape(4, 4)
    values[1, 1] = np.nan
    reference = (
        GeoReference(UTM13N, gcps=corner_gcps()) if gcp_mapping else GeoReference(UTM13N, NORTH_UP)
    )
    source = tmp_path / "source.tif"
    output = tmp_path / "geocoded.tif"
    write_raster(source, values, reference)
    report = geocode_magnitude(source, output, reference, 1.0, "EPSG:32613")
    with rasterio.open(output) as dataset:
        actual = dataset.read(1)
        assert dataset.crs == UTM13N
        assert_allclose(tuple(dataset.transform), tuple(NORTH_UP), atol=1e-7)
        assert_allclose(actual, values, atol=1e-7, equal_nan=True)
        assert dataset.read_masks(1)[0, 0] == 255
        assert dataset.read_masks(1)[1, 1] == 0
        assert "no DEM correction" in dataset.tags()["processing"]
    assert report["terrain_correction"] == "not applied"
    assert report["width"] == report["height"] == 4


@pytest.mark.parametrize("nodata", [np.nan, -9999.0])
def test_actual_gdal_reprojection_respects_explicit_validity_mask(tmp_path, nodata):
    values = np.zeros((4, 4), np.float32)
    values[1, 1] = 99
    values[2, 2] = nodata
    mask = np.full((4, 4), 255, dtype=np.uint8)
    mask[1, 1] = 0
    source = tmp_path / "masked.tif"
    output = tmp_path / "warped.tif"
    reference = GeoReference(UTM13N, NORTH_UP)
    write_raster(source, values, reference, mask, nodata=nodata)
    geocode_magnitude(source, output, reference, 1.0)
    expected_mask = mask.copy()
    expected_mask[2, 2] = 0
    with rasterio.open(output) as dataset:
        assert np.isnan(dataset.read(1)[1, 1])
        assert np.isnan(dataset.read(1)[2, 2])
        assert_array_equal(dataset.read(1)[expected_mask != 0], 0)
        assert_array_equal(dataset.read_masks(1), expected_mask)


def test_geocodes_two_dimensional_gcps_without_declared_height(tmp_path):
    reference = GeoReference(UTM13N, gcps=corner_gcps())
    for point in reference.gcps:
        point.z = None
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    values = np.arange(16).reshape(4, 4)
    write_raster(source, values)
    geocode_magnitude(source, output, reference, 1.0)
    assert all(point.z == 0 for point in reference.gcps)
    with rasterio.open(output) as dataset:
        assert_allclose(dataset.read(1), values, atol=1e-7)


def test_nonfinite_gcp_height_is_rejected():
    reference = GeoReference(UTM13N, gcps=corner_gcps())
    reference.gcps[0].z = np.inf
    with pytest.raises(ValueError, match="four finite"):
        reference.validate()


def test_warp_normalization_handles_partial_tiles_without_losing_edge_pixels(tmp_path):
    values = np.arange(513 * 515, dtype=np.float32).reshape(513, 515)
    mask = np.full(values.shape, 255, dtype=np.uint8)
    mask[511, 511] = mask[512, 514] = 0
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    reference = GeoReference(UTM13N, NORTH_UP)
    write_raster(source, values, reference, mask, nodata=None)
    geocode_magnitude(source, output, reference, 1.0)
    expected = values.copy()
    expected[mask == 0] = np.nan
    with rasterio.open(output) as dataset:
        assert_allclose(dataset.read(1), expected, atol=1e-6, equal_nan=True)


def test_actual_rotated_sheared_warp_preserves_footprint_and_exterior_nodata(tmp_path):
    reference = GeoReference(UTM13N, Affine(2, 1, 500_000, 0.5, -2, 4_000_000))
    source, output = tmp_path / "rotated.tif", tmp_path / "rectified.tif"
    write_raster(source, np.zeros((3, 4)), reference)
    geocode_magnitude(source, output, reference, 1.0, "EPSG:32613")
    # Independently specified edge corners: TL, TR, BR, BL.
    corners = np.array(
        [[500_000, 4_000_000], [500_008, 4_000_002], [500_011, 3_999_996], [500_003, 3_999_994]]
    )
    with rasterio.open(output) as dataset:
        assert dataset.transform.b == dataset.transform.d == 0
        assert dataset.bounds.left <= corners[:, 0].min()
        assert dataset.bounds.right >= corners[:, 0].max()
        assert dataset.bounds.bottom <= corners[:, 1].min()
        assert dataset.bounds.top >= corners[:, 1].max()
        assert corners[:, 0].min() - dataset.bounds.left < 2
        assert dataset.bounds.right - corners[:, 0].max() < 2
        assert corners[:, 1].min() - dataset.bounds.bottom < 2
        assert dataset.bounds.top - corners[:, 1].max() < 2
        values = dataset.read(1)
        finite = np.isfinite(values)
        assert 30 < finite.sum() < 80
        assert np.any(~finite)
        assert_array_equal(values[finite], 0)


@pytest.mark.parametrize("epsg,northing", [(32613, 4_000_000), (32713, 6_000_000)])
def test_automatic_utm_selection_matches_hemisphere(tmp_path, epsg, northing):
    reference = GeoReference(CRS.from_epsg(epsg), Affine(1, 0, 500_000, 0, -1, northing))
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    write_raster(source, np.ones((4, 4)), reference)
    report = geocode_magnitude(source, output, reference, 1.0)
    assert report["crs"] == f"EPSG:{epsg}"


def test_reprojection_uses_all_bounds_from_explicit_reference(tmp_path):
    embedded = GeoReference(UTM13N, Affine(1, 0, 500_010, 0, -1, 4_000_010))
    explicit = GeoReference(UTM13N, NORTH_UP)
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    values = np.arange(16, dtype=np.float32).reshape(4, 4)
    write_raster(source, values, embedded)
    geocode_magnitude(source, output, explicit, 1.0, "EPSG:32613")
    with rasterio.open(output) as dataset:
        assert_allclose(tuple(dataset.bounds), [500_000, 3_999_996, 500_004, 4_000_000], atol=1e-7)
        assert_allclose(dataset.read(1), values, atol=1e-7)


@pytest.mark.parametrize("resolution", [0, -1, np.inf, np.nan])
def test_invalid_resolution_rejected_before_opening_source(tmp_path, resolution):
    with pytest.raises(ValueError, match="positive finite metres"):
        geocode_magnitude(
            tmp_path / "absent.tif",
            tmp_path / "output.tif",
            GeoReference(UTM13N, NORTH_UP),
            resolution,
        )


@pytest.mark.parametrize("crs", ["EPSG:4326", "EPSG:2276"])
def test_output_crs_must_be_projected_in_metres(tmp_path, crs):
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    reference = GeoReference(UTM13N, NORTH_UP)
    write_raster(source, np.ones((4, 4)), reference)
    with pytest.raises(ValueError, match="projected with metre units"):
        geocode_magnitude(source, output, reference, 1.0, crs)
    assert not output.exists()


def test_polar_scene_requires_explicit_projected_crs(tmp_path):
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    reference = GeoReference(WGS84, Affine(0.01, 0, -30, 0, -0.01, 86))
    write_raster(source, np.ones((4, 4)), reference)
    with pytest.raises(ValueError, match="outside the UTM latitude range"):
        geocode_magnitude(source, output, reference, 1.0)


def test_output_pixel_limit_rejects_tiny_resolution_before_creation(tmp_path):
    source, output = tmp_path / "source.tif", tmp_path / "output.tif"
    reference = GeoReference(UTM13N, NORTH_UP)
    write_raster(source, np.ones((4, 4)), reference)
    with pytest.raises(ValueError, match="64 million pixels"):
        geocode_magnitude(source, output, reference, 0.0001)
    assert not output.exists()


def test_nonlinear_gcp_warp_places_target_at_tps_anchor_instead_of_polynomial_fit(tmp_path):
    # Nine control points include a local 8m eastward displacement. An automatic
    # polynomial fit misses this anchor; TPS must honour the actual measurement.
    gcps = [
        GroundControlPoint(
            row=row,
            col=col,
            x=500_000 + col + (8 if row == col == 50.5 else 0),
            y=4_000_000 - row,
            z=1700,
        )
        for row in (0.5, 50.5, 100.5)
        for col in (0.5, 50.5, 100.5)
    ]
    reference = GeoReference(UTM13N, gcps=gcps)
    rows, cols = np.mgrid[:101, :101]
    target = np.exp(-((rows - 50) ** 2 + (cols - 50) ** 2) / 2).astype(np.float32)
    source, output = tmp_path / "nonlinear.tif", tmp_path / "tps.tif"
    write_raster(source, target, reference)
    report = geocode_magnitude(source, output, reference, 0.5, "EPSG:32613")
    expected = np.array([500_058.5, 3_999_949.5])
    with rasterio.open(output) as dataset:
        data = dataset.read(1)
        row, col = np.unravel_index(np.nanargmax(data), data.shape)
        actual = np.array(dataset.xy(row, col))
        assert np.linalg.norm(actual - expected) < 0.75
        assert dataset.tags()["map_resampling_geometry"] == "2D GCP thin-plate spline"
        # Prove this fixture discriminates TPS from silently reverting to GDAL's
        # default polynomial. It uses the identical raster and destination grid.
        polynomial = np.full(data.shape, np.nan, dtype=np.float32)
        reproject(
            target,
            polynomial,
            gcps=gcps,
            src_crs=UTM13N,
            dst_crs=UTM13N,
            dst_transform=dataset.transform,
            src_nodata=np.nan,
            dst_nodata=np.nan,
            resampling=Resampling.bilinear,
        )
        row, col = np.unravel_index(np.nanargmax(polynomial), polynomial.shape)
        polynomial_peak = np.array(dataset.xy(row, col))
        assert np.linalg.norm(polynomial_peak - expected) > 3
    assert report["map_resampling_geometry"] == "2D GCP thin-plate spline"
    assert not report["map_resampling_uses_dem"]


@pytest.mark.parametrize("terrain_applied", [False, True])
def test_geocode_terrain_provenance_distinguishes_prior_formation_from_map_resampling(
    tmp_path, terrain_applied
):
    source, output = tmp_path / "formed.tif", tmp_path / "map.tif"
    reference = GeoReference(UTM13N, NORTH_UP)
    values = np.arange(16, dtype=np.float32).reshape(4, 4)
    write_raster(source, values, reference)
    report = geocode_magnitude(source, output, reference, 1.0, terrain_applied=terrain_applied)
    assert report["map_resampling_geometry"] == "affine"
    assert not report["map_resampling_uses_dem"]
    assert report["terrain_correction"] == (
        "DEM used in image formation" if terrain_applied else "not applied"
    )
    with rasterio.open(output) as dataset:
        assert dataset.tags()["map_resampling_uses_dem"] == "false"
        assert dataset.tags()["map_resampling_geometry"] == "affine"
        assert_allclose(dataset.read(1), values)
