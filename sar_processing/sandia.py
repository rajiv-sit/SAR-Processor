"""Sandia FARAD focused complex NITF import using GDAL metadata and raster IO."""

from pathlib import Path

import numpy as np
import rasterio
from rasterio.windows import Window

from .geography import dataset_reference, geocode_magnitude, pixel_lonlat
from .gotcha import source_record
from .outputs import write_preview, write_raw
from .viewer import write_geocoded_frame
from .sensor_metadata import parse_sandia_sensor_metadata


def decode_magnitude_phase(magnitude: np.ndarray, phase: np.ndarray) -> np.ndarray:
    if magnitude.shape != phase.shape or magnitude.dtype.kind != "u" or phase.dtype.kind != "u":
        raise ValueError("Expected matching unsigned magnitude and phase arrays")
    if magnitude.dtype.itemsize != 2 or phase.dtype.itemsize != 2:
        raise ValueError("Sandia importer supports 16-bit magnitude/phase bands")
    # NGA generic unsigned MP convention. Cast BEFORE multiplication to prevent overflow.
    theta = phase.astype(np.float64) * (2 * np.pi / 65536.0)
    return (magnitude.astype(np.float64) * np.exp(1j * theta)).astype(np.complex64)


def validate_sandia(path: Path, source) -> None:
    with path.open("rb") as raw:
        header = raw.read(360)
    if len(header) != 360 or header[:9] != b"NITF02.10":
        raise ValueError("Expected a complete NITF 2.1 header")
    if int(header[342:354]) != path.stat().st_size:
        raise ValueError("NITF file is truncated or its declared length is incorrect")
    if source.driver != "NITF" or source.count != 2 or source.subdatasets:
        raise ValueError("Expected a single-image, two-band Sandia NITF")
    if not 1 <= source.width * source.height <= 64_000_000:
        raise ValueError("Sandia raster must contain 1 to 64 million pixels")
    if source.dtypes != ("uint16", "uint16") or [
        source.tags(i).get("NITF_ISUBCAT") for i in (1, 2)
    ] != ["M", "P"]:
        raise ValueError("Expected unsigned 16-bit M,P bands in that order")
    if source.tags().get("NITF_ABPP") != "16":
        raise ValueError("Sandia phase decoding requires 16 actual bits per band (NITF_ABPP)")
    if "CMETAA" in source.tags(ns="TRE"):
        raise ValueError(
            "CMETAA amplitude remapping is not supported by the unscaled Sandia importer"
        )
    covered_rows = source.tags().get("NITF_BLOCKA_L_LINES_01")
    if covered_rows is not None and int(covered_rows) != source.height:
        raise ValueError("BLOCKA coordinates do not cover the complete image")


def process_sandia(
    path: Path,
    work: Path,
    resolution_m: float = 0.25,
    destination_crs: str | None = None,
    *,
    write_viewer_raw: bool = True,
    preview_db: float = 50.0,
    final_output: Path | None = None,
) -> dict:
    with rasterio.open(path) as source:
        validate_sandia(path, source)
        sensor_metadata = parse_sandia_sensor_metadata(
            source.tags(ns="xml:TRE").get("xml:TRE"), (source.height, source.width)
        )
        reference = dataset_reference(source)
        profile = {
            "driver": "GTiff",
            "width": source.width,
            "height": source.height,
            "count": 1,
            "compress": "deflate",
            "tiled": True,
            **reference.writer_options(),
        }
        with (
            rasterio.open(work / "complex_native.tif", "w", dtype="complex64", **profile) as iq_out,
            rasterio.open(
                work / "magnitude_native.tif", "w", dtype="float32", **profile
            ) as mag_out,
        ):
            for row in range(0, source.height, 256):
                window = Window(0, row, source.width, min(256, source.height - row))
                mag, phase = source.read((1, 2), window=window)
                iq_out.write(decode_magnitude_phase(mag, phase), 1, window=window)
                mag_out.write(mag.astype(np.float32), 1, window=window)
                valid = np.all(source.read_masks((1, 2), window=window) != 0, axis=0)
                iq_out.write_mask(valid, window=window)
                mag_out.write_mask(valid, window=window)
            iq_out.update_tags(
                stage="focused_complex_image",
                phase_scale="2*pi/65536",
                amplitude_units="uncalibrated DN",
                range_azimuth_compression="already performed upstream",
            )
        rows = np.array([0, 0, source.height - 1, source.height - 1, (source.height - 1) / 2])
        cols = np.array([0, source.width - 1, source.width - 1, 0, (source.width - 1) / 2])
        coords = pixel_lonlat(reference, rows, cols)
        shape = [source.height, source.width]
    geocoded = geocode_magnitude(
        work / "magnitude_native.tif",
        work / "magnitude_geocoded.tif",
        reference,
        resolution_m,
        destination_crs,
    )
    with rasterio.open(work / "magnitude_native.tif") as magnitude:
        # Viewer export preserves the entire native grid; PNG is bounded independently.
        data = magnitude.read(1, masked=True).filled(0)
        if write_viewer_raw:
            write_raw(work / "image.raw", data)
        write_preview(work / "image_native.png", data, preview_db)
    with rasterio.open(work / "magnitude_geocoded.tif") as magnitude:
        write_preview(work / "image.png", magnitude.read(1, masked=True).filled(0), preview_db)
    viewer = None
    if write_viewer_raw:
        viewer = write_geocoded_frame(
            work / "magnitude_geocoded.tif",
            work,
            source_label=str((final_output or work) / "magnitude_geocoded.tif"),
        )
    return {
        "source": source_record(path),
        "sensor_metadata": sensor_metadata,
        "input_stage": "focused_complex_image",
        "image_shape": shape,
        "viewer_raw_written": write_viewer_raw,
        "scientific_viewer": viewer,
        "preview_stage": "geocoded_magnitude",
        "preview_dynamic_range_db": preview_db,
        "range_compression": "already performed by data producer",
        "azimuth_compression": "already performed by data producer",
        "phase_decoding": "NGA generic unsigned MP convention: radians=code*2*pi/65536",
        "amplitude_calibration": "uncalibrated DN",
        "geocoded": geocoded,
        "native_pixel_coordinates_lon_lat": coords.tolist(),
        "coordinate_order": [
            "upper_left_center",
            "upper_right_center",
            "lower_right_center",
            "lower_left_center",
            "image_center",
        ],
        "georeferencing": "GDAL NITF BLOCKA/GCP/affine metadata; no terrain orthorectification",
    }
