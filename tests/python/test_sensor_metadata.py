"""Source geometry units, bounds, provenance, and correction-state regressions."""

import json
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, tostring

import numpy as np
import pytest
import rasterio

from sar_processing.sensor_metadata import (
    MAX_TRE_XML_BYTES,
    gotcha_autofocus_metadata,
    parse_sandia_sensor_metadata,
)


def xml_for(records):
    root = Element("tres")
    for name, fields in records.items():
        tre = SubElement(root, "tre", name=name, location="image")
        for key, value in fields.items():
            SubElement(tre, "field", name=key, value=str(value))
    return tostring(root, encoding="unicode")


@pytest.fixture
def geometry():
    return {
        "ACFTB": {
            "ROW_SPACING": "00.0770",
            "ROW_SPACING_UNITS": "m",
            "COL_SPACING": "00.0714",
            "COL_SPACING_UNITS": "m",
            "ENTELV": "+01646",
            "ELV_UNIT": "m",
            "ENTLOC": "+35.15500303-106.55589243",
            "EXITLOC": "+00.00000000+000.00000000",
            "EXITELV": "",
            "UNKNOWN_SENSOR_FIELD": " preserve me ",
            "LOC_ACCY": "000.00",
        },
        "EXPLTB": {
            "GRAZE_ANG": "28.58",
            "SLOPE_ANG": "28.58",
            "SQUINT_ANGLE": "+00.000",
            "ANGLE_TO_NORTH": "332.999",
        },
        "MENSRB": {
            "ACFT_LOC": "+35.13885676-106.59445361",
            "ACFT_ALT": "012462",
            "RP_LOC": "+35.15500303-106.55589243",
            "RP_ELV": "+05400",
            "RP_ROW": "02014",
            "RP_COL": "02868",
            "COSGRZ": "0.87819",
            "C_R_NC": "-0.3986985",
            "C_R_EC": "-0.7824651",
            "C_R_DC": "-0.4783178",
            "C_AZ_NC": "-0.891001",
            "C_AZ_EC": "+0.454002",
            "C_AZ_DC": "+0.000000",
            "C_AL_NC": "+0.217157",
            "C_AL_EC": "+0.426182",
            "C_AL_DC": "-0.878187",
            "RGCRP": "0014755",
            "RP_LOC_ACCY": "000000",
        },
    }


def test_realistic_sandia_geometry_has_explicit_units_and_no_fabricated_accuracy(geometry):
    report = parse_sandia_sensor_metadata(xml_for(geometry), (4028, 5736))
    normalized = report["normalized"]
    assert report["issues"] == []
    assert report["validation_status"] == "checked"
    assert report["available_tres"] == ["ACFTB", "EXPLTB", "MENSRB"]
    assert report["raw_tres"] == geometry
    assert normalized["aircraft_altitude_msl_m"]["value"] == pytest.approx(3798.4176)
    assert normalized["reference_elevation_msl_m"]["value"] == pytest.approx(1645.92)
    assert normalized["reference_elevation_msl_m"]["source_units"] == "ft"
    assert "geoid unspecified" in normalized["reference_elevation_msl_m"]["vertical_datum"]
    assert normalized["row_ground_spacing_m"]["value"] == 0.077
    assert normalized["column_ground_spacing_m"]["value"] == 0.0714
    assert normalized["reference_row_index"]["value"] == 2013
    assert normalized["reference_column_index"]["value"] == 2867
    assert normalized["aircraft_location_lonlat"]["value"] == [-106.59445361, 35.13885676]
    assert normalized["exit_location_lonlat"]["value"] == [0.0, 0.0]
    assert normalized["range_direction_ned"]["value"] == [-0.3986985, -0.7824651, -0.4783178]
    assert not report["terrain_projection"]["ready"]
    assert "zero accuracy fields" in report["accuracy_status"]
    assert not any("accuracy" in field for field in normalized)
    assert "RGCRP" in report["raw_tres"]["MENSRB"]  # Units not established by reader source.
    assert "slant_range_m" not in normalized
    assert json.loads(json.dumps(report, allow_nan=False)) == report


@pytest.mark.parametrize(
    "xml",
    [
        None,
        "",
        " \t\n",
        "<tres/>",
        xml_for({"OTHER": {"A": "x"}}),
        '<tres><tre name="ACFTB" location="file"/></tres>',
    ],
)
def test_missing_optional_metadata_is_explicitly_unavailable(xml):
    report = parse_sandia_sensor_metadata(xml)
    assert report["validation_status"] == "unavailable"
    assert report["raw_tres"] == {}
    assert report["normalized"] == {}


@pytest.mark.parametrize(
    "xml,match",
    [
        ("<tres>", "Malformed"),
        ("<wrong/>", "bounded"),
        ('<!DOCTYPE tres [<!ENTITY bad "a">]><tres/>', "DTDs"),
        ('<!ENTITY x SYSTEM "file:///not-readable"><tres/>', "entities"),
        (" " * (MAX_TRE_XML_BYTES + 1) + "x", "1 MiB"),
        ("<tres>" + "<tre/>" * 4096 + "</tres>", "bounded"),
        ('<tres><tre name="ACFTB"/><tre name="ACFTB"/></tres>', "repeated"),
        ('<tres><tre name="ACFTB"><field value="0"/></tre></tres>', "Invalid"),
        ('<tres><tre name="ACFTB"><field name="A"/></tre></tres>', "Invalid"),
        (
            '<tres><tre name="ACFTB"><field name="A" value="0"/><field name="A" value="1"/></tre></tres>',
            "repeated",
        ),
    ],
    ids=[
        "truncated",
        "root",
        "doctype",
        "entity",
        "size",
        "nodes",
        "duplicate-tre",
        "nameless-field",
        "valueless-field",
        "duplicate-field",
    ],
)
def test_malformed_ambiguous_or_unsafe_xml_is_rejected(xml, match):
    with pytest.raises(ValueError, match=match):
        parse_sandia_sensor_metadata(xml)


@pytest.mark.parametrize("shape", [(0, 2), (-1, 2), (2, 0), (1.5, 2), (True, 2), (1,), (1, 2, 3)])
def test_invalid_image_dimensions(shape):
    with pytest.raises(ValueError, match="two positive"):
        parse_sandia_sensor_metadata(None, shape)


@pytest.mark.parametrize("unit", ["m", "f", "F", " M "])
def test_explicit_spacing_and_elevation_unit_conversion(unit):
    record = {
        "ACFTB": {"ROW_SPACING": "2", "ROW_SPACING_UNITS": unit, "EXITELV": "100", "ELV_UNIT": unit}
    }
    report = parse_sandia_sensor_metadata(xml_for(record))
    factor = 0.3048 if unit.lower() == "f" else 1
    assert report["normalized"]["row_ground_spacing_m"]["value"] == 2 * factor
    assert report["normalized"]["exit_elevation_m"]["value"] == 100 * factor
    assert not report["issues"]


@pytest.mark.parametrize("unit", ["", "unknown", "km"])
def test_unknown_units_are_preserved_and_do_not_become_metres(unit):
    records = {"ACFTB": {"ROW_SPACING": "2", "ROW_SPACING_UNITS": unit}}
    report = parse_sandia_sensor_metadata(xml_for(records))
    assert not report["normalized"]
    assert report["raw_tres"] == records
    assert "unresolved units" in report["issues"][0]


@pytest.mark.parametrize("value", ["0", "-1"])
def test_nonpositive_ground_spacing_is_not_accepted(value):
    report = parse_sandia_sensor_metadata(
        xml_for({"ACFTB": {"ROW_SPACING": value, "ROW_SPACING_UNITS": "m"}})
    )
    assert not report["normalized"]
    assert "spacing must be positive" in report["issues"][0]


@pytest.mark.parametrize("value", ["nan", "inf", "-inf", "bad", "-1", "90.1"])
def test_invalid_angles_preserve_source_and_exclude_normalized_value(value):
    records = {"EXPLTB": {"GRAZE_ANG": value}}
    report = parse_sandia_sensor_metadata(xml_for(records))
    assert report["raw_tres"] == records
    assert report["validation_status"] == "issues_found"
    assert report["normalized"] == {}


@pytest.mark.parametrize(
    "value,expected", [("+90.000000+180.000000", [180, 90]), ("-90.000000-180.000000", [-180, -90])]
)
def test_pole_and_dateline_positions(value, expected):
    report = parse_sandia_sensor_metadata(xml_for({"MENSRB": {"ACFT_LOC": value}}))
    assert report["normalized"]["aircraft_location_lonlat"]["value"] == expected


@pytest.mark.parametrize(
    "value,match",
    [
        ("35N106W", "format"),
        ("+91.000000-106.000000", "bounds"),
        ("+35.000000-181.000000", "bounds"),
    ],
)
def test_unknown_location_formats_and_out_of_bounds_positions(value, match):
    report = parse_sandia_sensor_metadata(xml_for({"MENSRB": {"RP_LOC": value}}))
    assert report["normalized"] == {}
    assert match in report["issues"][0]


@pytest.mark.parametrize("value", ["0", "-1", "9999999", "1.5", "9"])
def test_invalid_reference_pixel_is_not_silently_clamped(value):
    report = parse_sandia_sensor_metadata(xml_for({"MENSRB": {"RP_ROW": value}}), (8, 9))
    assert not report["normalized"]
    assert "RP_ROW" in report["issues"][0]


def test_reference_pixels_with_no_shape_and_with_boundary_shape():
    records = {"MENSRB": {"RP_ROW": "1", "RP_COL": "2"}}
    for shape in (None, (1, 2)):
        report = parse_sandia_sensor_metadata(xml_for(records), shape)
        assert report["normalized"]["reference_row_index"]["value"] == 0
        assert report["normalized"]["reference_column_index"]["value"] == 1
        assert not report["issues"]


@pytest.mark.parametrize(
    "fields,match",
    [
        ({"C_R_NC": "1"}, "incomplete"),
        ({"C_R_NC": "0", "C_R_EC": "0", "C_R_DC": "0"}, "unit length"),
        ({"C_R_NC": "2", "C_R_EC": "0", "C_R_DC": "0"}, "bounds"),
    ],
)
def test_invalid_direction_vectors_are_reported(fields, match):
    report = parse_sandia_sensor_metadata(xml_for({"MENSRB": fields}))
    assert not report["normalized"]
    assert any(match in issue for issue in report["issues"])


def test_nonorthogonal_valid_length_directions_are_reported():
    records = {
        "MENSRB": {
            "C_R_NC": "1",
            "C_R_EC": "0",
            "C_R_DC": "0",
            "C_AZ_NC": "1",
            "C_AZ_EC": "0",
            "C_AZ_DC": "0",
        }
    }
    report = parse_sandia_sensor_metadata(xml_for(records))
    assert "not orthogonal" in report["issues"][0]
    assert report["validation_status"] == "issues_found"
    assert not report["terrain_projection"]["ready"]


def test_graze_cosine_consistency():
    report = parse_sandia_sensor_metadata(
        xml_for({"EXPLTB": {"GRAZE_ANG": "90"}, "MENSRB": {"COSGRZ": "1"}})
    )
    assert "inconsistent" in report["issues"][0]
    report = parse_sandia_sensor_metadata(xml_for({"MENSRB": {"COSGRZ": "0.5"}}))
    assert report["normalized"]["cosine_graze"]["value"] == 0.5
    assert not report["issues"]


@pytest.mark.parametrize("value", [None, {}, 0, {"r_correct": [1]}, {"ph_correct": [1]}])
def test_autofocus_missing_or_invalid_structure(value):
    if value is None:
        result = gotcha_autofocus_metadata(value, 1)
        assert not result["present"]
        assert not result["applied_by_importer"]
        assert result["fields"] == {}
    else:
        with pytest.raises(ValueError, match="must contain"):
            gotcha_autofocus_metadata(value, 1)


@pytest.mark.parametrize("count", [0, -1, 32_000_001, 1.0, True])
def test_autofocus_invalid_count(count):
    with pytest.raises(ValueError, match="pulse count"):
        gotcha_autofocus_metadata(None, count)


@pytest.mark.parametrize(
    "array",
    [
        np.array([1, 2]),
        np.zeros((2, 2)),
        np.array([1j]),
        np.array([True]),
        np.array(["1"]),
        np.array([1], object),
        np.array([np.inf]),
        np.array([np.nan]),
    ],
)
@pytest.mark.parametrize("field", ["r_correct", "ph_correct"])
def test_autofocus_requires_finite_real_per_pulse_alignment(array, field):
    af = {"r_correct": [0.2], "ph_correct": [0.8], field: array}
    with pytest.raises(ValueError, match=field):
        gotcha_autofocus_metadata(af, 1)


@pytest.mark.parametrize("count", [1, 3])
def test_autofocus_corrections_preserved_exactly_with_unknown_state(count):
    af = {
        "r_correct": np.arange(count, dtype=np.float32)[None, :],
        "ph_correct": np.linspace(-3, 3, count)[:, None],
    }
    report = gotcha_autofocus_metadata(af, count)
    assert report["present"]
    assert not report["applied_by_importer"]
    assert report["upstream_application_state"] == "unknown"
    assert report["sign_convention"] == "unknown"
    for name in af:
        assert report["fields"][name]["values"] == af[name].ravel().tolist()
        assert report["fields"][name]["min"] == af[name].min()
        assert report["fields"][name]["max"] == af[name].max()
        assert report["fields"][name]["units"] == "unknown"
    json.dumps(report, allow_nan=False)


@pytest.mark.measured
def test_all_local_sandia_metadata_is_preserved_and_schema_violations_reported():
    files = sorted(Path("data/sandia/FARAD_X_BAND").glob("*.ntf"))
    if len(files) != 30:
        pytest.skip("The separately downloaded 30-file Sandia collection is unavailable")
    for path in files:
        with rasterio.open(path) as source:
            report = parse_sandia_sensor_metadata(
                source.tags(ns="xml:TRE")["xml:TRE"], (source.height, source.width)
            )
        assert report["available_tres"] == ["ACFTB", "EXPLTB", "MENSRB"]
        if "PS0016_PT000001" in path.name or "PS0017_PT000002" in path.name:
            assert report["raw_tres"]["EXPLTB"]["ANGLE_TO_NORTH"] == "360.000"
            assert len(report["issues"]) == 1
            assert "ANGLE_TO_NORTH" in report["issues"][0]
            assert "angle_to_north_deg" not in report["normalized"]
        else:
            assert report["issues"] == [], (path.name, report["issues"])
        assert report["normalized"]["row_ground_spacing_m"]["value"] > 0
        assert report["normalized"]["column_ground_spacing_m"]["value"] > 0
        assert not report["terrain_projection"]["ready"]
        json.dumps(report, allow_nan=False)
