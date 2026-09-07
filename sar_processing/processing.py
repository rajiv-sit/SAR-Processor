"""Matched filtering and coherent, geometry-based azimuth compression."""

import numpy as np
from scipy.fft import next_fast_len
from scipy.signal import fftconvolve
from scipy.signal.windows import taylor

from .model import (
    C,
    MAX_COMPLEX_SAMPLES,
    ComplexArray,
    FloatArray,
    ImageGrid,
    PhaseHistory,
    RangeProfiles,
    RawEcho,
    real_scalar,
)


def window_weights(
    count: int, name: str, *, taylor_nbar: int = 4, taylor_sll_db: float = 35.0
) -> FloatArray:
    """Symmetric weighting; gain normalization belongs to the processing stage.

    Taylor ``sll_db`` is a positive desired sidelobe suppression in dB, not a
    guarantee about the reconstructed scene. Parameters follow SciPy's window.
    """
    if not isinstance(count, int) or isinstance(count, bool) or count < 1:
        raise ValueError("Window count must be a positive integer")
    if name not in {"hann", "none", "taylor"}:
        raise ValueError("Window must be hann, none or taylor")
    if name == "taylor":
        sll = real_scalar(taylor_sll_db, "Taylor sidelobe suppression")
        if (
            not isinstance(taylor_nbar, int)
            or isinstance(taylor_nbar, bool)
            or not 2 <= taylor_nbar <= 32
            or not np.isfinite(sll)
            or not 15 <= sll <= 120
        ):
            raise ValueError("Taylor requires nbar in [2, 32] and sidelobe dB in [15, 120]")
        weights = taylor(count, nbar=taylor_nbar, sll=sll, norm=False, sym=True)
        if np.any(weights < 0) or not np.isfinite(weights).all() or weights.sum() <= 0:
            raise ValueError("Taylor parameters produce invalid weights")
        return weights
    return np.hanning(count) if name == "hann" and count > 2 else np.ones(count)


def uniform_frequency(frequency: FloatArray) -> tuple[float, float, float]:
    if frequency.ndim != 1 or frequency.size < 2 or not np.isfinite(frequency).all():
        raise ValueError("Expected at least two finite frequencies")
    if frequency[0] <= 0 or np.any(np.diff(frequency) <= 0):
        raise ValueError("Frequency must be positive and increasing")
    index = np.arange(frequency.size)
    step, start = np.polyfit(index, frequency, 1)
    error = float(np.max(np.abs(frequency - (start + step * index))))
    tolerance = min(2 * float(np.spacing(np.float32(frequency.max()))), step * 0.001)
    if error > max(tolerance, step * 1e-8):
        raise ValueError("IFFT requires uniform frequencies, allowing float32 quantization")
    return float(start), float(step), error


def compress_phase_history(
    history: PhaseHistory,
    upsample: int = 32,
    window: str = "hann",
    *,
    taylor_nbar: int = 4,
    taylor_sll_db: float = 35.0,
) -> RangeProfiles:
    """Form range profiles from demodulated frequency-domain phase histories."""
    if not isinstance(history, PhaseHistory):
        raise TypeError("Range-profile formation requires frequency-domain PhaseHistory")
    history.validate()
    if not isinstance(upsample, int) or not 1 <= upsample <= 128:
        raise ValueError("Upsampling must be an integer in [1, 128]")
    pulses, count = history.samples.shape
    nfft = 1 << (count * upsample - 1).bit_length()
    if pulses * nfft > MAX_COMPLEX_SAMPLES:
        raise ValueError("Range-profile allocation exceeds 32 million complex samples")
    start, step, _ = uniform_frequency(history.frequency_hz)
    taper = window_weights(count, window, taylor_nbar=taylor_nbar, taylor_sll_db=taylor_sll_db)
    samples = np.fft.fftshift(np.fft.ifft(history.samples * taper, n=nfft, axis=1), axes=1) * (
        nfft / taper.sum()
    )
    spacing = C / (2 * step * nfft)
    return RangeProfiles(
        samples,
        history.positions_m,
        history.reference_range_m,
        np.full(pulses, -nfft // 2 * spacing),
        spacing,
        start,
    )


def compress_raw_echo(
    echo: RawEcho,
    window: str = "none",
    *,
    taylor_nbar: int = 4,
    taylor_sll_db: float = 35.0,
) -> RangeProfiles:
    """Linear matched filtering; lag zero remains at the receive-window start.

    Optional replica tapering is a mismatched filter: it changes sidelobes and
    noise gain. Weighted replica energy preserves the isolated target amplitude.
    The default remains the untapered matched filter.

    With D=sum(abs(replica)**2 * taper), white input noise of variance sigma**2
    has output variance sigma**2 * sum(abs(replica)**2 * taper**2) / D**2 at
    lags containing the full replica. Tapering therefore sacrifices matched-filter
    SNR; it does not provide a free resolution or sensitivity improvement.
    """
    if not isinstance(echo, RawEcho):
        raise TypeError("Matched filtering requires RawEcho, not an already focused image")
    echo.validate()
    pulses, count = echo.samples.shape
    nfft = next_fast_len(count + echo.replica.size - 1)
    if pulses * nfft > MAX_COMPLEX_SAMPLES:
        raise ValueError("Raw matched-filter FFT allocation exceeds 32 million complex samples")
    taper = window_weights(
        echo.replica.size, window, taylor_nbar=taylor_nbar, taylor_sll_db=taylor_sll_db
    )
    energy = float(np.sum(np.abs(echo.replica) ** 2 * taper))
    if not np.isfinite(energy) or energy <= 0:
        raise ValueError("Weighted replica must have finite, nonzero energy")
    kernel = np.conj((echo.replica * taper)[::-1])[None, :]
    filtered = fftconvolve(echo.samples, kernel, mode="full", axes=1)
    first = echo.replica.size - 1
    filtered = filtered[:, first : first + count] / energy
    return RangeProfiles(
        filtered,
        echo.positions_m,
        np.zeros(pulses),
        echo.receive_start_s * C / 2,
        C / (2 * echo.sampling_rate_hz),
        echo.carrier_hz,
    )


def _validate_interpolation(method: str, sinc_half_width: int) -> None:
    if method not in {"linear", "sinc"}:
        raise ValueError("Interpolation must be linear or sinc")
    if (
        not isinstance(sinc_half_width, int)
        or isinstance(sinc_half_width, bool)
        or not 2 <= sinc_half_width <= 32
    ):
        raise ValueError("Sinc half width must be an integer in [2, 32]")


def _interpolate_profile(
    samples: ComplexArray, indices: FloatArray, method: str, sinc_half_width: int
) -> ComplexArray:
    if np.any(indices < 0) or np.any(indices > samples.size - 1):
        raise ValueError("Image exceeds the measured range swath; reduce or move the image grid")
    if method == "linear":
        left = np.floor(indices).astype(np.intp)
        right = np.minimum(left + 1, samples.size - 1)
        fraction = indices - left
        return (1 - fraction) * samples[left] + fraction * samples[right]

    # Exact sample positions remain valid at both measured swath endpoints.
    rounded = np.rint(indices)
    exact = indices == rounded
    result = np.empty(indices.shape, dtype=np.complex128)
    result[exact] = samples[rounded[exact].astype(np.intp)]
    fractional = indices[~exact]
    first = np.floor(fractional).astype(np.intp) - sinc_half_width + 1
    if np.any(first < 0) or np.any(first + 2 * sinc_half_width > samples.size):
        raise ValueError("Sinc interpolation support exceeds the measured range swath")
    # Lanczos-windowed sinc, accumulated tap by tap to avoid pixels-by-taps buffers.
    # DC normalization preserves amplitude; no circular wrapping or edge extrapolation.
    interpolated = np.zeros(fractional.shape, dtype=np.complex128)
    gain = np.zeros(fractional.shape, dtype=np.float64)
    for tap in range(2 * sinc_half_width):
        sample_index = first + tap
        distance = fractional - sample_index
        weight = np.sinc(distance) * np.sinc(distance / sinc_half_width)
        gain += weight
        interpolated += weight * samples[sample_index]
    result[~exact] = interpolated / gain
    return result


def interpolate_profile(
    samples: ComplexArray,
    sample_indices: FloatArray,
    method: str = "linear",
    *,
    sinc_half_width: int = 8,
) -> ComplexArray:
    """Interpolate a complex vector at real sample indices, without extrapolation.

    Sinc assumes samples are bandlimited below Nyquist. Frequency histories
    should be adequately upsampled before interpolation (the default is 32).
    Sinc uses 2*half_width measured taps and a normalized Lanczos window.
    Fractional indices lacking full support raise; integer endpoints are exact.
    """
    _validate_interpolation(method, sinc_half_width)
    values = np.asarray(samples)
    indices = np.asarray(sample_indices)
    if (
        values.ndim != 1
        or values.size < 2
        or not np.iscomplexobj(values)
        or not np.isfinite(values).all()
    ):
        raise ValueError("Interpolation requires a finite complex vector with at least two samples")
    if indices.dtype.kind not in "biuf" or not np.isfinite(indices).all():
        raise ValueError("Interpolation indices must be real and finite")
    return _interpolate_profile(
        values.astype(np.complex128, copy=False),
        indices.astype(np.float64, copy=False),
        method,
        sinc_half_width,
    )


def backproject_profiles(
    profiles: RangeProfiles,
    grid: ImageGrid,
    azimuth_window: str = "hann",
    *,
    taylor_nbar: int = 4,
    taylor_sll_db: float = 35.0,
    interpolation: str = "linear",
    sinc_half_width: int = 8,
    tile_pixels: int = 65_536,
) -> ComplexArray:
    """Coherent geometric backprojection with bounded, flat image tiles.

    Temporary image arrays contain at most ``tile_pixels`` values, independent
    of the full image size. The returned full complex128 image is still resident.
    Geometry already accounts for each pulse's changing range; no extra RCMC is
    applied. Linear interpolation remains the default.
    """
    if not isinstance(profiles, RangeProfiles):
        raise TypeError("Backprojection requires range profiles and antenna geometry")
    profiles.validate()
    grid.validate()
    _validate_interpolation(interpolation, sinc_half_width)
    if (
        not isinstance(tile_pixels, int)
        or isinstance(tile_pixels, bool)
        or not 1 <= tile_pixels <= 1_048_576
    ):
        raise ValueError("Tile pixels must be an integer in [1, 1048576]")
    image = np.zeros((grid.y_m.size, grid.x_m.size), dtype=np.complex128)
    flat_image = image.ravel()
    heights = np.asarray(grid.z_m)
    taper = window_weights(
        profiles.samples.shape[0],
        azimuth_window,
        taylor_nbar=taylor_nbar,
        taylor_sll_db=taylor_sll_db,
    )
    for first in range(0, image.size, tile_pixels):
        last = min(first + tile_pixels, image.size)
        indices = np.arange(first, last)
        xx, yy = grid.x_m[indices % grid.x_m.size], grid.y_m[indices // grid.x_m.size]
        zz = heights if heights.ndim == 0 else heights.ravel()[first:last]
        for pulse, ant in enumerate(profiles.positions_m):
            delta = (
                np.sqrt((xx - ant[0]) ** 2 + (yy - ant[1]) ** 2 + (zz - ant[2]) ** 2)
                - profiles.reference_range_m[pulse]
            )
            sample_indices = (delta - profiles.range_start_m[pulse]) / profiles.range_step_m
            samples = _interpolate_profile(
                profiles.samples[pulse], sample_indices, interpolation, sinc_half_width
            )
            flat_image[first:last] += (
                taper[pulse] * samples * np.exp(1j * (4 * np.pi * profiles.carrier_hz / C) * delta)
            )
    return image / taper.sum()


def direct_reference(
    history: PhaseHistory,
    points_m: FloatArray,
    window: str = "hann",
    azimuth_window: str = "hann",
    *,
    taylor_nbar: int = 4,
    taylor_sll_db: float = 35.0,
) -> ComplexArray:
    """Small direct frequency sum, independent of range IFFT and interpolation."""
    history.validate()
    if points_m.ndim != 2 or points_m.shape[1] != 3 or not np.isfinite(points_m).all():
        raise ValueError("Expected finite (points, 3) reference coordinates")
    weight = window_weights(
        history.samples.shape[0],
        azimuth_window,
        taylor_nbar=taylor_nbar,
        taylor_sll_db=taylor_sll_db,
    )[:, None]
    weight = (
        weight
        * window_weights(
            history.samples.shape[1],
            window,
            taylor_nbar=taylor_nbar,
            taylor_sll_db=taylor_sll_db,
        )[None, :]
    )
    result = np.zeros(len(points_m), dtype=np.complex128)
    for i, point in enumerate(points_m):
        delta = np.linalg.norm(history.positions_m - point, axis=1) - history.reference_range_m
        phase = 4 * np.pi / C * delta[:, None] * history.frequency_hz[None, :]
        result[i] = np.sum(history.samples * weight * np.exp(1j * phase)) / weight.sum()
    return result
