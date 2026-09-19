"""Invariants of the pinned AI agents (src/app/agent/agentes.json).

The Assistant tab downloads each agent from these pins and verifies it by
SHA-256. Updating a version by hand is easy to get half-right (a platform
without a hash, a Windows entry pointing at a Linux binary...), so the shape
is checked here. The actual downloads are verified at install time.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

AGENTS = ["claude", "opencode", "gemini"]
PLATFORMS = ["linux-x64", "win-x64"]


def _pins(repo_root: Path) -> dict:
    return json.loads((repo_root / "src/app/agent/agentes.json").read_text(encoding="utf-8"))


@pytest.mark.parametrize("agent", AGENTS)
@pytest.mark.parametrize("platform", PLATFORMS)
def test_agent_has_pinned_platform(repo_root: Path, agent: str, platform: str) -> None:
    pin = _pins(repo_root)[agent]
    entry = pin["plataformas"].get(platform)
    assert entry, f"{agent}: falta la plataforma {platform}"
    assert entry["url"].startswith("https://"), f"{agent}/{platform}: url no https"
    assert re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]), f"{agent}/{platform}: sha256 invalido"
    # Gemini: lo de cada plataforma es Node (su version); el agente es `paquete`.
    versioned = pin["paquete"]["url"] if agent == "gemini" else entry["url"]
    assert pin["version"] in versioned, f"{agent}/{platform}: la url no es de la version {pin['version']}"
    exe = entry["exe"]
    if platform.startswith("win"):
        assert exe.endswith(".exe"), f"{agent}/{platform}: el ejecutable de Windows debe ser .exe ({exe})"
        assert re.search(r"win", entry["url"]), f"{agent}/{platform}: la url no parece de Windows"
    else:
        assert not exe.endswith(".exe"), f"{agent}/{platform}: ejecutable .exe fuera de Windows"
        assert "linux" in entry["url"], f"{agent}/{platform}: la url no parece de Linux"


@pytest.mark.parametrize("platform", ["linux-x64", "win-x64"])
def test_opencode_baseline_variant(repo_root: Path, platform: str) -> None:
    """Sin AVX2 se usa la variante baseline de opencode: tiene que existir."""
    plats = _pins(repo_root)["opencode"]["plataformas"]
    base = plats.get(platform + "-baseline")
    assert base and "baseline" in base["url"], f"falta opencode {platform}-baseline"
    assert base["exe"] == plats[platform]["exe"]
    assert base["sha256"] != plats[platform]["sha256"]
