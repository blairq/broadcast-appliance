"""Repo invariants for the bundled LSP VST3 plugin.

The bundle must keep the real VST3 on-disk layout so that hosts (and the
flatpak post-install step) find the plugin where they expect it.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

BUNDLE = Path("assets/audio-plugins/lsp-plugins.vst3")
PLUGIN_LIB = BUNDLE / "Contents" / "x86_64-linux" / "lsp-plugins.so"
MODULE_INFO = BUNDLE / "Contents" / "Resources" / "moduleinfo.json"


def test_bundle_directory_exists(repo_root: Path) -> None:
    """The .vst3 bundle directory must be present in the repo."""
    assert (repo_root / BUNDLE).is_dir(), f"missing VST3 bundle: {BUNDLE}"


def test_plugin_library_present(repo_root: Path) -> None:
    """The Linux plugin shared object must sit in the VST3 arch dir."""
    path = repo_root / PLUGIN_LIB
    assert path.is_file(), f"missing plugin library: {PLUGIN_LIB}"
    assert path.stat().st_size > 0, "lsp-plugins.so is empty"


def test_moduleinfo_present_and_valid(repo_root: Path) -> None:
    """moduleinfo.json must exist, parse, and describe the lsp-plugins module."""
    path = repo_root / MODULE_INFO
    assert path.is_file(), f"missing moduleinfo.json: {MODULE_INFO}"
    info = json.loads(path.read_text(encoding="utf-8"))
    assert info.get("Name") == "lsp-plugins", "moduleinfo Name mismatch"
    classes = info.get("Classes")
    assert isinstance(classes, list) and classes, "moduleinfo has no Classes"
