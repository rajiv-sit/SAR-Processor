"""Explicit reference-point phase calibration for phase histories.

This is deliberately not blind autofocus. The caller must identify an isolated,
stationary scatterer at a known scene coordinate, with stable scattering phase
over the aperture. Position errors and changing scattering phase are inseparable
from platform phase error with one reference point. Frequency-dependent errors,
delay errors and spatially varying atmospheric corrections are not estimated.
"""

from dataclasses import dataclass, replace

import numpy as np

from .model import C, FloatArray, PhaseHistory, finite_array, real_scalar


@dataclass(frozen=True)
class ReferenceFocusResult:
    history: PhaseHistory
    phase_error_rad: FloatArray
    frequency_coherence: FloatArray
    aperture_coherence_before: float
    aperture_coherence_after: float


def focus_reference_point(
    history: PhaseHistory,
    reference_point_m: FloatArray,
    *,
    min_coherence: float = 0.9,
    min_relative_amplitude: float = 0.1,
) -> ReferenceFocusResult:
    """Remove per-pulse constant phase errors relative to the first pulse.

    Demodulation at the supplied reference point should produce a coherent
    frequency sum for an isolated target. Reject weak pulses or frequency sums
    below ``min_coherence`` rather than inventing corrections. Phase is anchored
    to pulse zero, preserving its unknown target phase and common phase offset.
    The result is a new history; neither input samples nor vendor corrections
    are modified. These coherence checks cannot prove that a reference is truly
    isolated or stable: that assumption must be independently justified.
    """
    if not isinstance(history, PhaseHistory):
        raise TypeError("Reference-point focus requires frequency-domain PhaseHistory")
    history.validate()
    point = finite_array(reference_point_m, (3,), "Reference point")
    minimum = real_scalar(min_coherence, "Minimum coherence")
    relative = real_scalar(min_relative_amplitude, "Minimum relative amplitude")
    if not np.isfinite([minimum, relative]).all() or not 0 < minimum <= 1 or not 0 < relative <= 1:
        raise ValueError("Minimum coherence and relative amplitude must be in (0, 1]")
    if history.samples.shape[0] < 2:
        raise ValueError("Reference-point focus requires at least two pulses")
    delta = np.linalg.norm(history.positions_m - point, axis=1) - history.reference_range_m
    phase = 4 * np.pi / C * delta[:, None] * history.frequency_hz
    # Scale before reductions to keep coherence finite for large finite inputs.
    scale = float(np.abs(history.samples).max())
    if not np.isfinite(scale) or scale == 0:
        raise ValueError("Reference target has no usable finite signal")
    demodulated = history.samples / scale * np.exp(1j * phase)
    reference = np.mean(demodulated, axis=1)
    magnitudes = np.abs(reference)
    mean_magnitude = np.mean(np.abs(demodulated), axis=1)
    if np.any(mean_magnitude == 0) or magnitudes.max() == 0:
        raise ValueError("Reference target has no usable signal in one or more pulses")
    coherence = magnitudes / mean_magnitude
    if np.any(coherence < minimum) or np.any(magnitudes < relative * magnitudes.max()):
        raise ValueError("Reference target is weak or incoherent; phase correction is unreliable")
    phasors = reference / magnitudes
    # A principal phase error is sufficient for complex rotation; avoid an
    # unverified slow-time unwrapping model or removal of a real scene phase ramp.
    error = np.angle(phasors * np.conj(phasors[0]))
    correction = np.exp(-1j * error)
    corrected = replace(history, samples=history.samples * correction[:, None])
    return ReferenceFocusResult(
        history=corrected,
        phase_error_rad=error,
        frequency_coherence=coherence,
        aperture_coherence_before=float(np.abs(reference.sum()) / magnitudes.sum()),
        aperture_coherence_after=float(np.abs(np.sum(reference * correction)) / magnitudes.sum()),
    )
