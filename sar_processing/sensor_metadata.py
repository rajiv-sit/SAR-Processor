"""Inspect source geometry without substituting it for a calibrated sensor model.

GDAL's NITF schema defines field names and numeric limits. NGA SarPy's
``other_nitf.extract_sicd`` confirms ACFTB ground spacing in m/f, MENSRB
altitudes in feet above MSL, one-based reference pixels, and NED directions.
The specific MSL geoid is not supplied; these heights are never used as HAE.
Optional malformed values are preserved verbatim and reported as issues.
Malformed/ambiguous XML and unsafe allocations are rejected.
"""

import math
import re
from xml.etree import ElementTree

import numpy as np

GDAL_SCHEMA = "https://github.com/OSGeo/gdal/blob/master/frmts/nitf/data/nitf_spec.xml"
NGA_READER = "https://github.com/ngageoint/sarpy/blob/master/sarpy/io/complex/other_nitf.py"
RITSAR_READER = "https://github.com/dm6718/RITSAR/blob/master/ritsar/phsRead.py"
MAX_TRE_XML_BYTES = 1024 * 1024
SUPPORTED_TRES = frozenset({"ACFTB", "EXPLTB", "MENSRB"})


def _records(xml: str | None) -> dict[str, dict[str, str]]:
    if xml is None or not xml.strip():
        return {}
    if len(xml.encode("utf-8")) > MAX_TRE_XML_BYTES:
        raise ValueError("TRE XML exceeds the 1 MiB metadata limit")
    if re.search(r"<!\s*(?:DOCTYPE|ENTITY)", xml, re.IGNORECASE):
        raise ValueError("TRE XML must not contain DTDs or entities")
    try:
        root = ElementTree.fromstring(xml)
    except ElementTree.ParseError as error:
        raise ValueError("Malformed GDAL TRE XML") from error
    if root.tag != "tres" or sum(1 for _ in root.iter()) > 4096:
        raise ValueError("Expected a bounded GDAL tres document")
    result: dict[str, dict[str, str]] = {}
    for tre in root.findall("tre"):
        name = tre.get("name", "")
        if name not in SUPPORTED_TRES or tre.get("location", "image") != "image":
            continue
        if name in result:
            raise ValueError(f"Ambiguous repeated {name} image TRE")
        fields: dict[str, str] = {}
        for field in tre.findall("field"):
            key = field.get("name")
            if not key or key in fields or "value" not in field.attrib:
                raise ValueError(f"Invalid or repeated field in {name} TRE")
            fields[key] = field.attrib["value"]
        result[name] = fields
    return result


def parse_sandia_sensor_metadata(
    xml: str | None, image_shape: tuple[int, int] | None = None
) -> dict:
    """Return JSON-ready raw TREs, validated normalized values, and limitations.

    ``image_shape`` is (rows, columns), used only to check reference pixels.
    Numeric zero accuracy fields remain raw metadata, not a precision claim.
    Unsupported units/formats remain in ``raw_tres`` and are listed in ``issues``.
    Invalid optional values never become usable normalized measurements.
    """
    if image_shape is not None and (
        len(image_shape) != 2 or any(type(n) is not int or n < 1 for n in image_shape)
    ):
        raise ValueError("Image shape must contain two positive integer dimensions")
    records = _records(xml)
    normalized: dict = {}
    issues: list[str] = []

    def raw(tre: str, field: str) -> str:
        return records.get(tre, {}).get(field, "").strip()

    def number(tre: str, field: str, lower: float, upper: float) -> float | None:
        value = raw(tre, field)
        if not value:
            return None
        try:
            parsed = float(value)
        except ValueError:
            issues.append(f"{tre}.{field}: invalid numeric value")
            return None
        if not math.isfinite(parsed) or not lower <= parsed <= upper:
            issues.append(f"{tre}.{field}: outside finite bounds [{lower}, {upper}]")
            return None
        return parsed

    def put(key: str, value, units: str, *fields: str, **extra) -> None:
        normalized[key] = {"value": value, "units": units, "source_fields": list(fields), **extra}

    def position(tre: str, field: str, key: str) -> None:
        value = raw(tre, field)
        if not value:
            return
        match = re.fullmatch(r"([+-]\d{2}\.\d+)([+-]\d{3}\.\d+)", value)
        if match is None:
            issues.append(f"{tre}.{field}: unsupported signed decimal location format")
            return
        latitude, longitude = map(float, match.groups())
        if not -90 <= latitude <= 90 or not -180 <= longitude <= 180:
            issues.append(f"{tre}.{field}: latitude or longitude outside physical bounds")
            return
        put(
            key, [longitude, latitude], "degrees", f"{tre}.{field}", axis_order="longitude,latitude"
        )

    # ACFTB explicitly declares units. Never treat a blank or unfamiliar code as metres.
    for field, unit_field, key in (
        ("ROW_SPACING", "ROW_SPACING_UNITS", "row_ground_spacing_m"),
        ("COL_SPACING", "COL_SPACING_UNITS", "column_ground_spacing_m"),
        ("ENTELV", "ELV_UNIT", "entry_elevation_m"),
        ("EXITELV", "ELV_UNIT", "exit_elevation_m"),
    ):
        value = number("ACFTB", field, -100_000, 1_000_000)
        if value is None:
            continue
        unit = raw("ACFTB", unit_field).lower()
        if unit not in {"m", "f"}:
            issues.append(f"ACFTB.{field}: unresolved units {unit!r}")
            continue
        metres = value * (0.3048 if unit == "f" else 1.0)
        if "SPACING" in field and metres <= 0:
            issues.append(f"ACFTB.{field}: spacing must be positive")
            continue
        put(key, metres, "m", f"ACFTB.{field}", f"ACFTB.{unit_field}", source_units=unit)
    position("ACFTB", "ENTLOC", "entry_location_lonlat")
    position("ACFTB", "EXITLOC", "exit_location_lonlat")
    position("MENSRB", "ACFT_LOC", "aircraft_location_lonlat")
    position("MENSRB", "RP_LOC", "reference_location_lonlat")
    for field, key, lower, upper in (
        ("ACFT_ALT", "aircraft_altitude_msl_m", 0.0, 999999.0),
        ("RP_ELV", "reference_elevation_msl_m", -1000.0, 30000.0),
    ):
        value = number("MENSRB", field, lower, upper)
        if value is not None:
            put(
                key,
                value * 0.3048,
                "m",
                f"MENSRB.{field}",
                source_units="ft",
                vertical_datum="MSL; specific geoid unspecified",
                convention_source=NGA_READER,
            )
    for field, key, lower, upper in (
        ("ANGLE_TO_NORTH", "angle_to_north_deg", 0, 359.999),
        ("SQUINT_ANGLE", "squint_angle_deg", -60, 85),
        ("GRAZE_ANG", "graze_angle_deg", 0, 90),
        ("SLOPE_ANG", "slope_angle_deg", 0, 90),
    ):
        value = number("EXPLTB", field, lower, upper)
        if value is not None:
            put(key, value, "degrees", f"EXPLTB.{field}")
    for field, key, axis in (
        ("RP_ROW", "reference_row_index", 0),
        ("RP_COL", "reference_column_index", 1),
    ):
        value = number("MENSRB", field, 1, 99999)
        if value is None:
            continue
        if value != int(value) or (image_shape is not None and value > image_shape[axis]):
            issues.append(f"MENSRB.{field}: reference pixel outside integer image indices")
            continue
        put(key, int(value) - 1, "pixels", f"MENSRB.{field}", index_base=0)
    vectors: dict[str, list[float]] = {}
    for prefix, key in (
        ("C_R", "range_direction_ned"),
        ("C_AZ", "azimuth_direction_ned"),
        ("C_AL", "altitude_direction_ned"),
    ):
        fields = [f"{prefix}_{component}" for component in ("NC", "EC", "DC")]
        values = [number("MENSRB", field, -1, 1) for field in fields]
        if all(value is None for value in values):
            continue
        if any(value is None for value in values):
            issues.append(f"MENSRB.{prefix}: incomplete direction vector")
            continue
        vector = [float(value) for value in values if value is not None]
        norm = math.sqrt(sum(value * value for value in vector))
        if abs(norm - 1) > 0.005:
            issues.append(f"MENSRB.{prefix}: direction vector is not unit length")
            continue
        vectors[key] = vector
        put(key, vector, "unitless", *(f"MENSRB.{field}" for field in fields), norm=norm)
    keys = list(vectors)
    for i, key in enumerate(keys):
        for other in keys[i + 1 :]:
            if abs(sum(a * b for a, b in zip(vectors[key], vectors[other], strict=True))) > 0.005:
                issues.append(f"MENSRB: {key} and {other} are not orthogonal")
    cosine = number("MENSRB", "COSGRZ", 0, 1)
    if cosine is not None:
        put("cosine_graze", cosine, "unitless", "MENSRB.COSGRZ")
        if "graze_angle_deg" in normalized:
            expected = math.cos(math.radians(normalized["graze_angle_deg"]["value"]))
            if abs(expected - cosine) > 0.002:
                issues.append("MENSRB.COSGRZ and EXPLTB.GRAZE_ANG are inconsistent")
    return {
        "schema_version": 1,
        "source_domain": "GDAL xml:TRE",
        "available_tres": sorted(records),
        "raw_tres": records,
        "normalized": normalized,
        "issues": issues,
        "validation_status": "issues_found"
        if issues
        else ("checked" if records else "unavailable"),
        "convention_sources": [GDAL_SCHEMA, NGA_READER],
        "accuracy_status": "not independently established; zero accuracy fields are not proof",
        "terrain_projection": {
            "ready": False,
            "required": [
                "Validated image-to-range/Doppler or equivalent SAR sensor model",
                "Time-dependent sensor trajectory and image formation conventions",
                "DEM coverage and resolution appropriate to the scene",
                "Verified horizontal datum and ellipsoidal heights, including MSL/geoid conversion",
                "Independent ground control and terrain-projection accuracy checks",
            ],
            "reason": "Summary TRE values alone do not establish a complete terrain sensor model",
        },
    }


def gotcha_autofocus_metadata(data_af, pulse_count: int) -> dict:
    """Preserve optional producer corrections without guessing their application.

    RITSAR loads fp directly. Its reader supplies no af application contract;
    the names alone do not establish sign, units or whether fp was corrected.
    Reject malformed corrections instead of allowing silent pulse misalignment.
    """
    if type(pulse_count) is not int or not 1 <= pulse_count <= 32_000_000:
        raise ValueError("Autofocus metadata requires a bounded positive pulse count")
    result = {
        "present": data_af is not None,
        "applied_by_importer": False,
        "upstream_application_state": "unknown",
        "sign_convention": "unknown",
        "policy": "preserve_only; no automatic correction without a verified source contract",
        "reference": RITSAR_READER,
        "fields": {},
    }
    if data_af is None:
        return result
    if not isinstance(data_af, dict) or not {"r_correct", "ph_correct"} <= data_af.keys():
        raise ValueError("GOTCHA af must contain r_correct and ph_correct")
    fields = {}
    for name in ("r_correct", "ph_correct"):
        values = np.atleast_1d(np.asarray(data_af[name]).squeeze())
        if values.shape != (pulse_count,) or values.dtype.kind not in "fiu":
            raise ValueError(f"GOTCHA af.{name} must contain one real value per pulse")
        if not np.all(np.isfinite(values)):
            raise ValueError(f"GOTCHA af.{name} contains nonfinite values")
        fields[name] = {
            "values": values.tolist(),
            "units": "unknown",
            "min": float(values.min()),
            "max": float(values.max()),
        }
    result["fields"] = fields
    return result
