"""Exercise CLI stage selection and transactions with portable measured-format files."""

import json
from pathlib import Path
import runpy
import sys

import numpy as np
from numpy.testing import assert_allclose, assert_array_equal
import pytest
import rasterio
from rasterio.transform import from_origin
from scipy.io import savemat

from sar_processing import cli
from sar_processing.model import C


def write_gotcha(directory: Path, signal: complex = 1 + 2j) -> Path:
    directory.mkdir()
    positions = np.array([[20.0, -1.0, 10.0], [20.0, 0.0, 10.0], [20.0, 1.0, 10.0]])
    frequency = 9e9 + np.arange(17) * 1e6
    savemat(
        directory / "data_3dsar_pass1_az001_HH.mat",
        {
            "data": {
                "fp": np.full((frequency.size, len(positions)), signal, dtype=complex),
                "freq": frequency,
                "x": positions[:, 0],
                "y": positions[:, 1],
                "z": positions[:, 2],
                "r0": np.linalg.norm(positions, axis=1),
                "th": np.arange(len(positions), dtype=float),
            }
        },
    )
    return directory


def write_raw(path: Path, *, signal: complex = 1 + 2j, stage="raw_echo") -> Path:
    replica = np.array([1, 1j, -1], dtype=complex)
    samples = np.zeros((3, 64), dtype=complex)
    samples[:, 22:25] = signal * replica
    np.savez(
        path,
        stage=stage,
        samples=samples,
        replica=replica,
        sampling_rate_hz=C / 2,
        carrier_hz=1e6,
        receive_start_s=np.zeros(3),
        positions_m=np.array([[20.0, -1, 10], [20, 0, 10], [20, 1, 10]]),
    )
    return path


def write_sandia(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(
        path,
        "w",
        driver="NITF",
        width=5,
        height=4,
        count=2,
        dtype="uint16",
        crs="EPSG:4326",
        transform=from_origin(-106.555, 35.157, 0.002, 0.002),
        ICORDS="D",
        IREP="MULTI",
        ICAT="SAR",
        ISUBCAT="M,P",
        IREPBAND="M,M",
    ) as target:
        target.write(np.arange(1, 21, dtype="uint16").reshape(4, 5), 1)
        target.write(np.full((4, 5), 16384, dtype="uint16"), 2)
    return path


def arguments(source: Path, output: Path, *extra: str) -> list[str]:
    return ["--input", str(source), "--output", str(output), *extra]


def small_grid() -> list[str]:
    return ["--pixels", "3", "--scene-width", "1", "--last-az", "1"]


def read_report(output: Path) -> dict:
    return json.loads((output / "processing_report.json").read_text(encoding="utf-8"))


def assert_rolled_back(parent: Path, output: Path) -> None:
    assert not output.exists()
    assert not list(parent.glob(f".{output.name}-*"))


@pytest.mark.parametrize(
    "suffix, expected", [(".npz", "raw"), (".ntf", "sandia"), (".NTF", "sandia")]
)
def test_detect_format_from_file_suffix(tmp_path, suffix, expected):
    source = tmp_path / f"input{suffix}"
    source.touch()
    assert cli.detect_format(source) == expected


@pytest.mark.parametrize(
    "name, expected",
    [("image.NTF", "sandia"), ("image.ntf", "sandia"), ("data_3dsar_one.mat", "gotcha")],
)
def test_detect_format_searches_nested_directories(tmp_path, name, expected):
    nested = tmp_path / "nested"
    nested.mkdir()
    (nested / name).touch()
    assert cli.detect_format(tmp_path) == expected


@pytest.mark.parametrize("kind", ["missing", "empty", "unknown", "mixed"])
def test_detect_format_does_not_guess_ambiguous_or_unknown_stages(tmp_path, kind):
    source = tmp_path / kind
    if kind in ("empty", "mixed"):
        source.mkdir()
    if kind == "unknown":
        source.write_bytes(b"not a radar format")
    if kind == "mixed":
        (source / "image.ntf").touch()
        (source / "data_3dsar_one.mat").touch()
    with pytest.raises(ValueError, match="Cannot determine input stage"):
        cli.detect_format(source)


@pytest.mark.parametrize("explicit", [False, True])
def test_gotcha_end_to_end_preserves_complex_image_profiles_and_provenance(tmp_path, explicit):
    source = write_gotcha(tmp_path / "input")
    output = tmp_path / "result"
    options = ["--format", "gotcha", "--window", "none", "--upsample", "64"] if explicit else []
    assert cli.main(arguments(source, output, *small_grid(), *options)) == 0
    report = read_report(output)
    assert report["input_stage"] == "gotcha"
    assert report["pulses"] == 3
    assert report["image_shape"] == [3, 3]
    assert report["range_window"] == ("none" if explicit else "hann")
    assert report["range_upsample"] == (64 if explicit else 32)
    assert report["validation"]["direct_sum_relative_l2_error"] < 0.01
    assert report["validation"]["points"] == 9
    assert report["sources"][0]["file"].endswith("az001_HH.mat")
    assert len(report["sources"][0]["sha256"]) == 64
    assert "scene-local" in report["geocoded"]["status"]
    assert report["numpy_version"] == np.__version__
    assert report["rasterio_version"] == rasterio.__version__
    assert report["elapsed_seconds"] >= 0
    with np.load(output / "complex_image.npz", allow_pickle=False) as image:
        assert str(image["stage"]) == "complex_image"
        assert_allclose(image["image"][1, 1], 1 + 2j, atol=1e-12)
        assert_array_equal(image["x_m"], [-0.5, 0, 0.5])
    with np.load(output / "range_profiles.npz", allow_pickle=False) as profiles:
        assert str(profiles["stage"]) == "range_profiles"
        assert profiles["samples"].dtype == np.complex128
        assert profiles["positions_m"].shape == (3, 3)
        assert np.all(profiles["reference_range_m"] > 0)
        assert float(profiles["range_step_m"]) > 0
    assert (output / "image.png").is_file()
    assert (output / "image.raw").stat().st_size == 8 + 9 * 4
    assert not (output / "magnitude_geocoded.tif").exists()


def test_raw_matched_filter_end_to_end_uses_replica_and_geocodes_explicit_origin(tmp_path):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "result"
    assert (
        cli.main(
            arguments(
                source,
                output,
                *small_grid(),
                "--origin-lat",
                "35.157",
                "--origin-lon",
                "-106.555",
                "--origin-height",
                "1600",
                "--x-heading",
                "90",
                "--resolution",
                "0.25",
                "--crs",
                "EPSG:32613",
                "--azimuth-window",
                "none",
            )
        )
        == 0
    )
    report = read_report(output)
    assert report["input_stage"] == "raw"
    assert report["range_window"] == "supplied replica; no additional taper"
    assert "range_upsample" not in report
    assert "validation" not in report
    assert report["scene_origin"]["height_m"] == 1600
    assert report["geocoded"]["crs"] == "EPSG:32613"
    with np.load(output / "range_profiles.npz") as profiles:
        assert_allclose(profiles["samples"][:, 22], 1 + 2j, atol=1e-12)
        assert_array_equal(profiles["reference_range_m"], np.zeros(3))
        assert float(profiles["range_step_m"]) == 1
    with rasterio.open(output / "complex_native.tif") as image:
        assert image.dtypes[0] == "complex64"
        gcps, crs = image.gcps
        assert len(gcps) == 9
        assert crs.to_epsg() == 4326
    with rasterio.open(output / "magnitude_geocoded.tif") as image:
        assert image.crs.to_epsg() == 32613
        assert image.res == (0.25, 0.25)
        assert np.count_nonzero(np.isfinite(image.read(1))) > 0


@pytest.mark.parametrize("directory", [False, True])
@pytest.mark.parametrize("skip_viewer_raw", [False, True])
def test_sandia_real_nitf_files_create_complete_focused_complex_and_geographic_products(
    tmp_path, directory, skip_viewer_raw
):
    first = write_sandia(tmp_path / "source" / "first.NTF")
    if directory:
        write_sandia(tmp_path / "source" / "nested" / "second.ntf")
    source = first.parent if directory else first
    output = tmp_path / "result"
    options = ["--skip-viewer-raw"] if skip_viewer_raw else []
    assert cli.main(arguments(source, output, "--resolution", "100", *options)) == 0
    report = read_report(output)
    assert report["dataset"] == "Sandia"
    assert len(report["images"]) == (2 if directory else 1)
    for item in report["images"]:
        assert item["input_stage"] == "focused_complex_image"
        assert item["range_compression"] == "already performed by data producer"
        assert item["viewer_raw_written"] is not skip_viewer_raw
        target = output / Path(item["source"]["file"]).stem
        assert read_report(target)["source"] == item["source"]
        with rasterio.open(target / "complex_native.tif") as image:
            assert_allclose(image.read(1).imag, np.arange(1, 21).reshape(4, 5))
            assert_allclose(image.read(1).real, 0, atol=1e-12)
        with rasterio.open(target / "magnitude_native.tif") as image:
            assert image.shape == (4, 5)
            assert_array_equal(image.read(1), np.arange(1, 21).reshape(4, 5))
        with rasterio.open(target / "magnitude_geocoded.tif") as image:
            assert image.crs.to_epsg() == 32613
            assert np.isfinite(image.read(1)).any()
        if skip_viewer_raw:
            assert not (target / "image.raw").exists()
        else:
            assert (target / "image.raw").stat().st_size == 8 + 20 * 4
        assert (target / "image.png").is_file()


@pytest.mark.parametrize("format_name", ["gotcha", "raw"])
def test_skip_viewer_raw_is_rejected_for_non_sandia_inputs(tmp_path, capsys, format_name):
    source = (
        write_gotcha(tmp_path / "source")
        if format_name == "gotcha"
        else write_raw(tmp_path / "source.npz")
    )
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, *small_grid(), "--skip-viewer-raw")) == 1
    error = capsys.readouterr().err
    assert "--skip-viewer-raw" in error
    assert "Sandia" in error
    assert_rolled_back(tmp_path, output)


@pytest.mark.parametrize(
    "options, message",
    [
        (["--pixels", "1"], "2-4096"),
        (["--pixels", "4097"], "2-4096"),
        (["--scene-width", "0"], "scene width"),
        (["--scene-width", "nan"], "scene width"),
        (["--z", "inf"], "height"),
        (["--origin-lat", "35"], "Both --origin-lat"),
        (["--origin-lon", "-106"], "Both --origin-lat"),
        (["--origin-lat", "100", "--origin-lon", "-106"], "outside WGS84"),
        (["--autofocus-point", "0", "0", "0"], "only to GOTCHA"),
        (["--upsample", "32"], "only to GOTCHA"),
    ],
)
def test_invalid_raw_options_fail_without_partial_products(tmp_path, capsys, options, message):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, *small_grid(), *options)) == 1
    assert message in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


@pytest.mark.parametrize("format_name", ["auto", "raw"])
def test_missing_input_returns_failure_for_auto_and_explicit_stage(tmp_path, capsys, format_name):
    output = tmp_path / "result"
    assert cli.main(arguments(tmp_path / "missing.npz", output, "--format", format_name)) == 1
    assert "failed" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_existing_output_is_preserved_on_rejected_run(tmp_path, capsys):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "result"
    output.mkdir()
    sentinel = output / "important.txt"
    sentinel.write_text("keep existing output", encoding="utf-8")
    assert cli.main(arguments(source, output, *small_grid())) == 1
    assert "already exists" in capsys.readouterr().err
    assert sentinel.read_text(encoding="utf-8") == "keep existing output"
    assert list(output.iterdir()) == [sentinel]


def test_focused_sandia_cannot_be_range_or_azimuth_compressed_again(tmp_path, capsys):
    source = write_sandia(tmp_path / "source.ntf")
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, "--operation", "form-image")) == 1
    assert "already focused" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_explicit_sandia_empty_directory_fails(tmp_path, capsys):
    source = tmp_path / "source"
    source.mkdir()
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, "--format", "sandia")) == 1
    assert "No Sandia NITF" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_sandia_batch_failure_rolls_back_already_completed_images(tmp_path, capsys):
    source = tmp_path / "source"
    write_sandia(source / "first.ntf")
    (source / "second.ntf").write_bytes(b"broken NITF input")
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, "--resolution", "100")) == 1
    captured = capsys.readouterr()
    assert "Processed Sandia 1/2" in captured.out
    assert "failed" in captured.err
    assert_rolled_back(tmp_path, output)


def test_sandia_duplicate_stems_cannot_collide_or_overwrite_products(tmp_path, capsys):
    source = tmp_path / "source"
    write_sandia(source / "first" / "same.ntf")
    write_sandia(source / "second" / "SAME.ntf")
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, "--resolution", "100")) == 1
    assert "filenames must be unique" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


@pytest.mark.parametrize("format_name", ["gotcha", "raw"])
def test_zero_signal_reconstruction_is_a_failure(tmp_path, capsys, format_name):
    source = (
        write_gotcha(tmp_path / "source", signal=0j)
        if format_name == "gotcha"
        else write_raw(tmp_path / "source.npz", signal=0j)
    )
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, *small_grid())) == 1
    assert "no signal" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


@pytest.mark.parametrize("bad_reference", [0j, complex(np.nan, 0)])
def test_numerical_reference_failure_blocks_publication(
    tmp_path, capsys, monkeypatch, bad_reference
):
    source = write_gotcha(tmp_path / "source")
    output = tmp_path / "result"
    monkeypatch.setattr(
        cli,
        "direct_reference",
        lambda history, points, *options, **kwargs: np.full(len(points), bad_reference),
    )
    assert cli.main(arguments(source, output, *small_grid())) == 1
    assert "Direct reference error" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_corrupt_raw_stage_does_not_silently_become_synthetic_input(tmp_path, capsys):
    source = write_raw(tmp_path / "raw.npz", stage="complex_image")
    output = tmp_path / "result"
    assert cli.main(arguments(source, output, *small_grid())) == 1
    assert "not declared raw_echo" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_write_failure_rolls_back_products_and_returns_error(tmp_path, capsys, monkeypatch):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "result"

    def fail_report(path, report):
        raise OSError("simulated report disk failure")

    monkeypatch.setattr(cli, "write_json", fail_report)
    assert cli.main(arguments(source, output, *small_grid())) == 1
    assert "simulated report disk failure" in capsys.readouterr().err
    assert_rolled_back(tmp_path, output)


def test_python_module_entry_point_executes_cli_and_returns_exit_status(tmp_path, monkeypatch):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "result"
    monkeypatch.setattr(sys, "argv", ["sar_processing", *arguments(source, output, *small_grid())])
    with pytest.raises(SystemExit) as result:
        runpy.run_module("sar_processing", run_name="__main__")
    assert result.value.code == 0
    assert read_report(output)["input_stage"] == "raw"


@pytest.mark.parametrize("interpolation", ["linear", "sinc"])
def test_taylor_weighting_tiling_and_quality_report_match_complex_reference(
    tmp_path, interpolation
):
    source = write_gotcha(tmp_path / "input")
    output = tmp_path / "out"
    assert (
        cli.main(
            arguments(
                source,
                output,
                *small_grid(),
                "--window",
                "taylor",
                "--azimuth-window",
                "taylor",
                "--taylor-sll-db",
                "35",
                "--interpolation",
                interpolation,
                "--tile-pixels",
                "4",
            )
        )
        == 0
    )
    report = read_report(output)
    assert report["interpolation"] == interpolation
    assert report["range_impulse_response"]["peak_sidelobe_db"] < -30
    assert report["range_impulse_response"]["half_power_width"] > 0
    assert report["validation"]["direct_sum_relative_l2_error"] < 0.01
    frame = json.loads((output / "image.sarframe").read_text())
    assert frame["product"] == "scene_local_magnitude"
    assert "geolocation" not in frame


def test_explicit_reference_phase_focus_recovers_injected_pulse_errors(tmp_path):
    from scipy.io import loadmat

    source = write_gotcha(tmp_path / "input")
    path = next(source.glob("*.mat"))
    data = loadmat(path, simplify_cells=True)["data"]
    data["fp"] *= np.exp(1j * np.array([0.0, 0.8, -0.6]))
    savemat(path, {"data": data})
    output = tmp_path / "out"
    assert (
        cli.main(arguments(source, output, *small_grid(), "--autofocus-point", "0", "0", "0")) == 0
    )
    report = read_report(output)["additional_autofocus"]
    assert_allclose(report["phase_error_rad"], [0, 0.8, -0.6], atol=1e-12)
    assert report["aperture_coherence_after"] > report["aperture_coherence_before"]
    with np.load(output / "complex_image.npz") as result:
        assert_allclose(result["image"][1, 1], 1 + 2j, atol=1e-12)


def test_raw_accepts_normalized_weighted_replica_option(tmp_path):
    source = write_raw(tmp_path / "source.npz")
    output = tmp_path / "out"
    assert cli.main(arguments(source, output, *small_grid(), "--window", "taylor")) == 0
    assert "normalized mismatched filter" in read_report(output)["range_compression"]
    assert "mismatched" in read_report(output)["range_window"]
    with np.load(output / "range_profiles.npz") as profiles:
        assert_allclose(profiles["samples"][:, 22], 1 + 2j, atol=1e-12)


@pytest.mark.parametrize(
    "options", [["--window", "taylor"], ["--autofocus-point", "0", "0", "0"], ["--upsample", "32"]]
)
def test_focused_sandia_cannot_silently_ignore_recompression_options(tmp_path, options):
    source = write_sandia(tmp_path / "sample.ntf")
    output = tmp_path / "out"
    assert cli.main(arguments(source, output, *options)) == 1
    assert not output.exists()


@pytest.mark.parametrize("value", ["0", "nan", "121"])
def test_invalid_preview_controls_fail_before_processing(tmp_path, value):
    source = write_raw(tmp_path / "raw.npz")
    output = tmp_path / "out"
    assert cli.main(arguments(source, output, *small_grid(), "--preview-db", value)) == 1
    assert not output.exists()


def test_ellipsoidal_dem_enters_backprojection_and_geographic_products(tmp_path):
    source = write_gotcha(tmp_path / "input")
    dem = tmp_path / "dem.tif"
    with rasterio.open(
        dem,
        "w",
        driver="GTiff",
        width=20,
        height=20,
        count=1,
        dtype="float64",
        crs="EPSG:4326",
        transform=from_origin(-0.001, 0.001, 0.0001, 0.0001),
    ) as raster:
        raster.write(np.zeros((20, 20)), 1)
        raster.set_band_unit(1, "m")
        raster.update_tags(vertical_datum="ellipsoid")
    output = tmp_path / "out"
    assert (
        cli.main(
            arguments(
                source,
                output,
                *small_grid(),
                "--origin-lat",
                "0",
                "--origin-lon",
                "0",
                "--dem",
                str(dem),
                "--dem-height-reference",
                "ellipsoid",
            )
        )
        == 0
    )
    report = read_report(output)
    assert report["terrain_correction"] == "DEM used in image-formation geometry"
    assert report["geocoded"]["terrain_correction"] == "DEM used in image formation"
    assert abs(report["terrain"]["local_z_min_m"]) < 1e-4
    with np.load(output / "complex_image.npz") as image:
        assert image["z_m"].shape == (3, 3)
        assert_allclose(image["image"][1, 1], 1 + 2j, atol=1e-12)
    with rasterio.open(output / "magnitude_geocoded.tif") as image:
        assert "DEM used in image formation" in image.tags()["processing"]
    frame = json.loads((output / "geocoded.sarframe").read_text())
    assert frame["source"] == str(output / "magnitude_geocoded.tif")
    assert "geolocation" in frame


@pytest.mark.parametrize(
    "options",
    [
        ["--dem-height-reference", "ellipsoid"],
        ["--dem", "missing.tif"],
        ["--dem", "missing.tif", "--origin-lat", "0", "--origin-lon", "0"],
    ],
)
def test_terrain_requires_explicit_origin_and_vertical_datum(tmp_path, options):
    source = write_gotcha(tmp_path / "input")
    output = tmp_path / "out"
    assert cli.main(arguments(source, output, *small_grid(), *options)) == 1
    assert not output.exists()


def test_sandia_rejects_unjustified_dem_correction(tmp_path, capsys):
    source = write_sandia(tmp_path / "sample.ntf")
    assert cli.main(arguments(source, tmp_path / "out", "--dem", "missing.tif")) == 1
    assert "sensor model" in capsys.readouterr().err
