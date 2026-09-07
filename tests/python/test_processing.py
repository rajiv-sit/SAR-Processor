"""Numerical SAR tests with independently synthesized point targets and chirps."""

from dataclasses import replace

import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal

from sar_processing import model, processing
from sar_processing.model import ImageGrid, PhaseHistory, RangeProfiles, RawEcho
from sar_processing.processing import (
    backproject_profiles,
    compress_phase_history,
    compress_raw_echo,
    direct_reference,
    interpolate_profile,
    uniform_frequency,
    window_weights,
)

LIGHT_SPEED = 299_792_458.0


def point_history(target=(0.0, 0.0, 0.0), amplitude=1.0 + 0.0j):
    """Use the physical two-way propagation equation, without processing helpers."""
    azimuth = np.linspace(-35.0, 35.0, 17)
    angle = np.deg2rad(azimuth)
    antennas = np.column_stack((400 * np.cos(angle), 400 * np.sin(angle), np.full(17, 80)))
    frequency = 9.6e9 + 20e6 * np.arange(64)
    reference = np.linalg.norm(antennas, axis=1)
    distance = np.linalg.norm(antennas - np.asarray(target), axis=1)
    delay = 2 * (distance - reference) / LIGHT_SPEED
    samples = amplitude * np.exp(-2j * np.pi * delay[:, None] * frequency)
    return PhaseHistory(samples, frequency, antennas, reference, azimuth)


@pytest.mark.parametrize("target", [(0.0, 0.0, 0.0), (0.3, 0.2, 0.0), (-0.4, -0.25, 0.0)])
@pytest.mark.parametrize("window", ["none", "hann", "taylor"])
def test_point_target_location_complex_amplitude_and_direct_sum(target, window):
    amplitude = 0.7 + 1.1j
    history = point_history(target, amplitude)
    grid = ImageGrid(np.linspace(-0.6, 0.6, 25), np.linspace(-0.6, 0.6, 25))
    profiles = compress_phase_history(history, upsample=64, window=window)
    image = backproject_profiles(profiles, grid, azimuth_window=window)
    row = int(np.argmin(abs(grid.y_m - target[1])))
    col = int(np.argmin(abs(grid.x_m - target[0])))
    assert np.unravel_index(np.abs(image).argmax(), image.shape) == (row, col)
    assert_allclose(image[row, col], amplitude, rtol=5e-4, atol=1e-6)

    indices = [(row, col), (0, 0), (5, 17), (24, 24)]
    points = np.array([(grid.x_m[c], grid.y_m[r], 0.0) for r, c in indices])
    reference = direct_reference(history, points, window=window, azimuth_window=window)
    assert_allclose([image[r, c] for r, c in indices], reference, atol=6e-4)
    assert_allclose(reference[0], amplitude, atol=2e-10)


def test_global_phase_rotation_survives_both_compression_stages():
    history = point_history((0.2, -0.15, 0.0), 0.6 - 0.9j)
    rotation = np.exp(0.73j)
    rotated = replace(history, samples=history.samples * rotation)
    grid = ImageGrid(np.linspace(-0.3, 0.3, 13), np.linspace(-0.3, 0.3, 13))
    baseline_profiles = compress_phase_history(history, upsample=32)
    rotated_profiles = compress_phase_history(rotated, upsample=32)
    assert_allclose(rotated_profiles.samples, baseline_profiles.samples * rotation, atol=1e-14)
    baseline = backproject_profiles(baseline_profiles, grid)
    actual = backproject_profiles(rotated_profiles, grid)
    assert_allclose(actual, baseline * rotation, atol=1e-14)


@pytest.mark.parametrize("window", ["none", "hann"])
def test_ifft_profiles_equal_independent_frequency_sum_at_every_bin(window):
    rng = np.random.default_rng(841)
    samples = rng.normal(size=(3, 7)) + 1j * rng.normal(size=(3, 7))
    frequency = 3e9 + 1e6 * np.arange(7)
    history = PhaseHistory(samples, frequency, np.zeros((3, 3)), np.ones(3), np.zeros(3))
    profiles = compress_phase_history(history, upsample=2, window=window)
    weights = np.ones(7) if window == "none" else np.hanning(7)
    ranges = profiles.range_start_m[0] + profiles.range_step_m * np.arange(16)
    expected = np.column_stack(
        [
            np.sum(
                samples
                * weights
                * np.exp(4j * np.pi * (frequency - frequency[0]) * r / LIGHT_SPEED),
                axis=1,
            )
            / weights.sum()
            for r in ranges
        ]
    )
    assert profiles.samples.shape == (3, 16)
    assert_allclose(profiles.samples, expected, atol=2e-12)
    assert_allclose(profiles.carrier_hz, frequency[0], rtol=1e-14)
    assert_allclose(profiles.range_step_m, LIGHT_SPEED / (2e6 * 16), rtol=1e-12)
    assert_array_equal(profiles.reference_range_m, history.reference_range_m)


def chirp_echo():
    sample_rate = 200e6
    carrier = 9.6e9
    t = np.arange(32) / sample_rate
    replica = np.exp(1j * np.pi * (80e6 / t[-1]) * (t - t[-1] / 2) ** 2)
    starts = np.array([1e-6, 1.1e-6, 1.2e-6])
    lags = np.array([0, 13, 64])
    ranges = LIGHT_SPEED / 2 * (starts + lags / sample_rate)
    amplitudes = np.array([0.8 - 0.6j, -0.3 + 0.9j, 0.4 + 0.2j])
    received = np.zeros((3, 96), dtype=complex)
    for pulse, lag in enumerate(lags):
        received[pulse, lag : lag + len(replica)] = (
            amplitudes[pulse]
            * np.exp(-4j * np.pi * carrier * ranges[pulse] / LIGHT_SPEED)
            * replica
        )
    positions = np.column_stack((np.zeros(3), np.zeros(3), ranges))
    return RawEcho(received, replica, sample_rate, carrier, starts, positions), lags, amplitudes


def test_raw_lfm_matched_filter_lag_amplitude_phase_and_linear_edges():
    echo, lags, amplitudes = chirp_echo()
    profiles = compress_raw_echo(echo)
    assert_array_equal(np.abs(profiles.samples).argmax(axis=1), lags)
    physical_ranges = LIGHT_SPEED / 2 * (echo.receive_start_s + lags / echo.sampling_rate_hz)
    actual = profiles.samples[np.arange(3), lags]
    expected = amplitudes * np.exp(-4j * np.pi * echo.carrier_hz * physical_ranges / LIGHT_SPEED)
    assert_allclose(actual, expected, atol=2e-12)
    assert_allclose(profiles.range_start_m, echo.receive_start_s * LIGHT_SPEED / 2)
    assert_allclose(profiles.range_step_m, LIGHT_SPEED / (2 * echo.sampling_rate_hz))

    # Explicit lag correlation, including zero-padding at the end of the receive window.
    padded = np.pad(echo.samples, ((0, 0), (0, len(echo.replica))))
    energy = np.sum(np.abs(echo.replica) ** 2)
    expected_correlations = np.array(
        [
            [
                np.sum(row[lag : lag + len(echo.replica)] * np.conj(echo.replica)) / energy
                for lag in range(echo.samples.shape[1])
            ]
            for row in padded
        ]
    )
    assert_allclose(profiles.samples, expected_correlations, atol=1e-14)


def test_raw_echo_backprojection_restores_carrier_phase():
    echo, _, amplitudes = chirp_echo()
    grid = ImageGrid(np.array([0.0, 0.05]), np.array([0.0, 0.05]))
    image = backproject_profiles(compress_raw_echo(echo), grid, azimuth_window="none")
    assert_allclose(image[0, 0], amplitudes.mean(), atol=2e-10)


def test_range_swath_endpoints_are_inclusive_and_geometry_sets_pixel_order():
    carrier = 1e6
    ranges = np.arange(6)
    expected_amplitudes = 2 + 0.1 * ranges
    samples = expected_amplitudes * np.exp(-4j * np.pi * carrier * ranges / LIGHT_SPEED)
    profiles = RangeProfiles(
        samples[None, :], np.zeros((1, 3)), np.zeros(1), np.zeros(1), 1, carrier
    )
    grid = ImageGrid(np.array([0.0, 3.0]), np.array([0.0, 4.0]))
    assert_allclose(backproject_profiles(profiles, grid), [[2.0, 2.3], [2.4, 2.5]], atol=1e-14)


@pytest.mark.parametrize("start", [-10.0, 10.0])
def test_backprojection_rejects_grid_outside_measured_swath(start):
    profiles = RangeProfiles(
        np.ones((1, 6), complex), np.zeros((1, 3)), np.zeros(1), np.array([start]), 1.0, 1e6
    )
    with pytest.raises(ValueError, match="range swath"):
        backproject_profiles(profiles, ImageGrid(np.array([0.0, 1.0]), np.array([0.0, 1.0])))


@pytest.mark.parametrize("function", [compress_phase_history, compress_raw_echo])
def test_range_compression_rejects_wrong_processing_stage(function):
    with pytest.raises(TypeError, match="requires"):
        function(np.ones((2, 4), complex))


def test_backprojection_rejects_uncompressed_phase_history():
    with pytest.raises(TypeError, match="range profiles"):
        backproject_profiles(point_history(), ImageGrid(np.arange(2.0), np.arange(2.0)))


@pytest.mark.parametrize("upsample", [0, 129, 1.5, "2"])
def test_invalid_upsampling(upsample):
    with pytest.raises(ValueError, match="Upsampling"):
        compress_phase_history(point_history(), upsample=upsample)


def test_range_profile_allocation_limit_is_checked_before_fft():
    history = PhaseHistory(
        np.ones((245, 1024), complex),
        1e9 + np.arange(1024) * 1e6,
        np.zeros((245, 3)),
        np.zeros(245),
        np.zeros(245),
    )
    with pytest.raises(ValueError, match="32 million"):
        compress_phase_history(history, upsample=128)


@pytest.mark.parametrize("count", [1, 2, 5])
@pytest.mark.parametrize("name", ["hann", "none"])
def test_window_weights_small_apertures_are_nonzero(count, name):
    expected = np.hanning(count) if name == "hann" and count > 2 else np.ones(count)
    assert_array_equal(window_weights(count, name), expected)
    assert window_weights(count, name).sum() > 0


def test_unknown_window_is_rejected():
    with pytest.raises(ValueError, match="Window"):
        window_weights(8, "gaussian")


def test_float32_quantized_frequency_grid_is_accepted_with_bounded_fit_error():
    nominal = 9.6e9 + np.arange(127) * 2.75e6
    quantized = nominal.astype(np.float32).astype(np.float64)
    start, step, error = uniform_frequency(quantized)
    assert abs(start - nominal[0]) < 512
    assert abs(step - 2.75e6) < 10
    assert 0 < error <= 1024


@pytest.mark.parametrize(
    "frequency, message",
    [
        (np.array([[1.0, 2.0]]), "finite frequencies"),
        (np.array([1.0]), "finite frequencies"),
        (np.array([1.0, np.nan]), "finite frequencies"),
        (np.array([0.0, 1.0]), "positive and increasing"),
        (np.array([2.0, 1.0]), "positive and increasing"),
        (np.array([1.0, 1.0]), "positive and increasing"),
        (np.array([1e9, 1.001e9, 1.004e9]), "uniform frequencies"),
    ],
)
def test_invalid_frequency_grids(frequency, message):
    with pytest.raises(ValueError, match=message):
        uniform_frequency(frequency)


@pytest.mark.parametrize("points", [np.ones(3), np.ones((2, 2)), np.array([[np.nan, 0.0, 0.0]])])
def test_direct_reference_rejects_invalid_points(points):
    with pytest.raises(ValueError, match="reference coordinates"):
        direct_reference(point_history(), points)


@pytest.mark.parametrize(
    "field, value, message",
    [
        ("samples", np.ones((2, 64)), "complex pulse"),
        ("samples", np.ones(3, complex), "complex pulse"),
        ("samples", np.empty((0, 64), complex), "complex pulse"),
        ("samples", np.full((17, 64), complex(np.nan, 0)), "must be finite"),
        ("frequency_hz", np.zeros(64), "positive, increasing"),
        ("frequency_hz", np.ones(64), "positive, increasing"),
        ("frequency_hz", np.ones(63), "Frequency"),
        ("positions_m", np.zeros((17, 2)), "Antenna positions"),
        ("reference_range_m", np.full(17, -1), "nonnegative"),
        ("reference_range_m", np.full(17, np.inf), "Reference range"),
        ("azimuth_deg", np.zeros(16), "Azimuth"),
    ],
)
def test_phase_history_invalid_metadata(field, value, message):
    history = replace(point_history(), **{field: value})
    with pytest.raises(ValueError, match=message):
        history.validate()


def test_phase_history_requires_more_than_one_frequency_sample():
    history = replace(
        point_history(), samples=np.ones((17, 1), complex), frequency_hz=np.array([1e9])
    )
    with pytest.raises(ValueError, match="At least two"):
        history.validate()


@pytest.mark.parametrize(
    "field, value, message",
    [
        ("replica", np.ones(32), "complex reference chirp"),
        ("replica", np.ones((2, 16), complex), "complex reference chirp"),
        ("replica", np.ones(97, complex), "complex reference chirp"),
        ("replica", np.empty(0, complex), "complex reference chirp"),
        ("replica", np.zeros(32, complex), "nonzero energy"),
        ("replica", np.full(32, complex(np.nan, 0)), "finite, nonzero"),
        ("sampling_rate_hz", 0.0, "positive and finite"),
        ("carrier_hz", np.inf, "positive and finite"),
        ("receive_start_s", np.array([-1.0, 0.0, 0.0]), "nonnegative"),
        ("receive_start_s", np.zeros(2), "Receive start"),
    ],
)
def test_raw_echo_invalid_metadata(field, value, message):
    echo, _, _ = chirp_echo()
    with pytest.raises(ValueError, match=message):
        replace(echo, **{field: value}).validate()


@pytest.mark.parametrize(
    "field, value, message",
    [
        ("samples", np.ones((3, 1), complex), "at least two samples"),
        ("range_step_m", 0.0, "positive and finite"),
        ("carrier_hz", np.nan, "positive and finite"),
        ("range_start_m", np.zeros(2), "Range start"),
    ],
)
def test_invalid_range_profiles(field, value, message):
    echo, _, _ = chirp_echo()
    profiles = compress_raw_echo(echo)
    with pytest.raises(ValueError, match=message):
        replace(profiles, **{field: value}).validate()


@pytest.mark.parametrize(
    "axis, message",
    [
        (np.ones((2, 2)), "finite vectors"),
        (np.array([1.0]), "finite vectors"),
        (np.array([0.0, np.inf]), "finite vectors"),
        (np.array([0.0, 0.0]), "strictly increasing"),
        (np.array([1.0, 0.0]), "strictly increasing"),
    ],
)
def test_invalid_image_grid_axes(axis, message):
    with pytest.raises(ValueError, match=message):
        ImageGrid(axis, np.arange(2.0)).validate()


@pytest.mark.parametrize("height", [np.nan, np.inf])
def test_invalid_imaging_height(height):
    with pytest.raises(ValueError, match="height"):
        ImageGrid(np.arange(2.0), np.arange(2.0), height).validate()


def test_image_grid_allocation_limit():
    with pytest.raises(ValueError, match="16 million"):
        ImageGrid(np.arange(4097.0), np.arange(4097.0)).validate()


def test_complex_input_limit_rejects_broadcast_array_before_scanning_or_casting(monkeypatch):
    # A broadcast view describes a large input without allocating it in the test.
    oversized = np.broadcast_to(np.array(1j, dtype=np.complex64), (1, 32_000_001))

    def forbid_scan(*args, **kwargs):
        pytest.fail("Oversized input must be rejected before finite scanning")

    monkeypatch.setattr(model.np, "isfinite", forbid_scan)
    with pytest.raises(ValueError, match="32 million"):
        model.complex_matrix(oversized)


def test_complex_input_sample_limit_is_inclusive_and_preserves_conversion(monkeypatch):
    monkeypatch.setattr(model, "MAX_COMPLEX_SAMPLES", 6)
    values = np.array([[1 + 2j, 3 + 4j, 5 + 6j], [0j, -1j, -2 + 0j]], np.complex64)
    result = model.complex_matrix(values)
    assert result.dtype == np.complex128
    assert_array_equal(result, values)
    with pytest.raises(ValueError, match="32 million"):
        model.complex_matrix(np.ones((1, 7), np.complex64))


def test_invalid_metadata_shape_is_rejected_before_numeric_conversion():
    oversized = np.broadcast_to(np.array("not numeric"), (32_000_001,))
    with pytest.raises(ValueError, match="Positions must be finite with shape"):
        model.finite_array(oversized, (2, 3), "Positions")


def test_raw_fft_limit_is_checked_before_convolution(monkeypatch):
    echo, _, _ = chirp_echo()
    # 96 samples + 32 replica samples - 1 rounds to 128 complex FFT bins per pulse.
    monkeypatch.setattr(processing, "MAX_COMPLEX_SAMPLES", 3 * 128 - 1)

    def forbid_fft(*args, **kwargs):
        pytest.fail("The FFT must not execute above its allocation limit")

    monkeypatch.setattr(processing, "fftconvolve", forbid_fft)
    with pytest.raises(ValueError, match="Raw matched-filter FFT allocation"):
        compress_raw_echo(echo)


def test_raw_fft_limit_boundary_preserves_original_matched_filter_values(monkeypatch):
    echo, _, _ = chirp_echo()
    expected = compress_raw_echo(echo)
    monkeypatch.setattr(processing, "MAX_COMPLEX_SAMPLES", 3 * 128)
    actual = compress_raw_echo(echo)
    assert_array_equal(actual.samples, expected.samples)
    assert_array_equal(actual.range_start_m, expected.range_start_m)
    assert actual.range_step_m == expected.range_step_m


def test_frequency_fft_limit_is_checked_before_fft_call(monkeypatch):
    history = point_history()
    monkeypatch.setattr(processing, "MAX_COMPLEX_SAMPLES", history.samples.size - 1)

    def forbid_fft(*args, **kwargs):
        pytest.fail("Frequency FFT must not execute above its allocation limit")

    monkeypatch.setattr(processing.np.fft, "ifft", forbid_fft)
    with pytest.raises(ValueError, match="Range-profile allocation"):
        compress_phase_history(history, upsample=1)


@pytest.mark.parametrize("imaginary", [0.0, 0.5])
@pytest.mark.parametrize(
    "field", ["frequency_hz", "positions_m", "reference_range_m", "azimuth_deg"]
)
def test_phase_history_rejects_complex_physical_metadata_without_cast_warnings(field, imaginary):
    history = point_history()
    value = getattr(history, field).astype(np.complex128) + 1j * imaginary
    with np.errstate(all="raise"), pytest.raises(ValueError, match="real-valued"):
        replace(history, **{field: value}).validate()


@pytest.mark.parametrize("imaginary", [0.0, 0.5])
@pytest.mark.parametrize("field", ["receive_start_s", "positions_m"])
def test_raw_echo_rejects_complex_timing_and_geometry(field, imaginary):
    echo, _, _ = chirp_echo()
    value = getattr(echo, field).astype(np.complex128) + 1j * imaginary
    with pytest.raises(ValueError, match="real-valued"):
        replace(echo, **{field: value}).validate()


@pytest.mark.parametrize("imaginary", [0.0, 0.5])
@pytest.mark.parametrize("field", ["positions_m", "reference_range_m", "range_start_m"])
def test_range_profiles_reject_complex_physical_metadata(field, imaginary):
    echo, _, _ = chirp_echo()
    profiles = compress_raw_echo(echo)
    value = getattr(profiles, field).astype(np.complex128) + 1j * imaginary
    with pytest.raises(ValueError, match="real-valued"):
        replace(profiles, **{field: value}).validate()


@pytest.mark.parametrize(
    "value",
    [
        np.complex128(9.6e9 + 0j),
        np.complex128(9.6e9 + 1j),
        complex(9.6e9, 0),
        np.array([9.6e9]),
    ],
)
@pytest.mark.parametrize("field", ["sampling_rate_hz", "carrier_hz"])
def test_raw_echo_requires_real_scalars_for_physical_rates(field, value):
    echo, _, _ = chirp_echo()
    with pytest.raises(ValueError, match="real-valued numeric scalar"):
        replace(echo, **{field: value}).validate()


@pytest.mark.parametrize("value", [np.complex128(1 + 0j), np.complex128(1 + 0.5j)])
@pytest.mark.parametrize("field", ["range_step_m", "carrier_hz"])
def test_range_profiles_require_real_scalar_spacing_and_carrier(field, value):
    echo, _, _ = chirp_echo()
    profiles = compress_raw_echo(echo)
    with pytest.raises(ValueError, match="real-valued numeric scalar"):
        replace(profiles, **{field: value}).validate()


@pytest.mark.parametrize("field", ["x_m", "y_m", "z_m"])
@pytest.mark.parametrize("imaginary", [0.0, 0.5])
def test_image_grid_rejects_complex_coordinates_even_with_zero_imaginary_part(field, imaginary):
    grid = ImageGrid(np.arange(2.0), np.arange(2.0))
    value = np.asarray(getattr(grid, field), dtype=np.complex128) + 1j * imaginary
    with pytest.raises(ValueError, match="real-valued"):
        replace(grid, **{field: value}).validate()


def test_object_metadata_cannot_hide_complex_scalars_inside_a_real_cast():
    values = np.array([np.complex128(1 + 0j)], dtype=object)
    with pytest.raises(ValueError, match="real-valued"):
        model.finite_array(values, (1,), "Frequency")

    axis = np.array([np.complex128(0 + 0j), np.complex128(1 + 0j)], dtype=object)
    with pytest.raises(ValueError, match="real-valued"):
        ImageGrid(axis, np.arange(2.0)).validate()


def test_real_numpy_scalars_remain_accepted_and_are_normalized():
    echo, _, _ = chirp_echo()
    echo.sampling_rate_hz = np.array(echo.sampling_rate_hz)
    echo.carrier_hz = np.float64(echo.carrier_hz)
    echo.validate()
    assert isinstance(echo.sampling_rate_hz, float)
    assert isinstance(echo.carrier_hz, float)


@pytest.mark.parametrize("count", [0, -1, 1.5, True])
def test_invalid_window_counts(count):
    with pytest.raises(ValueError, match="positive integer"):
        window_weights(count, "hann")


@pytest.mark.parametrize(
    "kwargs",
    [
        {"taylor_nbar": 1},
        {"taylor_nbar": 33},
        {"taylor_nbar": 3.5},
        {"taylor_nbar": True},
        {"taylor_sll_db": 14},
        {"taylor_sll_db": 121},
        {"taylor_sll_db": np.nan},
        {"taylor_sll_db": np.inf},
    ],
)
def test_invalid_taylor_parameters(kwargs):
    with pytest.raises(ValueError, match="Taylor requires"):
        window_weights(32, "taylor", **kwargs)


@pytest.mark.parametrize("count", [1, 2, 17, 64])
def test_taylor_symmetry_unit_dc_gain_and_sidelobe_configuration(count):
    weights = window_weights(count, "taylor", taylor_nbar=5, taylor_sll_db=40)
    assert_allclose(weights, weights[::-1], atol=1e-14)
    assert (weights > 0).all()
    if count > 2:
        assert_allclose(weights.mean(), 1, atol=1e-14)
        assert not np.allclose(weights, window_weights(count, "taylor"))


@pytest.mark.parametrize("window", ["hann", "taylor"])
def test_weighted_raw_filter_preserves_target_amplitude_and_matches_direct_correlation(window):
    echo, lags, _ = chirp_echo()
    baseline = compress_raw_echo(echo)
    actual = compress_raw_echo(echo, window, taylor_nbar=5, taylor_sll_db=40)
    weights = window_weights(32, window, taylor_nbar=5, taylor_sll_db=40)
    denominator = np.sum(np.abs(echo.replica) ** 2 * weights)
    padded = np.pad(echo.samples, ((0, 0), (0, echo.replica.size)))
    expected = np.array(
        [
            [
                np.sum(row[i : i + echo.replica.size] * np.conj(echo.replica) * weights)
                / denominator
                for i in range(echo.samples.shape[1])
            ]
            for row in padded
        ]
    )
    assert_allclose(actual.samples, expected, atol=1e-14)
    assert_allclose(
        actual.samples[np.arange(3), lags], baseline.samples[np.arange(3), lags], atol=1e-14
    )
    # A taper changes the off-peak response, while normalization keeps peak gain.
    assert not np.allclose(actual.samples, baseline.samples)


def test_weighted_raw_filter_rejects_replica_energy_only_at_hann_zero_endpoints():
    echo, _, _ = chirp_echo()
    echo.replica = np.zeros(32, complex)
    echo.replica[0] = 1j
    with pytest.raises(ValueError, match="Weighted replica"):
        compress_raw_echo(echo, "hann")


@pytest.mark.parametrize("method", ["linear", "sinc"])
def test_complex_interpolation_integer_samples_includes_swath_endpoints(method):
    rng = np.random.default_rng(4792)
    samples = rng.normal(size=128) + 1j * rng.normal(size=128)
    assert_array_equal(interpolate_profile(samples, np.arange(128), method), samples)
    assert_array_equal(interpolate_profile(samples, np.empty((0, 2)), method), np.empty((0, 2)))
    assert interpolate_profile(samples, np.array(0.0), method) == samples[0]


def test_sinc_complex_bandlimited_fractional_delay_accuracy_and_dc_normalization():
    nodes = np.arange(128)
    queries = np.linspace(10.01, 115.99, 173).reshape(1, -1)
    samples = 0.7 * np.exp(2j * np.pi * 0.18 * nodes) - 0.3j * np.exp(-2j * np.pi * 0.07 * nodes)
    exact = 0.7 * np.exp(2j * np.pi * 0.18 * queries) - 0.3j * np.exp(-2j * np.pi * 0.07 * queries)
    linear = interpolate_profile(samples, queries)
    sinc = interpolate_profile(samples, queries, "sinc")
    assert np.linalg.norm(sinc - exact) < np.linalg.norm(linear - exact) / 40
    assert_allclose(sinc, exact, atol=0.002)
    assert_allclose(
        interpolate_profile(np.full(128, 0.7 - 0.3j), queries, "sinc"), 0.7 - 0.3j, atol=1e-15
    )
    assert sinc.dtype == np.complex128


@pytest.mark.parametrize("value", [-1e-12, 127.000000000001])
@pytest.mark.parametrize("method", ["linear", "sinc"])
def test_interpolation_never_clamps_or_wraps_unmeasured_swath(value, method):
    with pytest.raises(ValueError, match="range swath"):
        interpolate_profile(np.ones(128, complex), np.array([value]), method)


@pytest.mark.parametrize("value", [0.01, 6.99, 120.01, 126.99])
def test_sinc_rejects_fractional_edge_queries_without_full_measured_support(value):
    with pytest.raises(ValueError, match="Sinc interpolation support"):
        interpolate_profile(np.ones(128, complex), np.array([value]), "sinc")


@pytest.mark.parametrize("width", [1, 33, 2.5, True])
def test_invalid_sinc_support(width):
    with pytest.raises(ValueError, match="Sinc half width"):
        interpolate_profile(np.ones(128, complex), np.array([50.5]), "sinc", sinc_half_width=width)


def test_invalid_interpolator():
    with pytest.raises(ValueError, match="Interpolation must be"):
        interpolate_profile(np.ones(128, complex), np.array([50.5]), "cubic")


@pytest.mark.parametrize(
    "samples",
    [np.ones(3), np.ones((2, 3), complex), np.ones(1, complex), np.array([0j, complex(np.nan, 1)])],
)
def test_invalid_interpolation_samples(samples):
    with pytest.raises(ValueError, match="finite complex vector"):
        interpolate_profile(samples, np.array([0.0]))


@pytest.mark.parametrize("indices", [np.array([1 + 0j]), np.array([np.nan]), np.array(["0"])])
def test_invalid_interpolation_indices(indices):
    with pytest.raises(ValueError, match="real and finite"):
        interpolate_profile(np.ones(128, complex), indices)


@pytest.mark.parametrize("method", ["linear", "sinc"])
@pytest.mark.parametrize("tile_pixels", [1, 7, 32])
def test_backprojection_tiles_preserve_complex_result_and_bound_temporary_arrays(
    monkeypatch, method, tile_pixels
):
    history = point_history((0.17, -0.23, 0.0), 0.6 + 0.7j)
    grid = ImageGrid(np.linspace(-0.4, 0.4, 11), np.linspace(-0.4, 0.4, 9))
    profiles = compress_phase_history(history, upsample=4, window="taylor")
    expected = backproject_profiles(profiles, grid, interpolation=method, azimuth_window="taylor")
    interpolate = processing._interpolate_profile
    sizes = []

    def checked(values, indices, *args):
        sizes.append(indices.size)
        return interpolate(values, indices, *args)

    monkeypatch.setattr(processing, "_interpolate_profile", checked)
    actual = backproject_profiles(
        profiles, grid, interpolation=method, azimuth_window="taylor", tile_pixels=tile_pixels
    )
    assert_array_equal(actual, expected)
    assert max(sizes) <= tile_pixels


@pytest.mark.parametrize("tile_pixels", [0, 1048577, 1.1, True])
def test_invalid_backprojection_tile_size(tile_pixels):
    with pytest.raises(ValueError, match="Tile pixels"):
        backproject_profiles(
            compress_phase_history(point_history()),
            ImageGrid(np.arange(2.0), np.arange(2.0)),
            tile_pixels=tile_pixels,
        )


def test_sinc_backprojection_reduces_error_against_independent_direct_frequency_sum():
    history = point_history((0.13, -0.21, 0), 0.7 + 0.4j)
    grid = ImageGrid(np.linspace(-0.4, 0.4, 13), np.linspace(-0.4, 0.4, 11))
    points = np.array([(x, y, 0) for y in grid.y_m for x in grid.x_m])
    profiles = compress_phase_history(
        history, upsample=4, window="taylor", taylor_nbar=5, taylor_sll_db=40
    )
    exact = direct_reference(
        history, points, window="taylor", azimuth_window="taylor", taylor_nbar=5, taylor_sll_db=40
    )
    images = [
        backproject_profiles(
            profiles,
            grid,
            azimuth_window="taylor",
            interpolation=method,
            taylor_nbar=5,
            taylor_sll_db=40,
        ).ravel()
        for method in ("linear", "sinc")
    ]
    errors = [np.linalg.norm(image - exact) / np.linalg.norm(exact) for image in images]
    assert errors[1] < 0.003
    assert errors[1] < errors[0] / 10


@pytest.mark.parametrize("tile_pixels", [7, 1000])
def test_terrain_height_backprojection_matches_direct_sum_on_sloped_surface(tile_pixels):
    x, y = np.linspace(-0.3, 0.3, 13), np.linspace(-0.3, 0.3, 13)
    xx, yy = np.meshgrid(x, y)
    zz = 0.07 + 0.02 * xx + 0.03 * yy
    row, col = 2, 9
    amplitude = 0.7 + 0.4j
    target = (x[col], y[row], zz[row, col])
    history = point_history(target, amplitude)
    grid = ImageGrid(x, y, zz)
    profiles = compress_phase_history(history, upsample=16)
    actual = backproject_profiles(profiles, grid, interpolation="sinc", tile_pixels=tile_pixels)
    points = np.column_stack((xx.ravel(), yy.ravel(), zz.ravel()))
    expected = direct_reference(history, points).reshape(actual.shape)
    assert_allclose(actual, expected, atol=3e-4)
    assert_allclose(actual[row, col], amplitude, atol=3e-4)
    assert np.unravel_index(np.abs(actual).argmax(), actual.shape) == (row, col)
