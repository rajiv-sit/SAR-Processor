"""Export floating-point geographic products for the native ImGui viewer.

The manifest is published after its payload, inside the processing transaction.
Coordinates are sampled from the source GeoTIFF. Their interpolation residual
describes the display mapping only, not the source's absolute geographic accuracy.
"""

import argparse
from pathlib import Path
import struct
import sys

import numpy as np
import rasterio
from rasterio.windows import Window

from .geography import GeoReference, dataset_reference, pixel_lonlat
from .outputs import output_transaction, write_json


def geolocation_grid(reference: GeoReference, height: int, width: int) -> dict:
    if min(height, width) < 2 or height * width > 64_000_000:
        raise ValueError("Geographic viewer image must be at least 2x2 and at most 64M pixels")
    rows = np.linspace(0, height - 1, min(height, 33))
    cols = np.linspace(0, width - 1, min(width, 33))
    cc, rr = np.meshgrid(cols, rows)
    locations = pixel_lonlat(reference, rr, cc).reshape(len(rows), len(cols), 2)
    cc_mid, rr_mid = np.meshgrid((cols[1:] + cols[:-1]) / 2, (rows[1:] + rows[:-1]) / 2)
    truth = pixel_lonlat(reference, rr_mid, cc_mid).reshape(len(rows) - 1, len(cols) - 1, 2)
    # Unwrap longitude before interpolating a cell crossing the date line.
    continuous = locations.copy()
    continuous[:, :, 0] = np.rad2deg(
        np.unwrap(np.unwrap(np.deg2rad(locations[:, :, 0]), axis=1), axis=0)
    )
    estimate = (
        continuous[:-1, :-1] + continuous[1:, :-1] + continuous[:-1, 1:] + continuous[1:, 1:]
    ) / 4
    dlon = (estimate[:, :, 0] - truth[:, :, 0] + 180) % 360 - 180
    dlat = estimate[:, :, 1] - truth[:, :, 1]
    # Small-angle great-circle residual at cell midpoints (not a global bound).
    residual = 6_371_008.8 * np.hypot(
        np.deg2rad(dlat), np.deg2rad(dlon) * np.cos(np.deg2rad(truth[:, :, 1]))
    )
    return {
        "rows": rows.tolist(),
        "cols": cols.tolist(),
        "longitude": locations[:, :, 0].tolist(),
        "latitude": locations[:, :, 1].tolist(),
        "interpolation_error_m": float(residual.max()),
        "error_scope": "sampled cell-midpoint interpolation residual; absolute accuracy unknown",
    }


def write_geocoded_frame(source_path: Path, work: Path, *, source_label: str | None = None) -> dict:
    """Write into a new private transaction directory; caller publishes that directory."""
    payload_path = work / "geocoded.raw"
    manifest_path = work / "geocoded.sarframe"
    if payload_path.exists() or manifest_path.exists():
        raise FileExistsError("Scientific viewer products already exist")
    with rasterio.open(source_path) as source:
        if source.count != 1 or np.dtype(source.dtypes[0]).kind not in "uif":
            raise ValueError("Viewer requires a single real magnitude raster")
        mapping = geolocation_grid(dataset_reference(source), source.height, source.width)
        manifest = {
            "schema": "sar-scientific-frame-v1",
            "width": source.width,
            "height": source.height,
            "data_file": payload_path.name,
            "product": "geocoded_magnitude",
            "units": "uncalibrated magnitude DN",
            "source": source_label or str(source_path.resolve()),
            "crs": source.crs.to_string() if source.crs else "GCP mapping",
            "geolocation": mapping,
        }
        valid_count = 0
        with payload_path.open("xb") as output:
            output.write(struct.pack("<II", source.width, source.height))
            for row in range(0, source.height, 256):
                window = Window(0, row, source.width, min(256, source.height - row))
                values = source.read(1, window=window, masked=True, out_dtype="float64")
                valid = values.compressed()
                if (
                    not np.isfinite(valid).all()
                    or np.any(valid < 0)
                    or np.any(valid > np.finfo(np.float32).max)
                ):
                    raise ValueError(
                        "Viewer magnitude must be finite, nonnegative, and float32-safe"
                    )
                valid_count += valid.size
                output.write(values.filled(np.nan).astype("<f4").tobytes(order="C"))
        if valid_count == 0:
            raise ValueError("Viewer raster contains no valid pixels")
    write_json(manifest_path, manifest)
    return {"manifest": manifest_path.name, "valid_pixels": valid_count, **manifest}


def export_sequence(source: Path, output: Path) -> int:
    paths = [source] if source.is_file() else sorted(source.rglob("magnitude_geocoded.tif"))
    if not paths:
        raise ValueError("No magnitude_geocoded.tif products found")
    with output_transaction(output) as work:
        frames = []
        for index, path in enumerate(paths, 1):
            folder = work / f"{index:04d}_{path.parent.name}"
            folder.mkdir()
            frame = write_geocoded_frame(path, folder)
            frames.append({"directory": folder.name, **frame})
            print(f"Exported geocoded frame {index}/{len(paths)}: {path.parent.name}", flush=True)
        write_json(work / "sequence.json", {"frames": frames, "stage": "geocoded_magnitude"})
    return len(paths)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        export_sequence(args.input, args.output)
        return 0
    except (OSError, ValueError, rasterio.errors.RasterioError) as error:
        print(f"Viewer export failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
