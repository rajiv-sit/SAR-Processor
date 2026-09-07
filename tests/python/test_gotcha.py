"""Portable MAT/NPZ import fixtures; no downloaded datasets are required."""

import hashlib
from io import BytesIO
from zipfile import ZIP_DEFLATED, ZipFile

import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal
from scipy.io import savemat

from sar_processing import gotcha
from sar_processing.gotcha import load_gotcha, load_raw_echo, source_record


def mat_data(pulses=3, offset=0.0):
    frequency = 9e9 + 1e6 * np.arange(5)
    real = np.arange(5 * pulses).reshape(5, pulses) + offset
    return {
        "fp": real + 1j * (real + 100),
        "freq": frequency,
        "x": 1000.0 + np.arange(pulses),
        "y": 200.0 + np.arange(pulses),
        "z": np.full(pulses, 500.0),
        "r0": np.full(pulses, 1100.0),
        "th": np.arange(pulses) + offset,
    }


def write_mat(directory, azimuth=1, data=None, pass_number=1, polarization="HH"):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"data_3dsar_pass{pass_number}_az{azimuth:03d}_{polarization}.mat"
    savemat(path, {"data": mat_data() if data is None else data})
    return path


def test_loads_apertures_in_azimuth_order_and_transposes_frequency_pulse_layout(tmp_path):
    first = mat_data(offset=10)
    second = mat_data(offset=20)
    second_path = write_mat(tmp_path / "b", azimuth=2, data=second)
    first_path = write_mat(tmp_path / "a", azimuth=1, data=first)
    history = load_gotcha(tmp_path, 1, 2)
    assert history.samples.shape == (6, 5)
    assert history.samples.dtype == np.complex128
    assert_array_equal(history.samples, np.concatenate([first["fp"].T, second["fp"].T]))
    assert_array_equal(history.frequency_hz, first["freq"])
    assert_array_equal(history.azimuth_deg, np.concatenate([first["th"], second["th"]]))
    assert_array_equal(history.reference_range_m, np.full(6, 1100))
    assert_array_equal(
        history.positions_m[:3], np.column_stack([first[k] for k in ("x", "y", "z")])
    )
    for record, path in zip(history.sources, [first_path, second_path], strict=True):
        assert {key: record[key] for key in ("file", "bytes", "sha256")} == {
            "file": path.name,
            "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
        assert record["vendor_autofocus"]["applied_by_importer"] is False


def test_vendor_correction_vectors_are_preserved_without_guessing_application(tmp_path):
    data = mat_data()
    original = data["fp"].copy()
    data["af"] = {"r_correct": np.array([1.0, 2.0, 3.0]), "ph_correct": np.array([0.1, 0.2, 0.3])}
    write_mat(tmp_path, data=data)
    result = load_gotcha(tmp_path, 1, 1)
    assert_array_equal(result.samples, original.T)
    report = result.sources[0]["vendor_autofocus"]
    assert report["applied_by_importer"] is False
    assert_array_equal(report["fields"]["ph_correct"]["values"], [0.1, 0.2, 0.3])


@pytest.mark.parametrize("polarization", ["HH", "HV", "VH", "VV"])
def test_single_pulse_mat_squeeze_is_restored(tmp_path, polarization):
    data = mat_data(pulses=1)
    write_mat(tmp_path, azimuth=360, data=data, pass_number=2, polarization=polarization)
    history = load_gotcha(tmp_path, 360, 360, pass_number=2, polarization=polarization)
    assert history.samples.shape == (1, 5)
    assert history.positions_m.shape == (1, 3)
    assert_array_equal(history.samples, data["fp"].T)
    assert_array_equal(history.reference_range_m, data["r0"])


@pytest.mark.parametrize(
    "kwargs",
    [
        {"first_az": 0},
        {"first_az": 3, "last_az": 2},
        {"last_az": 361},
        {"pass_number": 0},
        {"polarization": "XX"},
    ],
)
def test_invalid_aperture_selection(tmp_path, kwargs):
    with pytest.raises(ValueError):
        load_gotcha(tmp_path, **kwargs)


def test_nonexistent_directory_is_rejected(tmp_path):
    with pytest.raises(ValueError, match="directory"):
        load_gotcha(tmp_path / "missing")


def test_missing_aperture_is_rejected_with_expected_filename(tmp_path):
    write_mat(tmp_path)
    with pytest.raises(ValueError, match=r"az002_HH.mat, found 0"):
        load_gotcha(tmp_path, 1, 2)


def test_duplicate_aperture_is_rejected_even_if_both_files_are_valid(tmp_path):
    write_mat(tmp_path / "first")
    write_mat(tmp_path / "second")
    with pytest.raises(ValueError, match="found 2"):
        load_gotcha(tmp_path, 1, 1)


@pytest.mark.parametrize("missing", ["fp", "freq", "x", "y", "z", "r0", "th"])
def test_missing_mat_metadata_is_rejected(tmp_path, missing):
    data = mat_data()
    del data[missing]
    write_mat(tmp_path, data=data)
    with pytest.raises(ValueError, match="Missing GOTCHA metadata"):
        load_gotcha(tmp_path, 1, 1)


def test_mat_without_data_structure_is_rejected(tmp_path):
    path = write_mat(tmp_path)
    savemat(path, {"unrelated": np.ones(3)})
    with pytest.raises(ValueError, match="Missing GOTCHA metadata"):
        load_gotcha(tmp_path, 1, 1)


def test_mat_nonstructure_data_is_rejected(tmp_path):
    write_mat(tmp_path, data=np.ones(3))
    with pytest.raises(ValueError, match="Missing GOTCHA metadata"):
        load_gotcha(tmp_path, 1, 1)


@pytest.mark.parametrize("invalid_samples", [np.ones((3, 5), complex), np.ones((5, 4), complex)])
def test_wrong_mat_sample_shape_is_not_silently_guessed(tmp_path, invalid_samples):
    data = mat_data()
    data["fp"] = invalid_samples
    write_mat(tmp_path, data=data)
    with pytest.raises(ValueError, match=r"indexed \[frequency, pulse\]"):
        load_gotcha(tmp_path, 1, 1)


@pytest.mark.parametrize(
    "field, value, message",
    [
        ("fp", np.ones((5, 3)), "complex pulse"),
        ("freq", np.array([9e9, 9e9, 9.1e9, 9.2e9, 9.3e9]), "positive, increasing"),
        ("freq", np.array([9e9, np.nan, 9.1e9, 9.2e9, 9.3e9]), "Frequency"),
        ("r0", np.full(3, -1), "nonnegative"),
        ("x", np.full(3, np.inf), "Antenna positions"),
        ("th", np.ones(2), "Azimuth"),
    ],
)
def test_invalid_mat_physical_metadata(tmp_path, field, value, message):
    data = mat_data()
    data[field] = value
    write_mat(tmp_path, data=data)
    with pytest.raises(ValueError, match=message):
        load_gotcha(tmp_path, 1, 1)


def test_apertures_require_identical_frequency_grids(tmp_path):
    write_mat(tmp_path, azimuth=1)
    data = mat_data()
    data["freq"] += 1.0
    write_mat(tmp_path, azimuth=2, data=data)
    with pytest.raises(ValueError, match="Frequency grids differ"):
        load_gotcha(tmp_path, 1, 2)


def raw_data():
    return {
        "stage": np.array("raw_echo"),
        "samples": np.array([[0j, 1 + 2j, 3 + 4j, 0j]]),
        "replica": np.array([1 + 2j, 3 + 4j]),
        "sampling_rate_hz": np.array(200e6),
        "carrier_hz": np.array(9.6e9),
        "receive_start_s": np.array([1e-6]),
        "positions_m": np.array([[100.0, 200.0, 300.0]]),
    }


def test_raw_npz_preserves_complex_samples_and_physical_metadata(tmp_path):
    data = raw_data()
    path = tmp_path / "echo.npz"
    np.savez(path, **data)
    echo = load_raw_echo(path)
    assert_array_equal(echo.samples, data["samples"])
    assert_array_equal(echo.replica, data["replica"])
    assert_array_equal(echo.positions_m, data["positions_m"])
    assert_array_equal(echo.receive_start_s, data["receive_start_s"])
    assert echo.carrier_hz == 9.6e9
    assert_allclose(echo.sampling_rate_hz, 200e6)


@pytest.mark.parametrize("stage", ["focused_image", "phase_history", "range_profiles", ""])
def test_raw_npz_stage_guard_rejects_recompression(tmp_path, stage):
    data = raw_data()
    data["stage"] = np.array(stage)
    path = tmp_path / "wrong-stage.npz"
    np.savez(path, **data)
    with pytest.raises(ValueError, match="not declared raw_echo"):
        load_raw_echo(path)


def test_raw_npz_rejects_invalid_physical_metadata(tmp_path):
    data = raw_data()
    data["sampling_rate_hz"] = np.array(-1.0)
    path = tmp_path / "invalid.npz"
    np.savez(path, **data)
    with pytest.raises(ValueError, match="positive and finite"):
        load_raw_echo(path)


def test_raw_npz_missing_stage_cannot_be_assumed_raw(tmp_path):
    path = tmp_path / "missing-stage.npz"
    np.savez(path, samples=np.ones((2, 3), complex))
    with pytest.raises(KeyError, match="stage"):
        load_raw_echo(path)


def test_raw_npz_does_not_load_pickle_objects(tmp_path):
    data = raw_data()
    data["samples"] = np.array([{"unexpected": "object"}], dtype=object)
    path = tmp_path / "object.npz"
    np.savez(path, **data)
    with pytest.raises(ValueError, match="Object arrays cannot be loaded"):
        load_raw_echo(path)


def test_source_record_hash_changes_with_content(tmp_path):
    path = tmp_path / "source.bin"
    path.write_bytes(b"first")
    first = source_record(path)
    path.write_bytes(b"second")
    second = source_record(path)
    assert first["sha256"] != second["sha256"]
    assert first["bytes"] == 5
    assert second["bytes"] == 6


def test_selected_gotcha_file_budget_is_checked_before_loading_any_mat(tmp_path, monkeypatch):
    first = write_mat(tmp_path, azimuth=1)
    second = write_mat(tmp_path, azimuth=2)
    total_bytes = first.stat().st_size + second.stat().st_size
    monkeypatch.setattr(gotcha, "MAX_INPUT_BYTES", total_bytes - 1)

    def forbid_load(*args, **kwargs):
        pytest.fail("All selected input sizes must be checked before loadmat")

    monkeypatch.setattr(gotcha, "loadmat", forbid_load)
    with pytest.raises(ValueError, match="Selected GOTCHA files exceed"):
        load_gotcha(tmp_path, 1, 2)


def test_gotcha_file_and_sample_limit_boundaries_are_inclusive(tmp_path, monkeypatch):
    first = write_mat(tmp_path, azimuth=1)
    second = write_mat(tmp_path, azimuth=2)
    monkeypatch.setattr(gotcha, "MAX_INPUT_BYTES", first.stat().st_size + second.stat().st_size)
    monkeypatch.setattr(gotcha, "MAX_COMPLEX_SAMPLES", 30)
    result = load_gotcha(tmp_path, 1, 2)
    assert result.samples.shape == (6, 5)


def test_combined_gotcha_sample_limit_is_checked_before_concatenation(tmp_path, monkeypatch):
    write_mat(tmp_path, azimuth=1)
    write_mat(tmp_path, azimuth=2)
    monkeypatch.setattr(gotcha, "MAX_COMPLEX_SAMPLES", 29)

    def forbid_concatenation(*args, **kwargs):
        pytest.fail("Combined phase history must be bounded before concatenation")

    monkeypatch.setattr(gotcha.np, "concatenate", forbid_concatenation)
    with pytest.raises(ValueError, match="Selected GOTCHA aperture exceeds"):
        load_gotcha(tmp_path, 1, 2)


def test_wrong_coordinate_shape_is_rejected_before_stacking(tmp_path, monkeypatch):
    data = mat_data()
    data["x"] = np.zeros(4)
    write_mat(tmp_path, data=data)

    def forbid_stacking(*args, **kwargs):
        pytest.fail("Invalid geometry must be rejected before stacking arrays")

    monkeypatch.setattr(gotcha.np, "column_stack", forbid_stacking)
    with pytest.raises(ValueError, match="Antenna positions must match"):
        load_gotcha(tmp_path, 1, 1)


def forbid_numpy_load(monkeypatch):
    def forbid_load(*args, **kwargs):
        pytest.fail("NPZ allocation bounds must be checked before numpy.load")

    monkeypatch.setattr(gotcha.np, "load", forbid_load)


def test_raw_npz_compressed_file_limit_is_checked_before_array_loading(tmp_path, monkeypatch):
    path = tmp_path / "raw.npz"
    np.savez(path, **raw_data())
    monkeypatch.setattr(gotcha, "MAX_INPUT_BYTES", path.stat().st_size - 1)
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match="Raw NPZ file exceeds"):
        load_raw_echo(path)


def test_raw_npz_expansion_limit_is_checked_before_array_loading(tmp_path, monkeypatch):
    path = tmp_path / "raw.npz"
    data = raw_data()
    data["samples"] = np.zeros((2, 100_000), complex)
    np.savez_compressed(path, **data)
    assert path.stat().st_size < 1_000_000
    monkeypatch.setattr(gotcha, "MAX_INPUT_BYTES", 1_000_000)
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match="Raw NPZ expansion exceeds"):
        load_raw_echo(path)


def write_header_only_npz(path, member, shape, dtype="<c16", version=(1, 0)):
    buffer = BytesIO()
    metadata = {"shape": shape, "fortran_order": False, "descr": dtype}
    writer = (
        np.lib.format.write_array_header_1_0
        if version == (1, 0)
        else np.lib.format.write_array_header_2_0
    )
    writer(buffer, metadata)
    contents = buffer.getvalue()
    if version == (3, 0):
        contents = contents[:6] + bytes(version) + contents[8:]
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as archive:
        archive.writestr(member, contents)


@pytest.mark.parametrize(
    "member,shape,dtype,message",
    [
        ("samples.npy", (1, 32_000_001), "<c16", "32 million"),
        ("positions_m.npy", (100_000_000, 3), "<f8", "declared array exceeds"),
        ("samples.npy", (1, 4), "<c16", "payload does not match"),
        ("samples.npy", (-1, 4), "<c16", "dimensions must be nonnegative"),
    ],
)
def test_raw_npz_declared_arrays_are_checked_before_loading(
    tmp_path, monkeypatch, member, shape, dtype, message
):
    path = tmp_path / "oversized-header.npz"
    write_header_only_npz(path, member, shape, dtype)
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match=message):
        load_raw_echo(path)


def test_raw_npz_accepts_version_two_numeric_array_headers(tmp_path):
    path = tmp_path / "version-two.npz"
    data = raw_data()
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as archive:
        for name, array in data.items():
            buffer = BytesIO()
            np.lib.format.write_array(buffer, array, version=(2, 0), allow_pickle=False)
            archive.writestr(name + ".npy", buffer.getvalue())
    result = load_raw_echo(path)
    assert_array_equal(result.samples, data["samples"])


def test_raw_npz_rejects_unsupported_npy_version_before_loading(tmp_path, monkeypatch):
    path = tmp_path / "unsupported.npz"
    write_header_only_npz(path, "samples.npy", (1, 2), version=(3, 0))
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match="versions 1 and 2"):
        load_raw_echo(path)


def test_raw_npz_rejects_invalid_archive_before_loading(tmp_path, monkeypatch):
    path = tmp_path / "not-an-archive.npz"
    path.write_bytes(b"broken data")
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match="valid raw echo NPZ"):
        load_raw_echo(path)


def test_raw_npz_expanded_file_budget_includes_every_member(tmp_path, monkeypatch):
    path = tmp_path / "all-members.npz"
    data = raw_data()
    data["unused"] = np.zeros(100_000, np.float64)
    np.savez_compressed(path, **data)
    with ZipFile(path) as archive:
        total = sum(member.file_size for member in archive.infolist())
    monkeypatch.setattr(gotcha, "MAX_INPUT_BYTES", total - 1)
    forbid_numpy_load(monkeypatch)
    with pytest.raises(ValueError, match="Raw NPZ expansion exceeds"):
        load_raw_echo(path)
