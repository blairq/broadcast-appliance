"""Repo invariants for the documentation surface.

All of these files are tracked in git, so their absence is a real
invariant violation: the checks assert hard instead of skipping, so a
missing file fails the suite instead of passing silently.
"""
from __future__ import annotations

from pathlib import Path

import pytest

REQUIRED_HEADINGS = ["Arquitectura", "Compilación", "Tests", "Dependencias"]


def _readme(repo_root: Path) -> Path:
    return repo_root / "README.md"


@pytest.mark.parametrize("heading", REQUIRED_HEADINGS)
def test_readme_contains_required_heading(repo_root: Path, heading: str) -> None:
    """README.md must document the required top-level sections."""
    readme = _readme(repo_root)
    assert readme.is_file(), f"missing README.md: {readme}"
    lines = readme.read_text(encoding="utf-8").splitlines()
    heading_lines = [ln for ln in lines if ln.lstrip().startswith("#")]
    assert any(heading in ln for ln in heading_lines), (
        f"required heading {heading!r} missing from README.md"
    )


def test_docs_conf_py_exists(repo_root: Path) -> None:
    """The Sphinx configuration must exist for the docs build."""
    conf = repo_root / "docs" / "conf.py"
    assert conf.is_file(), f"missing Sphinx config: {conf}"
    assert conf.stat().st_size > 0, "docs/conf.py is empty"


def test_readthedocs_yaml_exists(repo_root: Path) -> None:
    """The Read the Docs configuration must exist at the repo root."""
    rtd = repo_root / "readthedocs.yaml"
    assert rtd.is_file(), f"missing Read the Docs config: {rtd}"
    assert rtd.stat().st_size > 0, "readthedocs.yaml is empty"
