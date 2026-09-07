"""One-dimensional, isolated-target impulse-response measurements.

These measurements do not establish scene-wide spatial resolution, radiometric
calibration or geographic accuracy. A response cut must contain the whole main
lobe and measured sidelobes on both sides.
"""

from dataclasses import dataclass

import numpy as np
from scipy.signal import find_peaks

from .model import ComplexArray, FloatArray, real_scalar


@dataclass(frozen=True)
class ImpulseResponseMetrics:
    peak_index: int
    mainlobe_bounds: tuple[int, int]
    half_power_width: float
    peak_sidelobe_ratio: float
    integrated_sidelobe_ratio: float
    peak_sidelobe_db: float | None
    integrated_sidelobe_db: float | None


def impulse_response_metrics(
    response: ComplexArray | FloatArray,
    sample_spacing: float = 1.0,
    *,
    mainlobe_bounds: tuple[int, int] | None = None,
) -> ImpulseResponseMetrics:
    """Measure half-power width, PSLR and ISLR on a uniformly sampled cut.

    The default main lobe includes the nearest local power minima surrounding
    the global peak. Explicit bounds are inclusive sample indices. Half-power
    width uses linearly interpolated power crossings (-3.0103 dB), in units of
    ``sample_spacing``. PSLR is peak sidelobe power / peak main-lobe power; ISLR
    is the sum of all remaining sample powers / sum within the bounds. Neither
    ratio extrapolates unmeasured sidelobes. Zero sidelobes return a linear ratio
    of zero and ``None`` for the corresponding dB value (negative infinity).
    """
    values = np.asarray(response)
    spacing = real_scalar(sample_spacing, "Response sample spacing")
    if not np.isfinite(spacing) or spacing <= 0:
        raise ValueError("Response sample spacing must be positive and finite")
    if (
        values.ndim != 1
        or values.size < 5
        or values.dtype.kind not in "biufc"
        or not np.isfinite(values).all()
    ):
        raise ValueError("Response must be a finite numeric vector with at least five samples")
    magnitude = np.abs(values.astype(np.complex128 if np.iscomplexobj(values) else np.float64))
    maximum = float(magnitude.max())
    if maximum == 0 or not np.isfinite(maximum):
        raise ValueError("Response must have finite, nonzero peak magnitude")
    power = (magnitude / maximum) ** 2
    peak = int(np.argmax(power))
    if mainlobe_bounds is None:
        minima, _ = find_peaks(-power)
        left_minima, right_minima = minima[minima < peak], minima[minima > peak]
        if left_minima.size == 0 or right_minima.size == 0:
            raise ValueError("Response must contain main-lobe minima on both sides of the peak")
        left, right = int(left_minima[-1]), int(right_minima[0])
    else:
        if (
            not isinstance(mainlobe_bounds, (tuple, list))
            or len(mainlobe_bounds) != 2
            or any(not isinstance(i, int) or isinstance(i, bool) for i in mainlobe_bounds)
        ):
            raise ValueError("Main-lobe bounds must be two integer sample indices")
        left, right = mainlobe_bounds
    if not 0 < left < peak < right < values.size - 1:
        raise ValueError(
            "Main-lobe bounds must surround the peak and leave sidelobes on both sides"
        )
    left_crossings = np.flatnonzero(power[left:peak] <= 0.5)
    right_crossings = np.flatnonzero(power[peak + 1 : right + 1] <= 0.5)
    if left_crossings.size == 0 or right_crossings.size == 0:
        raise ValueError("Main-lobe bounds must include both half-power crossings")
    before = left + int(left_crossings[-1])
    after = peak + 1 + int(right_crossings[0])
    left_half = before + (0.5 - power[before]) / (power[before + 1] - power[before])
    right_half = after - 1 + (0.5 - power[after - 1]) / (power[after] - power[after - 1])
    sidelobes = np.concatenate((power[:left], power[right + 1 :]))
    pslr = float(sidelobes.max())
    islr = float(sidelobes.sum() / power[left : right + 1].sum())
    return ImpulseResponseMetrics(
        peak_index=peak,
        mainlobe_bounds=(left, right),
        half_power_width=float((right_half - left_half) * spacing),
        peak_sidelobe_ratio=pslr,
        integrated_sidelobe_ratio=islr,
        peak_sidelobe_db=float(10 * np.log10(pslr)) if pslr > 0 else None,
        integrated_sidelobe_db=float(10 * np.log10(islr)) if islr > 0 else None,
    )
