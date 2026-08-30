"""Every firmware environment declares PA_LOG_LEVEL and PA_HEAP_PROFILE exactly once.

Guards the defect in #244: [flags_base] used to supply defaults for both, and an env
that wanted a different value had to `build_unflags` the base first. That does not work
-- PlatformIO's ConfigureProject() runs ProcessFlags(BUILD_FLAGS) -> BuildFrameworks()
-> ProcessUnFlags(BUILD_UNFLAGS), and the framework's HybridCompile dummy-lib rebuild
inside BuildFrameworks() reads raw build_flags before any unflag has been applied. Both
values reached one -Werror compile, so NO *_profiler env built on any target.

Two properties are asserted, and they fail in opposite directions:

  * declared more than once -> the redefinition that broke the profiler envs,
  * declared nowhere        -> include/config.h #errors, but only for a target that is
                               actually compiled; this catches it in the host suite
                               instead of on whoever next builds that env.

Deliberately parses platformio.ini as text rather than through PlatformIO's own config
reader: the point is what the file declares, not what PlatformIO resolves after
inheritance, and reading it the second way would make the test agree with a broken file.
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PLATFORMIO_INI = REPO_ROOT / "platformio.ini"

REQUIRED_SYMBOLS = ("PA_LOG_LEVEL", "PA_HEAP_PROFILE")

# Envs that do not compile project sources and therefore never reach include/config.h.
# Kept explicit: a new env is covered by default, which is the point.
NON_COMPILING_ENVS = frozenset()


def _sections(text):
    """Yield (env_name, body) for each [env:*] section, comment lines removed.

    Comments are stripped first because platformio.ini documents this very defect in
    prose that contains the flag spellings -- matching those would make the guard pass
    on a file that only *talks* about declaring them.
    """
    stripped = "\n".join(
        line for line in text.splitlines() if not line.strip().startswith(";")
    )
    for part in re.split(r"(?m)^(?=\[)", stripped):
        if part.startswith("[env:"):
            yield part.split("]")[0].lstrip("["), part


def _inherits_from(body):
    """The env this one extends, as a full section key (``env:name``)."""
    match = re.search(r"^extends\s*=\s*(env:\S+)", body, re.M)
    return match.group(1) if match else None


def _interpolated_envs(body):
    """Env sections pulled in by ``${env:name.build_flags}`` interpolation.

    Redeclaring build_flags replaces the inherited list, so several envs re-import
    their parent's explicitly. Those still resolve the symbol; treating them as
    declaring nothing would be a false alarm.
    """
    return ["env:" + n for n in re.findall(r"\$\{env:([^.}]+)\.build_flags\}", body)]


def _declares_own_build_flags(body):
    return re.search(r"^build_flags\s*=", body, re.M) is not None


class EnvFlagDeclarations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PLATFORMIO_INI.read_text(encoding="utf-8")
        cls.envs = dict(_sections(cls.text))
        assert cls.envs, "parsed no [env:*] sections from platformio.ini"

    def _effective(self, env, symbol, _seen=None):
        """Values a symbol resolves to, following `extends` when a child declares no
        build_flags of its own (PlatformIO replaces rather than appends)."""
        _seen = _seen or set()
        if env in _seen or env not in self.envs:
            return []
        _seen.add(env)
        body = self.envs[env]
        if _declares_own_build_flags(body):
            found = re.findall(r"-D\s?%s=(\S+)" % symbol, body)
            for imported in _interpolated_envs(body):
                found += self._effective(imported, symbol, _seen)
            return found
        parent = _inherits_from(body)
        return self._effective(parent, symbol, _seen) if parent else []

    def test_no_symbol_is_declared_twice_in_one_env(self):
        for name, body in self.envs.items():
            for symbol in REQUIRED_SYMBOLS:
                found = re.findall(r"-D\s?%s=(\S+)" % symbol, body)
                self.assertLessEqual(
                    len(found), 1,
                    f"[{name}] declares {symbol} {len(found)} times ({found}). "
                    "Two definitions of one macro is what broke every *_profiler env "
                    "(#244); declare it once per env and never build_unflags it.",
                )

    def test_no_env_relies_on_build_unflags_for_these_symbols(self):
        for name, body in self.envs.items():
            unflags = re.search(r"^build_unflags\s*=\s*((?:\n\t.*)+)", body, re.M)
            if not unflags:
                continue
            for symbol in REQUIRED_SYMBOLS:
                self.assertNotIn(
                    symbol, unflags.group(1),
                    f"[{name}] build_unflags {symbol}. That is the pattern #244 removed: "
                    "the framework's dummy-lib compile reads build_flags before unflags "
                    "are applied, so both values reach one -Werror compile.",
                )

    def test_every_env_resolves_both_symbols(self):
        for name in self.envs:
            if name in NON_COMPILING_ENVS:
                continue
            for symbol in REQUIRED_SYMBOLS:
                values = self._effective(name, symbol)
                self.assertEqual(
                    len(values), 1,
                    f"[{name}] resolves {symbol} to {values}; expected exactly one value. "
                    "include/config.h #errors when it is undefined, so this would fail "
                    "the build for whoever compiles that env next.",
                )

    def test_flags_base_supplies_no_default_for_these_symbols(self):
        match = re.search(r"(?m)^\[flags_base\]\n(.*?)(?=^\[)", self.text, re.S)
        self.assertIsNotNone(match, "no [flags_base] section found")
        body = "\n".join(
            line for line in match.group(1).splitlines()
            if not line.strip().startswith(";")
        )
        for symbol in REQUIRED_SYMBOLS:
            self.assertNotIn(
                f"-D{symbol}=", body,
                f"[flags_base] defines {symbol}. It must stay per-env, for the same "
                "reason PA_AUDIO_DRIVER does: a base default forces any env wanting a "
                "different value to define the macro twice (#244).",
            )


if __name__ == "__main__":
    unittest.main()
