"""Drive the compiled C++ test binaries from pytest.

The C++ layer is normally exercised via ``ctest``; this module discovers
*every* built ``test_*`` executable under ``build/`` (excluding third-party
``_deps/``) and runs each one as a subprocess, asserting a clean exit.
Everything here skips cleanly when no build tree is present.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

# Repository root: this file lives in <root>/tests/test_cpp_tests.py
REPO_ROOT = Path(__file__).resolve().parent.parent

# Generous cap: some C++ suites (resampler, token service) do real work.
SUBPROCESS_TIMEOUT = 120


def _is_native_executable(path: Path) -> bool:
    """Return ``True`` if ``path`` can actually be executed on this host.

    The CMake tree also carries a Windows cross-compile output (``build/win/``)
    whose ``.exe`` targets keep the executable bit but are Windows PE binaries;
    running them on a non-Windows hosts fails with "exec format error".

    Filtering by native executability (not by a hardcoded subdir name) keeps
    the discovery portable: on a non-Windows host any ``.exe`` is skipped, on
    a Windows host it is kept.

    :param path: candidate ``test_*`` file to evaluate.
    :type path: Path
    :returns: ``True`` if the file is a native executable for the current host.
    :rtype: bool
    """
    if os.name != "nt" and path.suffix.lower() == ".exe":
        return False
    return True


def discover_test_binaries(build_dir: Path) -> list[Path]:
    """Find compiled ``test_*`` executables under the build tree.

    Recursively walks the build tree and keeps only the project's own native
    CTest targets: third-party sources under ``_deps/`` and non-native
    executables (e.g. the Windows ``.exe`` cross-build on a Linux host) are
    excluded.

    :param build_dir: root of the CMake build tree.
    :type build_dir: Path
    :returns: sorted list of native executable ``test_*`` files; may be empty
        if the build tree exists but no target has been compiled yet.
    :rtype: list[Path]
    """
    found = [
        path
        for path in build_dir.rglob("test_*")
        if "_deps" not in path.parts
        and path.is_file()
        and os.access(path, os.X_OK)
        and _is_native_executable(path)
    ]
    return sorted(found)


def _discovered_binaries() -> list[Path]:
    """Discover C++ test binaries at collection time, or ``[]`` if unbuilt.

    This runs during pytest collection (before fixtures are resolved), so it
    locates the build tree directly from the repository root instead of the
    session-scoped ``build_dir`` fixture.
    """
    build_dir = REPO_ROOT / "build"
    if not build_dir.is_dir():
        return []
    return discover_test_binaries(build_dir)


def pytest_generate_tests(metafunc) -> None:
    """Parametrize the per-binary test over every discovered C++ executable.

    An empty discovery (no build tree, or a build in progress that has not
    produced a target yet) yields a single no-op parametrization that the
    test body turns into a clean skip rather than a collection error.
    """
    if metafunc.function is not test_cpp_binary_runs_clean:
        return
    binaries = _discovered_binaries()
    if binaries:
        metafunc.parametrize("binary", binaries, ids=[b.name for b in binaries])
    else:
        metafunc.parametrize("binary", [None], ids=["(none-discovered)"])


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
    if not binaries:
        pytest.skip(f"build/ exists but no test_* executables compiled yet under {build_dir}")
    assert binaries, f"no compiled test_* executables found under {build_dir}"


def test_cpp_binary_runs_clean(build_dir: Path | None, binary: Path | None) -> None:
    """Every compiled C++ test binary must exit 0 when run directly."""
    if build_dir is None:
        pytest.skip("no build/ directory; C++ tests not compiled")
    if binary is None or not binary.is_file():
        pytest.skip("binary not built yet (build in progress); nothing to run")
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=SUBPROCESS_TIMEOUT,
    )
    assert result.returncode == 0, (
        f"{binary.name} exited with {result.returncode}\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
