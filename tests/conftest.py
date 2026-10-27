"""Shared fixtures for the broadcast-appliance repo-invariant suite.

This suite verifies repository-level invariants (packaging files, the
documentation, dependency pins) and drives the compiled C++
test binaries when a CMake build tree is present.
"""
from __future__ import annotations

import os
from pathlib import Path

import pytest

# The C++ test binaries expect the source-language texts (Spanish): neither the system
# language nor a saved launcher.json may change them (ba::i18n::idiomaDeLaMaquina).
os.environ["BA_IDIOMA"] = "es"

# Repository root: this file lives in <root>/tests/conftest.py
REPO_ROOT = Path(__file__).resolve().parent.parent


@pytest.fixture(scope="session")
def repo_root() -> Path:
    """Absolute path to the repository root (parent of ``tests/``)."""
    return REPO_ROOT


@pytest.fixture(scope="session")
def build_dir(repo_root: Path) -> Path | None:
    """Locate the CMake build directory, or ``None`` if the project is unbuilt.

    The C++ test executables are produced by CMake/CTest under
    ``build/src/core/``; the whole C++ layer is skipped cleanly when no
    build tree exists.
    """
    candidate = repo_root / "build"
    return candidate if candidate.is_dir() else None
