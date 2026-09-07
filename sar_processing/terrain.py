"""DEM-supported local scene heights with explicit ellipsoidal metre semantics.

This supplies backprojection target heights, not a sensor model for focused SAR
imagery. The caller must provide a verified Earth origin and DEM heights above
the same WGS84 ellipsoid. MSL heights need a geoid conversion before this API.
See https://isce-framework.github.io/isce3-docs/overview_geometry.html.
"""

from pathlib import Path

import numpy as np
import rasterio
from rasterio.warp import transform
from rasterio.windows import Window

from .geography import SceneOrigin, scene_lonlat
from .model import FloatArray, ImageGrid

MAX_DEM_WINDOW_CELLS = 4_194_304
POINTS_PER_BATCH = 65_536
MAX_HEIGHT_ITERATIONS = 12
HEIGHT_TOLERANCE_M = 0.0001
_METRE_UNITS = frozenset({"m", "metre", "metres", "meter", "meters"})
_ELLIPSOID_DATUMS = frozenset({"ellipsoid", "ellipsoidal", "hae", "wgs84", "wgs84 ellipsoid"})


def _validate_dem(source) -> None:
    if source.count != 1 or min(source.width, source.height) < 2:
        raise ValueError("DEM must be a single-band raster with at least 2 by 2 pixels")
    if source.crs is None or not (source.crs.is_geographic or source.crs.is_projected):
        raise ValueError("DEM requires a geographic or projected horizontal CRS")
    affine = source.transform
    if not np.isfinite(tuple(affine)).all() or abs(affine.determinant) < 1e-20:
        raise ValueError("DEM requires a finite, nondegenerate affine transform")
    if source.gcps[0]:
        raise ValueError("DEM GCP geometry must be reprojected to an affine grid first")
    if np.dtype(source.dtypes[0]).kind not in "fiu":
        raise ValueError("DEM heights must be real numeric values")
    if (source.units[0] or "").strip().lower() not in _METRE_UNITS:
        raise ValueError("DEM band must explicitly declare height units in metres")
    for tags in (source.tags(), source.tags(1)):
        for key, value in tags.items():
            if key.lower() in {"vertical_datum", "height_reference"}:
                if value.strip().lower() not in _ELLIPSOID_DATUMS:
                    raise ValueError("DEM declares an unsupported vertical datum; use WGS84 HAE")
    if not np.isfinite([source.scales[0], source.offsets[0]]).all() or source.scales[0] == 0:
        raise ValueError("DEM scale and offset must be finite, with nonzero scale")


def _sample_dem(source, lonlat: FloatArray) -> FloatArray:
    """Bilinear sample on pixel centres, loading only a bounded supporting window."""
    xs, ys = transform("EPSG:4326", source.crs, lonlat[:, 0], lonlat[:, 1])
    cols, rows = ~source.transform * (np.asarray(xs), np.asarray(ys))
    cols, rows = cols - 0.5, rows - 0.5
    if (
        not np.isfinite([rows, cols]).all()
        or np.any(rows < -1e-8)
        or np.any(cols < -1e-8)
        or np.any(rows > source.height - 1 + 1e-8)
        or np.any(cols > source.width - 1 + 1e-8)
    ):
        raise ValueError("Scene exceeds the DEM pixel-centre coverage")
    rows = np.clip(rows, 0, source.height - 1)
    cols = np.clip(cols, 0, source.width - 1)
    r0 = np.minimum(np.floor(rows).astype(np.intp), source.height - 2)
    c0 = np.minimum(np.floor(cols).astype(np.intp), source.width - 2)
    top, left = int(r0.min()), int(c0.min())
    height, width = int(r0.max()) - top + 2, int(c0.max()) - left + 2
    if height * width > MAX_DEM_WINDOW_CELLS:
        raise ValueError("DEM supporting window exceeds 4 million pixels; crop/resample the DEM")
    values = source.read(
        1, window=Window(left, top, width, height), masked=True, out_dtype="float64"
    )
    values = values * source.scales[0] + source.offsets[0]
    dr, dc = rows - r0, cols - c0
    r0, c0 = r0 - top, c0 - left
    result = np.zeros(rows.shape, dtype=np.float64)
    for rdelta, cdelta, weight in (
        (0, 0, (1 - dr) * (1 - dc)),
        (0, 1, (1 - dr) * dc),
        (1, 0, dr * (1 - dc)),
        (1, 1, dr * dc),
    ):
        selected = values[r0 + rdelta, c0 + cdelta]
        contributing = weight > 0
        if np.any(contributing & (np.ma.getmaskarray(selected) | ~np.isfinite(selected.data))):
            raise ValueError("DEM has missing or nonfinite supporting heights")
        result[contributing] += selected.data[contributing] * weight[contributing]
    if not np.all((-12000 <= result) & (result <= 100000)):
        raise ValueError("DEM heights lie outside supported terrestrial bounds")
    return result


def scene_height_grid(
    grid: ImageGrid,
    origin: SceneOrigin,
    dem_path: Path,
    *,
    height_reference: str = "ellipsoid",
) -> FloatArray:
    """Return (y, x) local-Z metres intersecting a verified ellipsoidal DEM.

    ``height_reference`` asserts the DEM heights are WGS84 ellipsoidal, and the
    band must separately declare metre units. A tag declaring another vertical
    datum is rejected. No geoid, origin, trajectory or accuracy is inferred.
    Constant and per-pixel grid heights are accepted as iterative initial values.
    Earth curvature and scene heading are handled through ECEF coordinates.
    Every output must close in geodetic height within 0.1 mm; that is numerical
    convergence, not the accuracy of the DEM or sensor. Missing data fail closed.
    """
    if height_reference != "ellipsoid":
        raise ValueError("Terrain processing requires explicitly ellipsoidal heights; convert MSL")
    grid.validate()
    origin.validate()
    shape = (grid.y_m.size, grid.x_m.size)
    initial = np.broadcast_to(np.asarray(grid.z_m, dtype=np.float64), shape)
    result = np.empty(shape, dtype=np.float64)
    with rasterio.open(dem_path) as source:
        _validate_dem(source)
        for first in range(0, result.size, POINTS_PER_BATCH):
            index = np.arange(first, min(first + POINTS_PER_BATCH, result.size))
            rows, cols = index // shape[1], index % shape[1]
            points = np.column_stack([grid.x_m[cols], grid.y_m[rows], initial[rows, cols]])
            for _ in range(MAX_HEIGHT_ITERATIONS):
                world = scene_lonlat(points, origin)
                residual = _sample_dem(source, world[:, :2]) - world[:, 2]
                if np.max(np.abs(residual)) <= HEIGHT_TOLERANCE_M:
                    result.ravel()[index] = points[:, 2]
                    break
                points[:, 2] += residual
            else:
                raise ValueError("DEM intersection failed to converge in local scene coordinates")
    return result
