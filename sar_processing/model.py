"""Explicit SAR processing stages and their physical units."""

from dataclasses import dataclass, field
from typing import Any

import numpy as np
import numpy.typing as npt

FloatArray = npt.NDArray[np.float64]
ComplexArray = npt.NDArray[np.complex128]
C = 299_792_458.0
MAX_COMPLEX_SAMPLES = 32_000_000


def complex_matrix(value: Any) -> ComplexArray:
    data = np.asarray(value)
    if data.ndim != 2 or min(data.shape) < 1 or not np.iscomplexobj(data):
        raise ValueError("Expected a nonempty complex pulse-by-sample matrix")
    if data.size > MAX_COMPLEX_SAMPLES:
        raise ValueError("Input allocation exceeds 32 million complex samples")
    if not np.isfinite(data).all():
        raise ValueError("Complex samples must be finite")
    return data.astype(np.complex128, copy=False)


def finite_array(value: Any, shape: tuple[int, ...], name: str) -> FloatArray:
    data = np.asarray(value)
    if data.shape != shape:
        raise ValueError(f"{name} must be finite with shape {shape}")
    if data.dtype.kind not in "biuf":
        raise ValueError(f"{name} must be real-valued numeric data")
    data = data.astype(np.float64, copy=False)
    if not np.isfinite(data).all():
        raise ValueError(f"{name} must be finite with shape {shape}")
    return data


def real_scalar(value: Any, name: str) -> float:
    data = np.asarray(value)
    if data.ndim != 0 or data.dtype.kind not in "biuf":
        raise ValueError(f"{name} must be a real-valued numeric scalar")
    return float(data)


@dataclass
class PhaseHistory:
    samples: ComplexArray
    frequency_hz: FloatArray
    positions_m: FloatArray
    reference_range_m: FloatArray
    azimuth_deg: FloatArray
    sources: list[dict[str, Any]] = field(default_factory=list)

    def validate(self) -> None:
        self.samples = complex_matrix(self.samples)
        pulses, count = self.samples.shape
        self.frequency_hz = finite_array(self.frequency_hz, (count,), "Frequency")
        if count < 2 or np.any(self.frequency_hz <= 0) or np.any(np.diff(self.frequency_hz) <= 0):
            raise ValueError("At least two positive, increasing frequencies are required")
        self.positions_m = finite_array(self.positions_m, (pulses, 3), "Antenna positions")
        self.reference_range_m = finite_array(self.reference_range_m, (pulses,), "Reference range")
        self.azimuth_deg = finite_array(self.azimuth_deg, (pulses,), "Azimuth")
        if np.any(self.reference_range_m < 0):
            raise ValueError("Reference ranges must be nonnegative")


@dataclass
class RawEcho:
    """Uncompressed baseband echoes: a*s(t-2R/c)*exp(-j*4*pi*fc*R/c)."""

    samples: ComplexArray
    replica: ComplexArray
    sampling_rate_hz: float
    carrier_hz: float
    receive_start_s: FloatArray
    positions_m: FloatArray

    def validate(self) -> None:
        self.samples = complex_matrix(self.samples)
        pulses, count = self.samples.shape
        replica = np.asarray(self.replica)
        if replica.ndim != 1 or not 1 <= replica.size <= count or not np.iscomplexobj(replica):
            raise ValueError(
                "A complex reference chirp no longer than the receive window is required"
            )
        if not np.isfinite(replica).all() or np.vdot(replica, replica).real <= 0:
            raise ValueError("Reference chirp must have finite, nonzero energy")
        self.replica = replica.astype(np.complex128)
        self.sampling_rate_hz = real_scalar(self.sampling_rate_hz, "Sampling rate")
        self.carrier_hz = real_scalar(self.carrier_hz, "Carrier frequency")
        if (
            not np.isfinite([self.sampling_rate_hz, self.carrier_hz]).all()
            or min(self.sampling_rate_hz, self.carrier_hz) <= 0
        ):
            raise ValueError("Sampling rate and carrier frequency must be positive and finite")
        self.receive_start_s = finite_array(self.receive_start_s, (pulses,), "Receive start time")
        self.positions_m = finite_array(self.positions_m, (pulses, 3), "Antenna positions")
        if np.any(self.receive_start_s < 0):
            raise ValueError("Receive start times must be nonnegative")


@dataclass
class RangeProfiles:
    """Complex range samples; range is relative to reference_range_m per pulse."""

    samples: ComplexArray
    positions_m: FloatArray
    reference_range_m: FloatArray
    range_start_m: FloatArray
    range_step_m: float
    carrier_hz: float

    def validate(self) -> None:
        self.samples = complex_matrix(self.samples)
        pulses, count = self.samples.shape
        if count < 2:
            raise ValueError("Range interpolation requires at least two samples")
        self.positions_m = finite_array(self.positions_m, (pulses, 3), "Antenna positions")
        self.reference_range_m = finite_array(self.reference_range_m, (pulses,), "Reference range")
        self.range_start_m = finite_array(self.range_start_m, (pulses,), "Range start")
        self.range_step_m = real_scalar(self.range_step_m, "Range spacing")
        self.carrier_hz = real_scalar(self.carrier_hz, "Carrier frequency")
        if (
            not np.isfinite([self.range_step_m, self.carrier_hz]).all()
            or min(self.range_step_m, self.carrier_hz) <= 0
        ):
            raise ValueError("Range spacing and carrier must be positive and finite")


@dataclass
class ImageGrid:
    x_m: FloatArray
    y_m: FloatArray
    z_m: float | FloatArray = 0.0

    def height_at(self, row: int, col: int) -> float:
        return float(self.z_m[row, col]) if isinstance(self.z_m, np.ndarray) else self.z_m

    def validate(self) -> None:
        for axis in (self.x_m, self.y_m):
            if axis.dtype.kind not in "biuf":
                raise ValueError("Image axes must be real-valued")
            if axis.ndim != 1 or axis.size < 2 or not np.isfinite(axis).all():
                raise ValueError("Image axes must be finite vectors with at least two pixels")
            if np.any(np.diff(axis) <= 0):
                raise ValueError("Image axes must be strictly increasing")
        heights = np.asarray(self.z_m)
        if heights.ndim == 0:
            self.z_m = real_scalar(self.z_m, "Imaging height")
        else:
            self.z_m = finite_array(heights, (self.y_m.size, self.x_m.size), "Imaging height")
        if not np.isfinite(self.z_m).all() or self.x_m.size * self.y_m.size > 16_777_216:
            raise ValueError("Invalid imaging height or image exceeds 16 million pixels")
