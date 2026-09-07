"""Command-line pipeline that respects the processing stage of each dataset."""

import argparse
from dataclasses import asdict
from pathlib import Path
import sys
import time

import numpy as np
import rasterio

from .autofocus import focus_reference_point
from .geography import SceneOrigin, geocode_magnitude, scene_reference
from .gotcha import load_gotcha, load_raw_echo, source_record
from .model import C, ImageGrid
from .outputs import output_transaction, write_formed_image, write_json, write_preview
from .processing import (
    backproject_profiles,
    compress_phase_history,
    compress_raw_echo,
    direct_reference,
    uniform_frequency,
    window_weights,
)
from .quality import impulse_response_metrics
from .sandia import process_sandia
from .viewer import write_geocoded_frame
from .terrain import scene_height_grid


def detect_format(path: Path) -> str:
    if path.is_dir():
        sandia = any(p.is_file() and p.suffix.lower() == ".ntf" for p in path.rglob("*"))
        gotcha = any(path.rglob("data_3dsar_*.mat"))
        if sandia != gotcha:
            return "sandia" if sandia else "gotcha"
    elif path.is_file():
        if path.suffix.lower() == ".ntf":
            return "sandia"
        if path.suffix.lower() == ".npz":
            return "raw"
    raise ValueError("Cannot determine input stage; select --format or provide supported input")


def form_image(args, work: Path) -> dict:
    if not 2 <= args.pixels <= 4096 or not np.isfinite(args.scene_width) or args.scene_width <= 0:
        raise ValueError("Require 2-4096 pixels and a positive finite scene width")
    axis = np.linspace(-args.scene_width / 2, args.scene_width / 2, args.pixels)
    grid = ImageGrid(axis, axis, args.z)
    grid.validate()
    if (args.origin_lat is None) != (args.origin_lon is None):
        raise ValueError("Both --origin-lat and --origin-lon are required for scene geocoding")
    origin = None
    if args.origin_lat is not None:
        origin = SceneOrigin(args.origin_lat, args.origin_lon, args.origin_height, args.x_heading)
        origin.validate()
    if args.dem is not None:
        if origin is None or args.dem_height_reference != "ellipsoid":
            raise ValueError(
                "DEM processing requires a verified scene origin and --dem-height-reference ellipsoid"
            )
        grid.z_m = scene_height_grid(
            grid, origin, args.dem, height_reference=args.dem_height_reference
        )
        grid.validate()
    report = {
        "input_stage": args.format,
        "azimuth_window": args.azimuth_window,
        "scene_z_m": args.z,
        "interpolation": args.interpolation,
        "sinc_half_width": args.sinc_half_width,
        "tile_pixels": args.tile_pixels,
        "preview_dynamic_range_db": args.preview_db,
        "taylor": {"nbar": args.taylor_nbar, "sidelobe_setting_db": args.taylor_sll_db},
    }
    taper_options = {"taylor_nbar": args.taylor_nbar, "taylor_sll_db": args.taylor_sll_db}
    if args.dem is not None:
        report["terrain"] = {
            "source": source_record(args.dem),
            "height_reference": "WGS84 ellipsoid metres",
            "local_z_min_m": float(np.min(grid.z_m)),
            "local_z_max_m": float(np.max(grid.z_m)),
            "scope": "DEM surface used in pulse-to-pixel geometry; no atmospheric or radiometric correction",
        }
    history = None
    if args.format == "gotcha":
        args.window = args.window or "hann"
        args.upsample = 32 if args.upsample is None else args.upsample
        history = load_gotcha(
            args.input, args.first_az, args.last_az, args.pass_number, args.polarization
        )
        autofocus_report: dict = {"status": "not applied"}
        if args.autofocus_point is not None:
            focused = focus_reference_point(history, np.asarray(args.autofocus_point))
            history = focused.history
            autofocus_report = {
                "status": "reference-point constant phase correction",
                "reference_point_m": args.autofocus_point,
                "assumption": "independently identified isolated stationary phase-stable target",
                "phase_error_rad": focused.phase_error_rad.tolist(),
                "frequency_coherence": focused.frequency_coherence.tolist(),
                "aperture_coherence_before": focused.aperture_coherence_before,
                "aperture_coherence_after": focused.aperture_coherence_after,
            }
        profiles = compress_phase_history(history, args.upsample, args.window, **taper_options)
        _, step, fit_error = uniform_frequency(history.frequency_hz)
        report.update(
            range_window=args.window,
            range_upsample=args.upsample,
            sources=history.sources,
            frequency_step_hz=step,
            frequency_grid_max_fit_error_hz=fit_error,
            bandwidth_hz=float(np.ptp(history.frequency_hz)),
            nominal_c_over_2B_m=C / float(2 * np.ptp(history.frequency_hz)),
            additional_autofocus=autofocus_report,
            pass_number=args.pass_number,
            polarization=args.polarization,
            first_az=args.first_az,
            last_az=args.last_az,
            range_compression="IFFT of frequency-domain phase history",
        )
        if history.frequency_hz.size >= 4:
            nfft = 1 << (history.frequency_hz.size * 64 - 1).bit_length()
            weights = window_weights(history.frequency_hz.size, args.window, **taper_options)
            response = np.fft.fftshift(np.fft.ifft(weights, n=nfft))
            report["range_impulse_response"] = {
                **asdict(impulse_response_metrics(response, C / (2 * step * nfft))),
                "width_units": "slant-range metres",
                "scope": "ideal isolated-target spectral-window response; not measured scene quality",
            }
    else:
        if args.upsample is not None or args.autofocus_point is not None:
            raise ValueError(
                "--upsample and --autofocus-point apply only to GOTCHA phase histories"
            )
        raw = load_raw_echo(args.input)
        profiles = compress_raw_echo(raw, window=args.window or "none", **taper_options)
        report.update(
            sources=[source_record(args.input)],
            range_compression=(
                "linear matched filter using supplied replica"
                if args.window in (None, "none")
                else "linear normalized mismatched filter using weighted supplied replica"
            ),
            range_window=(
                "supplied replica; no additional taper"
                if args.window in (None, "none")
                else f"{args.window} weighted replica; normalized mismatched filter"
            ),
        )
    print(
        f"Backprojecting {profiles.samples.shape[0]} pulses onto {args.pixels}x{args.pixels} grid",
        flush=True,
    )
    image = backproject_profiles(
        profiles,
        grid,
        args.azimuth_window,
        interpolation=args.interpolation,
        sinc_half_width=args.sinc_half_width,
        tile_pixels=args.tile_pixels,
        **taper_options,
    )
    if not np.any(image):
        raise ValueError("Reconstruction contains no signal")
    if history is not None:
        indices = np.unique(np.linspace(0, args.pixels - 1, 5, dtype=int))
        pixels = {(int(y), int(x)) for y in indices for x in indices}
        strongest = np.argsort(np.abs(image).ravel())[-min(25, image.size) :]
        for index in strongest:
            row, col = np.unravel_index(index, image.shape)
            pixels.add((int(row), int(col)))
        ordered = sorted(pixels)
        points = np.array([(axis[x], axis[y], grid.height_at(y, x)) for y, x in ordered])
        expected = direct_reference(
            history, points, args.window or "hann", args.azimuth_window, **taper_options
        )
        actual = np.array([image[y, x] for y, x in ordered])
        relative_error = float(
            float(np.linalg.norm(actual - expected)) / max(float(np.linalg.norm(expected)), 1e-30)
        )
        if not np.isfinite(relative_error) or relative_error > 0.01:
            raise ValueError(
                f"Direct reference error {relative_error:.4%} exceeds 1%; increase upsampling"
            )
        report["validation"] = {
            "direct_sum_relative_l2_error": relative_error,
            "points": len(points),
            "limit": 0.01,
            "scope": "numerical consistency, not external scene ground truth",
        }
    reference = None
    if origin is not None:
        reference = scene_reference(grid, origin)
        report["scene_origin"] = asdict(origin)
    write_formed_image(work, image, grid, reference, preview_db=args.preview_db)
    if reference is not None:
        report["geocoded"] = geocode_magnitude(
            work / "magnitude_native.tif",
            work / "magnitude_geocoded.tif",
            reference,
            args.resolution,
            args.crs,
            terrain_applied=args.dem is not None,
        )
        with rasterio.open(work / "magnitude_geocoded.tif") as geographic:
            write_preview(
                work / "image_geocoded.png",
                geographic.read(1, masked=True).filled(0),
                args.preview_db,
            )
        report["scientific_viewer"] = write_geocoded_frame(
            work / "magnitude_geocoded.tif",
            work,
            source_label=str(args.output.resolve() / "magnitude_geocoded.tif"),
        )
    else:
        report["geocoded"] = {"status": "scene-local coordinates; geographic origin not supplied"}
        write_json(
            work / "image.sarframe",
            {
                "schema": "sar-scientific-frame-v1",
                "width": image.shape[1],
                "height": image.shape[0],
                "data_file": "image.raw",
                "product": "scene_local_magnitude",
                "units": "uncalibrated magnitude DN",
                "source": str(args.output.resolve() / "complex_image.npz"),
            },
        )
    np.savez_compressed(
        work / "range_profiles.npz",
        samples=profiles.samples,
        range_start_m=profiles.range_start_m,
        range_step_m=profiles.range_step_m,
        reference_range_m=profiles.reference_range_m,
        positions_m=profiles.positions_m,
        carrier_hz=profiles.carrier_hz,
        stage="range_profiles",
    )
    report.update(
        image_shape=list(image.shape),
        pulses=profiles.samples.shape[0],
        pixel_spacing_m=float(axis[1] - axis[0]),
        azimuth_compression="coherent geometric backprojection with complex interpolation",
        amplitude_calibration="uncalibrated",
        terrain_correction=("DEM used in image-formation geometry" if args.dem else "not applied"),
    )
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--format", choices=("auto", "gotcha", "sandia", "raw"), default="auto")
    parser.add_argument("--operation", choices=("auto", "form-image"), default="auto")
    parser.add_argument(
        "--skip-viewer-raw",
        action="store_true",
        help="Sandia: omit the redundant full-size raw viewer file to save disk space",
    )
    parser.add_argument("--first-az", type=int, default=1)
    parser.add_argument("--last-az", type=int, default=4)
    parser.add_argument("--pass-number", type=int, default=1)
    parser.add_argument("--polarization", choices=("HH", "HV", "VH", "VV"), default="HH")
    parser.add_argument("--pixels", type=int, default=512)
    parser.add_argument("--scene-width", type=float, default=100)
    parser.add_argument("--z", type=float, default=0)
    parser.add_argument("--upsample", type=int, help="GOTCHA IFFT upsampling; default 32")
    parser.add_argument(
        "--window",
        choices=("hann", "none", "taylor"),
        help="Range weighting: GOTCHA default hann; raw default none",
    )
    parser.add_argument("--azimuth-window", choices=("hann", "none", "taylor"), default="hann")
    parser.add_argument("--taylor-nbar", type=int, default=4)
    parser.add_argument("--taylor-sll-db", type=float, default=35.0)
    parser.add_argument("--interpolation", choices=("linear", "sinc"), default="linear")
    parser.add_argument("--sinc-half-width", type=int, default=8)
    parser.add_argument("--tile-pixels", type=int, default=65536)
    parser.add_argument(
        "--autofocus-point",
        type=float,
        nargs=3,
        metavar=("X", "Y", "Z"),
        help="Known isolated phase-stable GOTCHA reference target in scene metres",
    )
    parser.add_argument("--preview-db", type=float, default=50.0)
    parser.add_argument(
        "--dem",
        type=Path,
        help="DEM GeoTIFF with metre band units and verified ellipsoidal heights",
    )
    parser.add_argument(
        "--dem-height-reference",
        choices=("ellipsoid",),
        help="Explicit assertion of the DEM vertical datum; requires --dem",
    )
    parser.add_argument(
        "--resolution", type=float, default=0.25, help="Geocoded output pixel spacing in metres"
    )
    parser.add_argument(
        "--crs", help="Projected destination CRS in metres; default local WGS84 UTM"
    )
    parser.add_argument("--origin-lat", type=float)
    parser.add_argument("--origin-lon", type=float)
    parser.add_argument(
        "--origin-height",
        type=float,
        default=0,
        help="Scene origin WGS84 ellipsoid height in metres",
    )
    parser.add_argument(
        "--x-heading", type=float, default=90, help="Scene x-axis heading clockwise from north"
    )
    args = parser.parse_args(argv)
    try:
        started = time.perf_counter()
        if args.format == "auto":
            args.format = detect_format(args.input)
        if not args.input.exists():
            raise ValueError("Input does not exist")
        if args.skip_viewer_raw and args.format != "sandia":
            raise ValueError("--skip-viewer-raw applies only to Sandia image imports")
        if args.format == "sandia" and args.operation == "form-image":
            raise ValueError(
                "Sandia input is already focused; range/azimuth compression would process it twice"
            )
        if args.format == "sandia" and (
            args.window is not None or args.autofocus_point is not None or args.upsample is not None
        ):
            raise ValueError("Range weighting and autofocus require phase histories or raw echoes")
        if args.dem is not None and args.format == "sandia":
            raise ValueError(
                "Sandia terrain correction requires a verified sensor model and MSL geoid; corner reprojection is insufficient"
            )
        if args.dem_height_reference is not None and args.dem is None:
            raise ValueError("--dem-height-reference requires --dem")
        if not np.isfinite(args.preview_db) or not 1 <= args.preview_db <= 120:
            raise ValueError("Preview dynamic range must be between 1 and 120 dB")
        with output_transaction(args.output) as work:
            report: dict
            if args.format == "sandia":
                paths = (
                    sorted(
                        p
                        for p in args.input.rglob("*")
                        if p.is_file() and p.suffix.lower() == ".ntf"
                    )
                    if args.input.is_dir()
                    else [args.input]
                )
                if not paths:
                    raise ValueError("No Sandia NITF images found")
                if len({path.stem.lower() for path in paths}) != len(paths):
                    raise ValueError("Sandia image filenames must be unique across input folders")
                images = []
                for index, path in enumerate(paths):
                    target = work / path.stem
                    target.mkdir()
                    result = process_sandia(
                        path,
                        target,
                        args.resolution,
                        args.crs,
                        write_viewer_raw=not args.skip_viewer_raw,
                        preview_db=args.preview_db,
                        final_output=args.output.resolve() / path.stem,
                    )
                    write_json(target / "processing_report.json", result)
                    images.append(result)
                    print(f"Processed Sandia {index + 1}/{len(paths)}: {path.name}", flush=True)
                report = {"dataset": "Sandia", "images": images}
            else:
                report = form_image(args, work)
            report.update(
                elapsed_seconds=time.perf_counter() - started,
                numpy_version=np.__version__,
                rasterio_version=rasterio.__version__,
                gdal_version=rasterio.__gdal_version__,
            )
            write_json(work / "processing_report.json", report)
        print(f"Saved {args.output}", flush=True)
        return 0
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        EOFError,
        rasterio.errors.RasterioError,
    ) as error:
        print(f"SAR processing failed: {error}", file=sys.stderr)
        return 1
