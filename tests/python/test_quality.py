"""Impulse-response tests with independently known sinc and sample-power metrics."""

import numpy as np
import pytest
from numpy.testing import assert_allclose

from sar_processing.quality import impulse_response_metrics


def test_rectangular_aperture_sinc_has_known_half_power_width_and_peak_sidelobe():
    x = np.linspace(-16, 16, 32_001)
    response = np.sinc(x) * np.exp(0.7j)
    measured = impulse_response_metrics(response, sample_spacing=0.001)
    assert measured.peak_index == 16_000
    assert measured.mainlobe_bounds == (15_000, 17_000)
    assert_allclose(measured.half_power_width, 0.88589294, atol=2e-6)
    assert_allclose(measured.peak_sidelobe_db, -13.26146, atol=1e-4)
    expected_energy_ratio = np.sum(np.abs(response[np.abs(x) > 1]) ** 2) / np.sum(
        np.abs(response[np.abs(x) <= 1]) ** 2
    )
    assert_allclose(measured.integrated_sidelobe_ratio, expected_energy_ratio, rtol=1e-14)
    assert_allclose(measured.integrated_sidelobe_db, 10 * np.log10(expected_energy_ratio))


def test_explicit_mainlobe_inclusive_discrete_energy_and_interpolated_power_crossings():
    response = np.sqrt(np.array([0.01, 0.04, 0, 0.25, 1, 0.25, 0, 0.09, 0.01]))
    measured = impulse_response_metrics(response, 2.0, mainlobe_bounds=(2, 6))
    assert_allclose(measured.half_power_width, 8 / 3)
    assert_allclose(measured.peak_sidelobe_ratio, 0.09)
    assert_allclose(measured.integrated_sidelobe_ratio, 0.15 / 1.5)
    scaled = impulse_response_metrics(response * 1e150 * np.exp(1.23j), 2.0, mainlobe_bounds=(2, 6))
    assert_allclose(scaled.half_power_width, measured.half_power_width)
    assert_allclose(scaled.peak_sidelobe_ratio, measured.peak_sidelobe_ratio)


def test_zero_sidelobes_have_zero_ratios_and_json_safe_absent_db():
    measured = impulse_response_metrics(np.array([0, 0, 0, 1, 0, 0, 0]), mainlobe_bounds=(2, 4))
    assert measured.peak_sidelobe_ratio == measured.integrated_sidelobe_ratio == 0
    assert measured.peak_sidelobe_db is measured.integrated_sidelobe_db is None
    assert_allclose(measured.half_power_width, 1)


@pytest.mark.parametrize("spacing", [0, -1, np.nan, np.inf])
def test_invalid_response_spacing(spacing):
    with pytest.raises(ValueError, match="positive and finite"):
        impulse_response_metrics(np.ones(7), spacing)


@pytest.mark.parametrize(
    "response", [np.ones((3, 3)), np.ones(4), np.array([1, 1, 1, 1, np.nan]), np.array(["1"] * 5)]
)
def test_invalid_response_vectors(response):
    with pytest.raises(ValueError, match="finite numeric vector"):
        impulse_response_metrics(response)


def test_zero_response_rejected():
    with pytest.raises(ValueError, match="nonzero peak"):
        impulse_response_metrics(np.zeros(7))


@pytest.mark.parametrize(
    "response",
    [np.arange(7), np.arange(7)[::-1], np.ones(7), np.array([0, 0.5, 1, 0.5, 0.2, 0.1, 0])],
)
def test_missing_bounding_minima_rejected_instead_of_fabricating_islr(response):
    with pytest.raises(ValueError, match="minima on both sides"):
        impulse_response_metrics(response)


@pytest.mark.parametrize("bounds", [(1,), (1.0, 5), (True, 5), 3])
def test_invalid_mainlobe_index_types(bounds):
    with pytest.raises(ValueError, match="two integer"):
        impulse_response_metrics(np.array([0, 0, 0, 1, 0, 0, 0]), mainlobe_bounds=bounds)


@pytest.mark.parametrize("bounds", [(0, 5), (1, 6), (4, 5), (1, 2), (5, 1)])
def test_bounds_must_enclose_peak_and_leave_measured_sidelobes(bounds):
    with pytest.raises(ValueError, match="surround the peak"):
        impulse_response_metrics(np.array([0, 0, 0, 1, 0, 0, 0]), mainlobe_bounds=bounds)


@pytest.mark.parametrize(
    "response", [np.array([0, 0, 0.9, 1, 0.9, 0, 0]), np.array([0, 0, 0.1, 1, 0.9, 0, 0])]
)
def test_mainlobe_bounds_must_include_both_half_power_crossings(response):
    with pytest.raises(ValueError, match="half-power crossings"):
        impulse_response_metrics(response, mainlobe_bounds=(2, 4))
