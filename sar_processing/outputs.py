"""Complete output transactions; preserve complex values and separate display scaling."""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import struct
from uuid import uuid4

import numpy as np
from PIL import Image
import rasterio

from .geography import GeoReference
from .model import ComplexArray, ImageGrid, complex_matrix


@contextmanager
def output_transaction(output: Path):
    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}; choose a new run directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / f".{output.name}-{uuid4().hex}"
    # Windows treats mode 0700 specially: it installs an owner-only ACL that
    # survives rename and prevents a desktop user from reading worker outputs.
    # Inherit the destination parent's existing access rules on Windows.
    work.mkdir(mode=0o777 if os.name == "nt" else 0o700)
    try:
        yield work
        work.rename(output)
    finally:
        if work.exists():
            shutil.rmtree(work)


def write_json(path: Path, report: dict) -> None:
    path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def checked_float32(data: np.ndarray) -> np.ndarray:
    """Narrow real/complex products only when the stored values remain finite."""
    dtype = np.complex64 if np.iscomplexobj(data) else np.float32
    try:
        with np.errstate(over="raise", invalid="raise"):
            converted = data.astype(dtype)
    except FloatingPointError as error:
        raise ValueError("Image values exceed the finite float32 output range") from error
    if not np.isfinite(converted).all():
        raise ValueError("Output values must be finite")
    return converted


def validate_magnitude(magnitude: np.ndarray) -> None:
    if (
        magnitude.ndim != 2
        or magnitude.size == 0
        or np.iscomplexobj(magnitude)
        or not np.isfinite(magnitude).all()
        or np.any(magnitude < 0)
    ):
        raise ValueError("Magnitude output requires finite nonnegative real image data")


def write_preview(path: Path, magnitude: np.ndarray, dynamic_range_db: float = 50.0) -> None:
    validate_magnitude(magnitude)
    if not np.isfinite(dynamic_range_db) or not 1 <= dynamic_range_db <= 120:
        raise ValueError("Display dynamic range must be between 1 and 120 dB")
    peak = float(magnitude.max())
    values = magnitude / (peak if peak > 0 else 1.0)
    if max(values.shape) > 1600:
        # Average intensity before reducing resolution, so narrow bright features
        # cannot disappear between a subsampled set of pixels. Never alter IQ data.
        scale = 1600 / max(values.shape)
        size = (
            max(1, int(round(values.shape[1] * scale))),
            max(1, int(round(values.shape[0] * scale))),
        )
        with Image.fromarray((values**2).astype(np.float32)) as intensity:
            with intensity.resize(size, Image.Resampling.BOX) as reduced:
                values = np.sqrt(np.asarray(reduced, dtype=np.float64))
    db = 20 * np.log10(np.maximum(values, 1e-6))
    pixels = (255 * np.clip((db + dynamic_range_db) / dynamic_range_db, 0, 1)).astype(np.uint8)
    with Image.fromarray(pixels) as preview:
        preview.save(path)


def write_raw(path: Path, magnitude: np.ndarray) -> None:
    validate_magnitude(magnitude)
    data = checked_float32(magnitude)
    with path.open("wb") as output:
        output.write(struct.pack("<II", magnitude.shape[1], magnitude.shape[0]))
        output.write(data.astype("<f4", copy=False).tobytes(order="C"))


def write_formed_image(
    work: Path,
    image: ComplexArray,
    grid: ImageGrid,
    reference: GeoReference | None,
    *,
    preview_db: float = 50.0,
) -> None:
    image = complex_matrix(image)
    grid.validate()
    if image.shape != (grid.y_m.size, grid.x_m.size):
        raise ValueError("Image and coordinate dimensions differ")
    image32 = checked_float32(image)
    magnitude = np.abs(image)
    magnitude32 = checked_float32(magnitude)
    if reference is not None:
        reference.validate()
    np.savez_compressed(
        work / "complex_image.npz",
        image=image,
        x_m=grid.x_m,
        y_m=grid.y_m,
        z_m=grid.z_m,
        stage="complex_image",
    )
    write_raw(work / "image.raw", magnitude32)
    write_preview(work / "image.png", magnitude, preview_db)
    # No arbitrary latitude/longitude is assigned to a scene without an origin.
    if reference is not None:
        for filename, data in (
            ("complex_native.tif", image32),
            ("magnitude_native.tif", magnitude32),
        ):
            with rasterio.open(
                work / filename,
                "w",
                driver="GTiff",
                count=1,
                height=image.shape[0],
                width=image.shape[1],
                dtype=data.dtype,
                compress="deflate",
                **reference.writer_options(),
            ) as target:
                target.write(data, 1)
