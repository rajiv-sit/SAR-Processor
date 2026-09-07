"""Reference-point calibration with independently injected pulse phase errors."""

from dataclasses import replace

import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal

from sar_processing.autofocus import focus_reference_point
from sar_processing.model import C, PhaseHistory


def isolated_reference():
    point = np.array([0.13, -0.21, 0.07])
    angle = np.linspace(-0.2, 0.2, 31)
    positions = np.column_stack((500 * np.cos(angle), 500 * np.sin(angle), np.full(31, 120)))
    frequencies = 9.2e9 + np.arange(256) * 2e6
    ranges = np.linalg.norm(positions, axis=1)
    delta = np.linalg.norm(positions - point, axis=1) - ranges
    amplitude = 0.7 - 0.4j
    samples = amplitude * np.exp(-4j * np.pi / C * delta[:, None] * frequencies)
    return PhaseHistory(samples, frequencies, positions, ranges, np.rad2deg(angle)), point


def test_reference_focus_restores_injected_phase_errors_and_preserves_first_pulse_phase():
    history, point = isolated_reference()
    pulse = np.arange(len(history.samples))
    injected = 1.3 + 2.6 * np.sin(pulse * 0.9) + 0.15 * pulse
    damaged = replace(history, samples=history.samples * np.exp(1j * injected[:, None]))
    original = damaged.samples.copy()
    result = focus_reference_point(damaged, point)
    assert_allclose(result.history.samples, history.samples * np.exp(1j * injected[0]), atol=3e-13)
    assert_allclose(
        np.exp(1j * result.phase_error_rad), np.exp(1j * (injected - injected[0])), atol=3e-13
    )
    assert_allclose(result.frequency_coherence, 1, atol=1e-14)
    assert result.aperture_coherence_before < 0.3
    assert_allclose(result.aperture_coherence_after, 1, atol=1e-14)
    assert_array_equal(damaged.samples, original)
    assert not np.shares_memory(result.history.samples, damaged.samples)


def test_clean_reference_preserves_complex_amplitude_and_phase():
    history, point = isolated_reference()
    result = focus_reference_point(history, point)
    assert_allclose(result.history.samples, history.samples, atol=3e-13)
    assert_allclose(result.phase_error_rad, 0, atol=3e-13)
    assert_allclose(result.aperture_coherence_before, 1, atol=1e-14)


def test_reference_calibration_tolerates_small_measurement_noise_and_amplitude_changes():
    history, point = isolated_reference()
    rng = np.random.default_rng(9227)
    errors = rng.uniform(-np.pi, np.pi, len(history.samples))
    amplitudes = np.linspace(0.5, 1.0, len(history.samples))
    samples = history.samples * amplitudes[:, None] * np.exp(1j * errors[:, None])
    samples += 0.002 * (rng.normal(size=samples.shape) + 1j * rng.normal(size=samples.shape))
    result = focus_reference_point(replace(history, samples=samples), point)
    assert_allclose(
        np.exp(1j * result.phase_error_rad), np.exp(1j * (errors - errors[0])), atol=0.002
    )
    assert_allclose(np.abs(result.history.samples), np.abs(samples), atol=1e-15)


@pytest.mark.parametrize(
    "kwargs",
    [
        {"min_coherence": 0},
        {"min_coherence": 1.01},
        {"min_relative_amplitude": 0},
        {"min_relative_amplitude": 1.01},
        {"min_coherence": np.nan},
        {"min_relative_amplitude": np.inf},
    ],
)
def test_invalid_reference_thresholds(kwargs):
    history, point = isolated_reference()
    with pytest.raises(ValueError, match=r"in \(0, 1\]"):
        focus_reference_point(history, point, **kwargs)


@pytest.mark.parametrize(
    "point", [np.ones(2), np.ones((1, 3)), np.array([np.inf, 0, 0]), np.ones(3, complex)]
)
def test_invalid_reference_coordinates(point):
    history, _ = isolated_reference()
    with pytest.raises(ValueError, match="Reference point"):
        focus_reference_point(history, point)


def test_wrong_processing_stage_rejected():
    with pytest.raises(TypeError, match="PhaseHistory"):
        focus_reference_point(np.ones((2, 3), complex), np.zeros(3))


def test_single_pulse_cannot_estimate_phase_error():
    history, point = isolated_reference()
    history = replace(
        history,
        samples=history.samples[:1],
        positions_m=history.positions_m[:1],
        reference_range_m=history.reference_range_m[:1],
        azimuth_deg=history.azimuth_deg[:1],
    )
    with pytest.raises(ValueError, match="at least two pulses"):
        focus_reference_point(history, point)


@pytest.mark.parametrize("damage", ["zero", "missing_pulse", "weak_pulse", "incoherent"])
def test_unusable_reference_is_rejected_without_correcting_input(damage):
    history, point = isolated_reference()
    if damage == "zero":
        history.samples[:] = 0
    elif damage == "missing_pulse":
        history.samples[3] = 0
    elif damage == "weak_pulse":
        history.samples[3] *= 0.01
    else:
        history.samples[3, ::2] *= -1
    original = history.samples.copy()
    with pytest.raises(ValueError, match="no usable|weak or incoherent"):
        focus_reference_point(history, point)
    assert_array_equal(history.samples, original)
