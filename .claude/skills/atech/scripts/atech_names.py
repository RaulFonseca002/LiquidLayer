"""atech_names.py — commercial / human names for Atech modules.

The SDK's `module.yaml` `name:` field is the authoritative commercial name (it
carries the exact part, e.g. "I2S Speaker (MAX98357A)"). This module surfaces
that name everywhere the skill shows a module, plus a SHORT label that fits an
ASCII layout box. Import-only; no CLI.

`short_name` / `full_name` accept either a ModuleSpec (with `.name` / `.id`) or
a plain module-id string, so callers can use whichever they have.
"""
from __future__ import annotations

import re

# Short labels for the compact board diagram, keyed by module id. Anything not
# listed falls back to the SDK name with the parenthetical part number stripped.
SHORT = {
    "st7735_tft":      "TFT display",
    "ruview_csi":      "WiFi sensor",
    "button":          "Button",
    "neopixel":        "NeoPixel LEDs",
    "speaker":         "Speaker",
    "microphone":      "Microphone",
    "aht20":           "Temp/Humidity",
    "scd40_air_quality": "CO2 sensor",
    "icm40608":        "Motion (IMU)",
    "icm40608_imu":    "Motion (IMU)",
    "pir":             "Motion (PIR)",
    "distance_sensor": "Distance",
    "vl53l5cx_distance": "Distance",
    "dc_motor":        "DC motor",
    "stepper_motor":   "Stepper motor",
    "rotary_encoder":  "Knob",
    "robot_arm":       "Robot arm",
}


def _id(spec) -> str:
    if isinstance(spec, str):
        return spec
    return getattr(spec, "id", None) or (spec.get("id") if isinstance(spec, dict) else "") or ""


def _sdk_name(spec) -> str | None:
    if isinstance(spec, str):
        return None
    return getattr(spec, "name", None) or (spec.get("name") if isinstance(spec, dict) else None)


def full_name(spec) -> str:
    """Commercial name as the catalog states it, e.g. 'I2S Speaker (MAX98357A)'."""
    return _sdk_name(spec) or _id(spec) or "unknown module"


def short_name(spec) -> str:
    """Compact commercial label for a layout box, e.g. 'Speaker', 'TFT display'."""
    mid = _id(spec)
    if mid in SHORT:
        return SHORT[mid]
    name = _sdk_name(spec)
    if not name:
        return mid or "module"
    # strip a trailing "(...)" part-number and any leading bare part token
    name = re.sub(r"\s*\([^)]*\)\s*$", "", name).strip()
    return name
