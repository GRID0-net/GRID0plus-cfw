#!/usr/bin/env python3
"""Compatibility entry point for the component-aware update regression tests."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("test_update_components.py")), run_name="__main__")
