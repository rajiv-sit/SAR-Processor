"""WGS84 coordinates and GDAL reprojection, preserving pixel-center conventions."""

from dataclasses import dataclass, field
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np
import rasterio
from rasterio.control import GroundControlPoint
from rasterio.crs import CRS
from rasterio.enums import Resampling
from rasterio.transform import Affine, GCPTransformer, array_bounds, xy
from rasterio.warp import calculate_default_transform, reproject, transform
from rasterio.windows import Window

from .model import FloatArray, ImageGrid


@dataclass
class GeoReference:
    crs: CRS
    affine: Affine | None = None
    gcps: list[GroundControlPoint] = field(default_factory=list)

    def validate(self) -> None:
        if not self.crs or (self.affine is None) == (not self.gcps):
            raise ValueError("Exactly one affine transform or GCP mapping and a CRS are required")
        if self.affine is not None:
            if not np.isfinite(tuple(self.affine)).all() or abs(self.affine.determinant) < 1e-20:
                raise ValueError("Geographic transform is degenerate or non-finite")
        else:
            # GDAL's VRT serialization requires a numeric Z even for a 2D mapping.
            for point in self.gcps:
                if point.z is None:
                    point.z = 0.0
            coordinates = [[g.row, g.col, g.x, g.y, g.z] for g in self.gcps]
            if (
                len(self.gcps) < 4
                or np.iscomplexobj(coordinates)
                or not np.isfinite(coordinates).all()
            ):
                raise ValueError(
                    "At least four finite geographic control points with real coordinates are required"
                )

    def writer_options(self) -> dict:
        self.validate()
        return {
            "crs": self.crs,
            **({"transform": self.affine} if self.affine else {"gcps": self.gcps}),
        }


def dataset_reference(source) -> GeoReference:
    gcps, crs = source.gcps
    if gcps:
        result = GeoReference(crs, gcps=gcps)
    else:
        if not source.crs or source.transform == Affine.identity():
            raise ValueError("Input has no usable geographic coordinates")
        result = GeoReference(source.crs, source.transform)
    result.validate()
    return result


def pixel_lonlat(reference: GeoReference, rows: FloatArray, cols: FloatArray) -> FloatArray:
    reference.validate()
    if (
        np.iscomplexobj(rows)
        or np.iscomplexobj(cols)
        or rows.shape != cols.shape
        or not np.isfinite([rows, cols]).all()
    ):
        raise ValueError("Pixel coordinates must be real with matching shapes and finite values")
    if reference.gcps:
        with GCPTransformer(reference.gcps, tps=True) as converter:
            xs, ys = converter.xy(rows.ravel(), cols.ravel(), offset="center")
    else:
        xs, ys = xy(reference.affine, rows.ravel(), cols.ravel(), offset="center")
    lon, lat = transform(reference.crs, CRS.from_epsg(4326), xs, ys)
    result = np.column_stack([lon, lat])
    if (
        not np.isfinite(result).all()
        or np.any(np.abs(result[:, 0]) > 180)
        or np.any(np.abs(result[:, 1]) > 90)
    ):
        raise ValueError("Coordinate transformation produced invalid longitude/latitude")
    return result


@dataclass
class SceneOrigin:
    latitude: float
    longitude: float
    height_m: float = 0.0
    x_heading_deg: float = 90.0

    def validate(self) -> None:
        values = [self.latitude, self.longitude, self.height_m, self.x_heading_deg]
        if np.iscomplexobj(values) or not np.isfinite(values).all():
            raise ValueError("Scene origin must be real and finite")
        if abs(self.latitude) > 90 or abs(self.longitude) > 180:
            raise ValueError("Scene origin is outside WGS84 bounds")


def scene_lonlat(points_m: FloatArray, origin: SceneOrigin) -> FloatArray:
    """Scene x heading clockwise from north; y is 90 degrees left of x; z is up."""
    origin.validate()
    if (
        np.iscomplexobj(points_m)
        or points_m.ndim != 2
        or points_m.shape[1] != 3
        or not np.isfinite(points_m).all()
    ):
        raise ValueError("Expected finite scene points with real coordinates and shape (points, 3)")
    lat, lon, heading = np.deg2rad([origin.latitude, origin.longitude, origin.x_heading_deg])
    east = points_m[:, 0] * np.sin(heading) - points_m[:, 1] * np.cos(heading)
    north = points_m[:, 0] * np.cos(heading) + points_m[:, 1] * np.sin(heading)
    enu = np.column_stack([east, north, points_m[:, 2]])
    basis = np.array(
        [
            [-np.sin(lon), -np.sin(lat) * np.cos(lon), np.cos(lat) * np.cos(lon)],
            [np.cos(lon), -np.sin(lat) * np.sin(lon), np.cos(lat) * np.sin(lon)],
            [0, np.cos(lat), np.sin(lat)],
        ]
    )
    ox, oy, oz = transform(
        "EPSG:4979", "EPSG:4978", [origin.longitude], [origin.latitude], [origin.height_m]
    )
    ecef = enu @ basis.T + np.array([ox[0], oy[0], oz[0]])
    lons, lats, heights = transform("EPSG:4978", "EPSG:4979", *ecef.T)
    return np.column_stack([lons, lats, heights])


def scene_reference(grid: ImageGrid, origin: SceneOrigin) -> GeoReference:
    grid.validate()
    rows = np.unique(np.linspace(0, grid.y_m.size - 1, 5, dtype=int))
    cols = np.unique(np.linspace(0, grid.x_m.size - 1, 5, dtype=int))
    points = [(grid.x_m[c], grid.y_m[r], grid.height_at(r, c)) for r in rows for c in cols]
    world = scene_lonlat(np.asarray(points), origin)
    gcps = [
        GroundControlPoint(
            row=float(r) + 0.5, col=float(c) + 0.5, x=float(p[0]), y=float(p[1]), z=float(p[2])
        )
        for (r, c), p in zip(((r, c) for r in rows for c in cols), world)
    ]
    return GeoReference(CRS.from_epsg(4326), gcps=gcps)


def _prepare_warp_source(source, path: Path, reference: GeoReference) -> None:
    """Apply explicit geometry and combine masks/nodata in bounded raster tiles."""
    with rasterio.open(
        path,
        "w",
        driver="GTiff",
        width=source.width,
        height=source.height,
        count=1,
        dtype="float32",
        nodata=np.nan,
        compress="deflate",
        tiled=True,
        **reference.writer_options(),
    ) as destination:
        for row in range(0, source.height, 512):
            for col in range(0, source.width, 512):
                window = Window(
                    col, row, min(512, source.width - col), min(512, source.height - row)
                )
                values = source.read(1, window=window, masked=True, out_dtype="float32").filled(
                    np.nan
                )
                if source.nodata is not None:
                    values[values == source.nodata] = np.nan
                values[~np.isfinite(values)] = np.nan
                destination.write(values, 1, window=window)


def geocode_magnitude(
    source_path: Path,
    output_path: Path,
    reference: GeoReference,
    resolution_m: float,
    destination_crs: str | None = None,
    *,
    terrain_applied: bool = False,
) -> dict:
    """Resample magnitude using affine geometry or a 2D thin-plate spline.

    ``terrain_applied`` records the caller's prior use of DEM heights during
    image formation. This operation does not perform a DEM/range-Doppler solve;
    GCP height values do not turn its two-dimensional TPS into terrain correction.
    """
    reference.validate()
    if not np.isfinite(resolution_m) or resolution_m <= 0:
        raise ValueError("Geocoded resolution must be positive finite metres")
    with rasterio.open(source_path) as source:
        center = pixel_lonlat(
            reference, np.array([(source.height - 1) / 2]), np.array([(source.width - 1) / 2])
        )[0]
        if destination_crs is None:
            if not -80 <= center[1] <= 84:
                raise ValueError("Specify a projected CRS outside the UTM latitude range")
            zone = min(60, int((center[0] + 180) // 6) + 1)
            destination_crs = f"EPSG:{(32600 if center[1] >= 0 else 32700) + zone}"
        dst_crs = CRS.from_user_input(destination_crs)
        if not dst_crs.is_projected or dst_crs.linear_units_factor[1] != 1:
            raise ValueError("Destination CRS must be projected with metre units")
        if reference.gcps:
            # SRC_METHOD is a GDALCreateGenImgProjTransformer2 option. Keep it
            # explicit: GDAL's default polynomial does not honour nonlinear GCPs
            # exactly. Rasterio 1.5.1 also duplicates it into GDALWarpOptions,
            # producing a GDAL 3.12 warning there even though TPS is selected.
            kwargs: dict = {"gcps": reference.gcps, "SRC_METHOD": "GCP_TPS"}
        else:
            left, bottom, right, top = array_bounds(source.height, source.width, reference.affine)
            kwargs = {"left": left, "bottom": bottom, "right": right, "top": top}
        dst_transform, width, height = calculate_default_transform(
            reference.crs, dst_crs, source.width, source.height, resolution=resolution_m, **kwargs
        )
        if min(width, height) < 1 or width * height > 64_000_000:
            raise ValueError(
                "Geocoded raster exceeds 64 million pixels; increase resolution spacing"
            )
        # A normalized on-disk source avoids loading the entire raster into memory.
        # GDAL otherwise gives nodata precedence over a separate validity mask and
        # can use dataset geometry instead of an explicitly supplied transform.
        with TemporaryDirectory(prefix="sar-geocode-") as temporary:
            prepared_path = Path(temporary) / "source.tif"
            _prepare_warp_source(source, prepared_path, reference)
            with (
                rasterio.open(prepared_path) as prepared,
                rasterio.open(
                    output_path,
                    "w",
                    driver="GTiff",
                    width=width,
                    height=height,
                    count=1,
                    dtype="float32",
                    crs=dst_crs,
                    transform=dst_transform,
                    nodata=np.nan,
                    compress="deflate",
                    tiled=True,
                ) as destination,
            ):
                source_mapping: dict = (
                    {"gcps": reference.gcps, "SRC_METHOD": "GCP_TPS"}
                    if reference.gcps
                    else {"src_transform": reference.affine}
                )
                reproject(
                    source=rasterio.band(prepared, 1),
                    destination=rasterio.band(destination, 1),
                    src_crs=reference.crs,
                    dst_transform=dst_transform,
                    dst_crs=dst_crs,
                    src_nodata=np.nan,
                    dst_nodata=np.nan,
                    resampling=Resampling.bilinear,
                    **source_mapping,
                )
                destination.update_tags(
                    processing=(
                        "DEM used in image formation; control-point map resampling"
                        if terrain_applied
                        else "corner/control-point georeferencing; no DEM correction"
                    ),
                    units="uncalibrated magnitude DN",
                    map_resampling_geometry="2D GCP thin-plate spline"
                    if reference.gcps
                    else "affine",
                    map_resampling_uses_dem="false",
                )
    return {
        "crs": dst_crs.to_string(),
        "resolution_m": resolution_m,
        "width": width,
        "height": height,
        "resampling": "bilinear magnitude",
        "terrain_correction": "DEM used in image formation" if terrain_applied else "not applied",
        "map_resampling_geometry": "2D GCP thin-plate spline" if reference.gcps else "affine",
        "map_resampling_uses_dem": False,
    }
