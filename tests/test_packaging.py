"""Repo invariants for the Flatpak manifest and the .desktop entry."""
from __future__ import annotations

import json
from pathlib import Path

import pytest

FLATPAK_MANIFEST = Path("packaging/flatpak/io.github.blairq.broadcastappliance.json")
DESKTOP_FILE = Path("packaging/BroadcastAppliance.desktop")


@pytest.fixture(scope="module")
def manifest_path(repo_root: Path) -> Path:
    """Path of the flatpak manifest inside the repository."""
    return repo_root / FLATPAK_MANIFEST


@pytest.fixture(scope="module")
def manifest(manifest_path: Path) -> dict:
    """The parsed flatpak manifest (fails the test if it is not valid JSON)."""
    assert manifest_path.is_file(), f"missing flatpak manifest: {manifest_path}"
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def test_flatpak_manifest_is_valid_json(manifest_path: Path) -> None:
    """The manifest must exist and parse as JSON."""
    assert manifest_path.is_file(), f"missing flatpak manifest: {manifest_path}"
    try:
        json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        pytest.fail(f"flatpak manifest is not valid JSON: {exc}")


@pytest.mark.parametrize("key", ["app-id", "runtime", "modules"])
def test_manifest_has_required_key(manifest: dict, key: str) -> None:
    """The manifest must carry the keys flatpak-builder needs to build."""
    assert key in manifest, f"flatpak manifest missing required key: {key!r}"


def test_manifest_app_id_matches_filename(manifest: dict) -> None:
    """The app-id must match the manifest file name (flatpak convention)."""
    assert manifest["app-id"] == "io.github.blairq.broadcastappliance"


def test_manifest_modules_is_non_empty_list(manifest: dict) -> None:
    """At least one module (the app itself) must be declared."""
    modules = manifest["modules"]
    assert isinstance(modules, list) and modules, "modules must be a non-empty list"
    names = [m.get("name") for m in modules]
    assert "broadcastappliance" in names, f"app module missing from {names}"


def test_desktop_file_exists(repo_root: Path) -> None:
    """The .desktop entry must be present in packaging/."""
    assert (repo_root / DESKTOP_FILE).is_file(), f"missing {DESKTOP_FILE}"


def test_desktop_declares_type_application(repo_root: Path) -> None:
    """The .desktop entry must declare Type=Application."""
    text = (repo_root / DESKTOP_FILE).read_text(encoding="utf-8")
    lines = [ln.strip() for ln in text.splitlines()]
    assert "Type=Application" in lines, "Type=Application missing from .desktop file"


def test_desktop_has_exec_line(repo_root: Path) -> None:
    """The .desktop entry must carry a non-empty Exec= line."""
    text = (repo_root / DESKTOP_FILE).read_text(encoding="utf-8")
    exec_lines = [
        ln.strip() for ln in text.splitlines() if ln.strip().startswith("Exec=")
    ]
    assert exec_lines, "Exec= line missing from .desktop file"
    assert len(exec_lines[0]) > len("Exec="), "Exec= line is empty"
