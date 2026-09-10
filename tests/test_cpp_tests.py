"""Drive the compiled C++ test binaries from pytest.

The C++ layer is normally exercised via ``ctest``; this module lets the
Python suite discover the built executables under ``build/`` and run a
representative sample as subprocesses. Everything here skips cleanly when
no build tree is present.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

# Representative sample of the C++ test targets under build/src/core/.
SAMPLE_BINARIES = ["test_ring_drift", "test_channel_strip"]

# Generous cap: some C++ suites (resampler, token service) do real work.
SUBPROCESS_TIMEOUT = 120


def discover_test_binaries(build_dir: Path) -> list[Path]:
    """Find compiled ``test_*`` executables under the build tree.

    Third-party sources under ``_deps/`` are excluded so that only the
    project's own CTest targets are considered.
    """
    found = [
        path
        for path in build_dir.rglob("test_*")
        if "_deps" not in path.parts
        and path.is_file()
        and os.access(path, os.X_OK)
    ]
    return sorted(found)


def test_build_dir_fixture_locates_build(build_dir: Path | None) -> None:
    """The build_dir fixture must return a real directory when a build exists."""
    if build_dir is None:
        pytest.skip("no build/ directory; C++ tests not compiled")
    assert build_dir.is_dir()


def test_discover_cpp_test_binaries(build_dir: Path | None) -> None:
    """A build tree must contain at least one compiled test executable."""
    if build_dir is None:
        pytest.skip("no build/ directory; C++ tests not compiled")
    binaries = discover_test_binaries(build_dir)
    assert binaries, f"no compiled test_* executables found under {build_dir}"


@pytest.mark.parametrize("name", SAMPLE_BINARIES)
def test_cpp_binary_runs_clean(build_dir: Path | None, name: str) -> None:
    """A sample of the C++ test binaries must exit 0 when run directly."""
    if build_dir is None:
        pytest.skip("no build/ directory; C++ tests not compiled")
    binary = build_dir / "src" / "core" / name
    if not binary.is_file():
        pytest.skip(f"{name} not built in {build_dir}")
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=SUBPROCESS_TIMEOUT,
    )
    assert result.returncode == 0, (
        f"{name} exited with {result.returncode}\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
