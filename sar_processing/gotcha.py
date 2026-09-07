"""Strict GOTCHA import: frequency in Hz and geometry in metres."""

import hashlib
from math import prod
from pathlib import Path
from zipfile import BadZipFile, ZipFile

import numpy as np
from scipy.io import loadmat

from .model import MAX_COMPLEX_SAMPLES, PhaseHistory, RawEcho
from .sensor_metadata import gotcha_autofocus_metadata

MAX_INPUT_BYTES = 512 * 1024 * 1024


def source_record(path: Path) -> dict:
    with path.open("rb") as source:
        sha = hashlib.file_digest(source, "sha256").hexdigest()
    return {"file": path.name, "bytes": path.stat().st_size, "sha256": sha}


def load_gotcha(
    directory: Path,
    first_az: int = 1,
    last_az: int = 4,
    pass_number: int = 1,
    polarization: str = "HH",
) -> PhaseHistory:
    if not 1 <= first_az <= last_az <= 360 or pass_number < 1:
        raise ValueError("Require 1 <= first azimuth <= last azimuth <= 360 and pass >= 1")
    if polarization not in {"HH", "HV", "VH", "VV"} or not directory.is_dir():
        raise ValueError("Require a GOTCHA directory and valid polarization")
    selected: list[Path] = []
    for azimuth in range(first_az, last_az + 1):
        name = f"data_3dsar_pass{pass_number}_az{azimuth:03d}_{polarization}.mat"
        paths = sorted(directory.rglob(name))
        if len(paths) != 1:
            raise ValueError(f"Expected exactly one {name}, found {len(paths)}")
        selected.append(paths[0])
    if sum(path.stat().st_size for path in selected) > MAX_INPUT_BYTES:
        raise ValueError("Selected GOTCHA files exceed the 512 MiB input limit")
    blocks: list[PhaseHistory] = []
    total_samples = 0
    for path in selected:
        data = loadmat(path, simplify_cells=True).get("data")
        if (
            not isinstance(data, dict)
            or not {"fp", "freq", "x", "y", "z", "r0", "th"} <= data.keys()
        ):
            raise ValueError(f"Missing GOTCHA metadata in {path.name}")
        freq = np.atleast_1d(data["freq"])
        r0 = np.atleast_1d(data["r0"])
        samples = np.asarray(data["fp"])
        if samples.ndim == 1 and r0.size == 1:
            samples = samples[:, None]
        if samples.shape != (freq.size, r0.size):
            raise ValueError("GOTCHA fp must be indexed [frequency, pulse]")
        total_samples += samples.size
        if total_samples > MAX_COMPLEX_SAMPLES:
            raise ValueError("Selected GOTCHA aperture exceeds 32 million complex samples")
        coordinates = [np.atleast_1d(data[k]) for k in ("x", "y", "z")]
        if any(coordinate.shape != r0.shape for coordinate in coordinates):
            raise ValueError("Antenna positions must match the pulse count")
        positions = np.column_stack(coordinates)
        source = source_record(path)
        source["vendor_autofocus"] = gotcha_autofocus_metadata(data.get("af"), int(r0.size))
        block = PhaseHistory(samples.T, freq, positions, r0, np.atleast_1d(data["th"]), [source])
        block.validate()
        if blocks and not np.array_equal(freq, blocks[0].frequency_hz):
            raise ValueError("Frequency grids differ between aperture files")
        blocks.append(block)
    result = PhaseHistory(
        np.concatenate([b.samples for b in blocks]),
        blocks[0].frequency_hz,
        np.concatenate([b.positions_m for b in blocks]),
        np.concatenate([b.reference_range_m for b in blocks]),
        np.concatenate([b.azimuth_deg for b in blocks]),
        [s for b in blocks for s in b.sources],
    )
    result.validate()
    return result


def validate_raw_archive(path: Path) -> None:
    """Bound archive expansion and declared NPY allocations before array loading."""
    if path.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError("Raw NPZ file exceeds the 512 MiB input limit")
    try:
        with ZipFile(path) as archive:
            members = archive.infolist()
            if sum(member.file_size for member in members) > MAX_INPUT_BYTES:
                raise ValueError("Raw NPZ expansion exceeds the 512 MiB input limit")
            for member in members:
                with archive.open(member) as source:
                    version = np.lib.format.read_magic(source)
                    if version == (1, 0):
                        shape, _, dtype = np.lib.format.read_array_header_1_0(source)
                    elif version == (2, 0):
                        shape, _, dtype = np.lib.format.read_array_header_2_0(source)
                    else:
                        raise ValueError("Raw NPZ supports NPY array versions 1 and 2")
                    if dtype.hasobject:
                        raise ValueError("Object arrays cannot be loaded when allow_pickle=False")
                    if any(dimension < 0 for dimension in shape):
                        raise ValueError("Raw NPZ array dimensions must be nonnegative")
                    count = prod(shape)
                    if member.filename == "samples.npy" and count > MAX_COMPLEX_SAMPLES:
                        raise ValueError("Raw NPZ samples exceed 32 million complex samples")
                    if count * dtype.itemsize > MAX_INPUT_BYTES:
                        raise ValueError("Raw NPZ declared array exceeds the 512 MiB input limit")
                    if count * dtype.itemsize != member.file_size - source.tell():
                        raise ValueError("Raw NPZ array payload does not match its declared shape")
    except BadZipFile as error:
        raise ValueError("Expected a valid raw echo NPZ archive") from error


def load_raw_echo(path: Path) -> RawEcho:
    validate_raw_archive(path)
    with np.load(path, allow_pickle=False) as data:
        if str(data["stage"].item()) != "raw_echo":
            raise ValueError("NPZ is not declared raw_echo; focused images cannot be recompressed")
        result = RawEcho(
            data["samples"],
            data["replica"],
            float(data["sampling_rate_hz"]),
            float(data["carrier_hz"]),
            data["receive_start_s"],
            data["positions_m"],
        )
    result.validate()
    return result
