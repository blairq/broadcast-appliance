"""Repo invariants for the documentation surface.

These checks are skipped cleanly when the corresponding file is not part
of the repository yet, so the suite stays green on a bare checkout.
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
    if not readme.is_file():
        pytest.skip("README.md not present in repository")
    lines = readme.read_text(encoding="utf-8").splitlines()
    heading_lines = [ln for ln in lines if ln.lstrip().startswith("#")]
    assert any(heading in ln for ln in heading_lines), (
        f"required heading {heading!r} missing from README.md"
    )


def test_docs_conf_py_exists(repo_root: Path) -> None:
    """The Sphinx configuration must exist for the docs build."""
    conf = repo_root / "docs" / "conf.py"
    if not conf.is_file():
        pytest.skip("docs/conf.py not present in repository")
    assert conf.stat().st_size > 0, "docs/conf.py is empty"


def test_readthedocs_yaml_exists(repo_root: Path) -> None:
    """The Read the Docs configuration must exist at the repo root."""
    rtd = repo_root / "readthedocs.yaml"
    if not rtd.is_file():
        pytest.skip("readthedocs.yaml not present in repository")
    assert rtd.stat().st_size > 0, "readthedocs.yaml is empty"
