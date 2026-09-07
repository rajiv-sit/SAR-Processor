"""Add per-scan stage views beside existing scientific frames, without copying finals.

Run ``python -m sar_processing.stages --input <sequence-or-reconstruction>``.
Each scan publishes its new sibling payloads/manifests before ``scan.sarscan``;
existing files are never replaced. Earlier scans remain published if a later
scan fails. Intermediate previews are at most 512 pixels on either axis.
Magnitude reduction averages source intensity in nonoverlapping integer bins;
phase uses the nearest source pixel to each preview-bin centre, without averaging
wrapped angles. Original scientific arrays and final frame payloads are untouched.
"""

import argparse
from contextlib import contextmanager
import json
from math import prod
import os
from pathlib import Path
import shutil
import struct
import sys
from typing import Callable
from uuid import uuid4

import numpy as np
import rasterio
from rasterio.windows import Window

from .gotcha import validate_raw_archive
from .outputs import write_json

MAX_SOURCE_PIXELS = 64_000_000
MAX_MANIFEST_BYTES = 1_048_576
Reader = Callable[[int, int], np.ndarray]


def _sibling(directory: Path, name: object, suffix: str) -> Path:
    if (
        not isinstance(name, str)
        or not name
        or any(char in name for char in "/\\:")
        or name in {".", ".."}
        or Path(name).suffix != suffix
    ):
        raise ValueError("Stage products require plain sibling filenames")
    target = directory / name
    if target.is_symlink() or target.resolve().parent != directory.resolve():
        raise ValueError("Stage sibling must remain inside the scan directory")
    return target


def _read_manifest(path: Path, *, payload_directory: Path | None = None) -> dict:
    if path.stat().st_size > MAX_MANIFEST_BYTES:
        raise ValueError("Frame manifest exceeds 1 MiB")
    metadata = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(metadata, dict) or metadata.get("schema") != "sar-scientific-frame-v1":
        raise ValueError("Expected a scientific frame manifest")
    width, height = metadata.get("width"), metadata.get("height")
    if (
        type(width) is not int
        or type(height) is not int
        or min(width, height) < 1
        or width * height > MAX_SOURCE_PIXELS
        or not isinstance(metadata.get("source"), str)
        or not metadata["source"]
    ):
        raise ValueError("Frame dimensions and source must be valid")
    payload = _sibling(payload_directory or path.parent, metadata.get("data_file"), ".raw")
    with payload.open("rb") as data:
        header = data.read(8)
    if len(header) != 8 or struct.unpack("<II", header) != (width, height):
        raise ValueError("Final frame payload dimensions do not match its manifest")
    if payload.stat().st_size != 8 + width * height * 4:
        raise ValueError("Final frame payload size does not match its manifest")
    return metadata


def _shape(
    source_shape: tuple[int, int], maximum: int, preserve_aspect: bool = True
) -> tuple[int, int]:
    if len(source_shape) != 2 or min(source_shape) < 1 or prod(source_shape) > MAX_SOURCE_PIXELS:
        raise ValueError("Stage source must be a nonempty 2D grid of at most 64 million pixels")
    if not preserve_aspect:
        return min(source_shape[0], maximum), min(source_shape[1], maximum)
    scale = min(1.0, maximum / max(source_shape))
    return max(1, int(source_shape[0] * scale)), max(1, int(source_shape[1] * scale))


def _preview(
    reader: Reader,
    source_shape: tuple[int, int],
    maximum: int,
    phase: bool,
    *,
    preserve_aspect: bool = True,
) -> np.ndarray:
    height, width = _shape(source_shape, maximum, preserve_aspect)
    result = np.full((height, width), np.nan, dtype=np.float32)
    row_edges = np.linspace(0, source_shape[0], height + 1).astype(np.intp)
    col_edges = np.linspace(0, source_shape[1], width + 1).astype(np.intp)
    columns = ((col_edges[:-1] + col_edges[1:] - 1) // 2).astype(np.intp)
    for row in range(height):
        start, stop = int(row_edges[row]), int(row_edges[row + 1])
        if phase:
            start, stop = (start + stop - 1) // 2, (start + stop - 1) // 2 + 1
        values = np.ma.asarray(reader(start, stop))
        if values.shape != (stop - start, source_shape[1]):
            raise ValueError("Stage reader returned an inconsistent row shape")
        valid = ~np.ma.getmaskarray(values)
        checked = values.data[valid]
        if not np.isfinite(checked).all():
            raise ValueError("Stage source contains nonfinite unmasked values")
        if phase:
            if not np.iscomplexobj(values):
                raise ValueError("Phase preview requires complex source samples")
            selected = values.data[0, columns]
            usable = valid[0, columns] & (np.abs(selected) > 0)
            result[row, usable] = np.angle(selected[usable]).astype(np.float32)
        else:
            if not np.iscomplexobj(values) and np.any(checked < 0):
                raise ValueError("Stage magnitude must be nonnegative")
            magnitude = np.abs(values.data).astype(np.float64)
            if np.any(magnitude[valid] > np.finfo(np.float32).max):
                raise ValueError("Stage magnitude exceeds float32 range")
            power = np.where(valid, magnitude, 0.0) ** 2
            sums = np.add.reduceat(power.sum(axis=0), col_edges[:-1])
            counts = np.add.reduceat(valid.sum(axis=0), col_edges[:-1])
            usable = counts > 0
            result[row, usable] = np.sqrt(sums[usable] / counts[usable]).astype(np.float32)
    if not np.isfinite(result).any():
        raise ValueError("Stage preview contains no defined, valid samples")
    return result


def _write_stage(
    work: Path,
    name: str,
    source: Path,
    values: np.ndarray,
    source_shape: tuple[int, int],
    product: str,
    *,
    phase: bool = False,
    axes: dict | None = None,
) -> str:
    payload = f"stage_{name}.raw"
    manifest_name = f"stage_{name}.sarframe"
    with (work / payload).open("xb") as output:
        output.write(struct.pack("<II", values.shape[1], values.shape[0]))
        output.write(values.astype("<f4").tobytes(order="C"))
    manifest = {
        "schema": "sar-scientific-frame-v1",
        "width": values.shape[1],
        "height": values.shape[0],
        "data_file": payload,
        "product": product,
        "units": "radians" if phase else "uncalibrated magnitude DN",
        "display_scale": "phase_radians" if phase else "amplitude_db",
        "source": str(source.resolve()),
        "source_shape": list(source_shape),
        "display_only": True,
        "preview_resampling": "nearest source sample to integer-bin centre; zero amplitude has undefined phase"
        if phase
        else "sqrt(mean(abs(source)**2)) over valid samples in nonoverlapping integer bins",
        "source_pixel_mapping": "integer bin edges floor(linspace(0, source_size, preview_size+1))",
    }
    if axes is not None:
        manifest["axes"] = axes
    write_json(work / manifest_name, manifest)
    return manifest_name


def _available(stage_id: str, label: str, manifest: str) -> dict:
    return {"id": stage_id, "label": label, "status": "available", "manifest": manifest}


def _unavailable(stage_id: str, label: str, reason: str) -> dict:
    return {"id": stage_id, "label": label, "status": "unavailable", "reason": reason}


def _sandia_stages(frame: Path, metadata: dict, work: Path, maximum: int) -> list[dict]:
    source = Path(metadata["source"])
    if (
        metadata.get("product") != "geocoded_magnitude"
        or not source.is_absolute()
        or source.name != "magnitude_geocoded.tif"
    ):
        raise ValueError("Sandia stages require a geographic frame with an absolute source GeoTIFF")
    reason = (
        "Sandia supplied an already-focused complex image; this acquisition stage is unavailable"
    )
    stages = [
        _unavailable("range", "Range compression", reason),
        _unavailable("backprojection", "Backprojection / azimuth compression", reason),
    ]
    native_shape = None
    for name, label, filename, phase in (
        ("focused_magnitude", "Focused native magnitude", "magnitude_native.tif", False),
        ("focused_phase", "Focused native phase", "complex_native.tif", True),
    ):
        path = source.parent / filename
        with rasterio.open(path) as raster:
            if raster.count != 1:
                raise ValueError("Stage GeoTIFF must have one band")
            shape = raster.shape
            if native_shape is not None and native_shape != shape:
                raise ValueError("Native magnitude and complex stage shapes differ")
            native_shape = shape

            def reader(start: int, stop: int) -> np.ndarray:
                return raster.read(
                    1, window=Window(0, start, raster.width, stop - start), masked=True
                )

            values = _preview(reader, shape, maximum, phase)
        manifest = _write_stage(work, name, path, values, shape, name, phase=phase)
        stages.append(_available(name, label, manifest))
    stages.append(_available("geocoded", "Geocoded magnitude", frame.name))
    return stages


def _gotcha_stages(frame: Path, metadata: dict, work: Path, maximum: int) -> list[dict]:
    if metadata.get("product") not in {
        "scene_local_magnitude",
        "backprojected_magnitude",
        "geocoded_magnitude",
    }:
        raise ValueError("Backprojection stages require the existing formed image frame")
    ranges = frame.parent / "range_profiles.npz"
    image_path = frame.parent / "complex_image.npz"
    validate_raw_archive(ranges)
    validate_raw_archive(image_path)
    with np.load(ranges, allow_pickle=False) as archive:
        samples = archive["samples"]
        starts = archive["range_start_m"]
        step = archive["range_step_m"].item()
        if (
            archive["stage"].item() != "range_profiles"
            or samples.ndim != 2
            or not np.iscomplexobj(samples)
            or starts.shape != (samples.shape[0],)
            or starts.dtype.kind not in "fiu"
            or not np.isfinite(starts).all()
            or not np.isrealobj(step)
            or not np.isfinite(step)
            or step <= 0
        ):
            raise ValueError("Range archive requires complex profiles and valid metre range axes")
        shape = samples.shape
        values = _preview(
            lambda first, last: samples[first:last], shape, maximum, False, preserve_aspect=False
        )
        axes = {
            "display_aspect": "independent axes: pulse index and range bins have different units",
            "rows": {"label": "Pulse index", "units": "index", "source_extent": [0, shape[0] - 1]},
            "columns": {
                "label": "Range-bin index",
                "units": "index",
                "source_extent": [0, shape[1] - 1],
            },
            "differential_range": {
                "units": "m",
                "sample_step": float(step),
                "start_min": float(starts.min()),
                "start_max": float(starts.max()),
                "end_min": float(starts.min() + (shape[1] - 1) * step),
                "end_max": float(starts.max() + (shape[1] - 1) * step),
                "reference": "per-pulse reference range; see original range_profiles.npz",
                "scope": "range bins are aligned by column; physical starts may vary by pulse",
            },
        }
        manifest = _write_stage(
            work, "range", ranges, values, shape, "range_profile_magnitude", axes=axes
        )
    stages = [_available("range", "Range compression: pulse versus differential range", manifest)]
    with np.load(image_path, allow_pickle=False) as archive:
        samples = archive["image"]
        if archive["stage"].item() != "complex_image":
            raise ValueError("Image archive is not a formed complex image")
        shape = samples.shape
        if metadata["product"] == "geocoded_magnitude":
            _shape(shape, maximum)
            formed = {
                "schema": "sar-scientific-frame-v1",
                "width": shape[1],
                "height": shape[0],
                "data_file": "image.raw",
                "source": str(image_path.resolve()),
                "product": "backprojected_magnitude",
                "units": "uncalibrated magnitude DN",
                "display_scale": "amplitude_db",
                "source_shape": list(shape),
                "display_only": False,
                "preview_resampling": "none; existing full-resolution formed magnitude payload",
            }
            backprojection = "stage_backprojection.sarframe"
            write_json(work / backprojection, formed)
            _read_manifest(work / backprojection, payload_directory=frame.parent)
        elif shape != (metadata["height"], metadata["width"]):
            raise ValueError("Complex image and final frame shapes differ")
        else:
            backprojection = frame.name
        stages.append(_available("backprojection", "Backprojected magnitude", backprojection))
        phase = _preview(lambda first, last: samples[first:last], shape, maximum, True)
        manifest = _write_stage(
            work, "backprojected_phase", image_path, phase, shape, "backprojected_phase", phase=True
        )
    stages.append(_available("backprojected_phase", "Backprojected phase", manifest))
    if (frame.parent / "geocoded.sarframe").is_file():
        _read_manifest(frame.parent / "geocoded.sarframe")
        stages.append(_available("geocoded", "Geocoded magnitude", "geocoded.sarframe"))
    else:
        stages.append(
            _unavailable(
                "geocoded",
                "Geocoded magnitude",
                "No verified Earth origin; reconstruction is scene-local",
            )
        )
    return stages


@contextmanager
def _stage_workspace(directory: Path):
    parent = directory.resolve()
    work = parent / f".scan-stages-{uuid4().hex}"
    # Windows 0700 adds an owner-only DACL. Stage files must inherit the final
    # directory's desktop-readable DACL even while being prepared privately.
    work.mkdir(mode=0o777 if os.name == "nt" else 0o700)
    try:
        yield work
    finally:
        if work.resolve().parent != parent:
            raise OSError("Stage workspace escaped its intended parent")
        shutil.rmtree(work)


def export_scan(frame: Path, *, max_pixels: int = 512) -> Path:
    """Publish one scan bundle in place; roll back only this call's new products."""
    if type(max_pixels) is not int or not 2 <= max_pixels <= 512:
        raise ValueError("Intermediate preview maximum must be an integer in [2, 512]")
    frame = _sibling(frame.parent, frame.name, ".sarframe")
    metadata = _read_manifest(frame)
    target = frame.parent / "scan.sarscan"
    if target.exists():
        raise FileExistsError("Scan stages already exist; originals are preserved")
    with _stage_workspace(frame.parent) as work:
        gotcha = (frame.parent / "range_profiles.npz").is_file()
        stages = (
            _gotcha_stages(frame, metadata, work, max_pixels)
            if gotcha
            else _sandia_stages(frame, metadata, work, max_pixels)
        )
        total_pixels = 0
        for stage in stages:
            if stage["status"] == "available":
                name = stage["manifest"]
                item = _read_manifest(
                    work / name if (work / name).exists() else frame.parent / name,
                    payload_directory=frame.parent
                    if name == "stage_backprojection.sarframe"
                    else None,
                )
                total_pixels += item["width"] * item["height"]
        if total_pixels > MAX_SOURCE_PIXELS:
            raise ValueError("Available scan stages exceed the 64 million pixel budget")
        default = (
            "geocoded"
            if any(s["id"] == "geocoded" and s["status"] == "available" for s in stages)
            else "backprojection"
        )
        write_json(
            work / target.name,
            {
                "schema": "sar-scientific-scan-v1",
                "label": frame.parent.name,
                "source": metadata["source"],
                "default_stage": default,
                "stages": stages,
            },
        )
        files = sorted(p for p in work.iterdir() if p.name != target.name) + [work / target.name]
        destinations = [frame.parent / p.name for p in files]
        if any(p.exists() for p in destinations):
            raise FileExistsError("Stage products already exist; originals are preserved")
        published: list[Path] = []
        try:
            for path, destination in zip(files, destinations, strict=True):
                # Same-directory hard links publish atomically and fail if a target
                # appears concurrently, unlike POSIX rename which can replace it.
                os.link(path, destination)
                published.append(destination)
        except OSError:
            for path in reversed(published):
                path.unlink()
            raise
    return target


def export_stages(source: Path, *, max_pixels: int = 512) -> int:
    """Add stages for one frame, a GOTCHA reconstruction, or a Sandia sequence."""
    if source.is_file():
        frames = [source]
    elif (source / "range_profiles.npz").is_file():
        frames = [
            source
            / ("image.sarframe" if (source / "image.sarframe").is_file() else "geocoded.sarframe")
        ]
    else:
        selected = {path.parent: path for path in source.rglob("geocoded.sarframe")}
        for path in source.rglob("image.sarframe"):
            if (path.parent / "range_profiles.npz").is_file():
                selected[path.parent] = path
        frames = sorted(selected.values())
    if not frames:
        raise ValueError("No existing scientific scan products found")
    for index, frame in enumerate(frames, 1):
        result = export_scan(frame, max_pixels=max_pixels)
        print(f"Exported stages {index}/{len(frames)}: {result}", flush=True)
    return len(frames)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--max-pixels", type=int, default=512)
    args = parser.parse_args(argv)
    try:
        export_stages(args.input, max_pixels=args.max_pixels)
        return 0
    except (OSError, ValueError, TypeError, KeyError, rasterio.errors.RasterioError) as error:
        print(f"Stage export failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
