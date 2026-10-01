#!/usr/bin/env python3
"""Asset sets: which pictures a build environment's filesystem image carries.

ADR 0065 -- only what is SHOWN may differ between boards. A file declares which
builds carry it by which set directory it sits in, so the fact lives beside the
file instead of in a list that goes stale. These tests cover the staging rule,
the declarations, the 8 KiB per-photograph cap now that the default set carries
pictures (#316), and PA:INCLUDE resolution across the set and common roots,
which is what lets a set carry a fragment of a page and not only whole files
(#382).
"""

import gzip
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GZIP_FSDATA = ROOT / "tools" / "gzip_fsdata.py"
DATA = ROOT / "data"


def _config():
    from platformio.project.config import ProjectConfig

    return ProjectConfig(str(ROOT / "platformio.ini"))


class AssetSetDeclarations(unittest.TestCase):
    """Every environment that images a filesystem names exactly one set."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.config = _config()
        except ImportError:  # pragma: no cover - depends on the runner
            raise unittest.SkipTest("platformio is not installed; this check needs its config parser")

    def test_every_env_resolves_to_a_known_set(self):
        known = {"legacy", "default"}
        for env in self.config.envs():
            if self.config.get(f"env:{env}", "platform", "") == "native":
                continue
            value = self.config.get(f"env:{env}", "custom_asset_set", "default")
            self.assertIn(value, known, f"{env} names asset set {value!r}, which is not one of {known}")

    def test_artoo_carries_legacy_and_p4_carries_default(self):
        """Every P4 env resolves the `default` set from a declaration in its own
        chain, not from a fallback. A P4 env re-parented onto env:artoo_esp32
        would silently take the 640 KB board's set -- the trap this checks."""
        self.assertEqual(self.config.get("env:artoo_esp32", "custom_asset_set", "default"), "legacy")
        p4_envs = [env for env in self.config.envs() if env.startswith("firebeetle2")]
        self.assertIn("firebeetle2", p4_envs)
        for env in p4_envs:
            self.assertEqual(
                self.config.get(f"env:{env}", "custom_asset_set", None),
                "default",
                f"{env} does not resolve the P4 asset set from its own chain",
            )

    def test_every_artoo_variant_inherits_legacy(self):
        for env in self.config.envs():
            if not env.startswith("artoo_esp32"):
                continue
            self.assertEqual(
                self.config.get(f"env:{env}", "custom_asset_set", "default"),
                "legacy",
                f"{env} does not carry the legacy set",
            )


class AssetSetStaging(unittest.TestCase):
    """The staging rule in tools/gzip_fsdata.py."""

    def test_set_directories_are_not_imaged_as_ordinary_data(self):
        """A set directory is staged by its own pass. Were it also walked as part
        of the common tree, every build would carry every set -- which is the cost
        this whole mechanism exists to avoid."""
        source = GZIP_FSDATA.read_text(encoding="utf-8")
        self.assertIn("ASSET_SETS_DIR", source)
        self.assertIn("rel.startswith(ASSET_SETS_DIR + os.sep)", source)

    def test_a_named_set_that_does_not_exist_is_a_hard_failure(self):
        """A typo must not ship an image quietly missing every picture, on a
        surface whose pictures are what an operator selects by."""
        source = GZIP_FSDATA.read_text(encoding="utf-8")
        self.assertIn("raise SystemExit", source)
        self.assertIn("custom_asset_set is", source)

    def test_both_sets_exist_together(self):
        """An environment naming a missing set fails the build, so both
        directories have to be present once either is. Photographs live in
        default (#316); drawings in legacy are #382's."""
        sets_root = DATA / "asset-sets"
        self.assertTrue(sets_root.is_dir(), "data/asset-sets/ must exist")
        present = sorted(d.name for d in sets_root.iterdir() if d.is_dir())
        self.assertEqual(
            present,
            ["default", "legacy"],
            "both sets must exist together: an environment naming a missing set fails the build",
        )

    def test_default_photographs_fit_two_littlefs_blocks(self):
        """8 KiB is two 4 KiB blocks. 9 KiB would be three (#316, ADR 0065)."""
        default = DATA / "asset-sets" / "default"
        photos = sorted(default.glob("*.webp"))
        self.assertGreaterEqual(len(photos), 1)
        for path in photos:
            size = path.stat().st_size
            self.assertLessEqual(
                size,
                8192,
                f"{path.name} is {size} B, over the 8 KiB block-boundary cap",
            )


class _FakeSConsEnv:
    """Stands in for the ``env`` object SCons injects via ``Import("env")``.

    Only what gzip_fsdata.py actually calls: ``subst()`` for the three
    ``$VAR`` lookups, ``GetProjectOption()`` for ``custom_asset_set``, and
    ``Replace()`` to repoint ``PROJECT_DATA_DIR`` at the staged copy.
    """

    def __init__(self, project_data_dir, build_dir, custom_asset_set="default"):
        self._vars = {
            "$PIOPLATFORM": "espressif32",
            "$PROJECT_DATA_DIR": str(project_data_dir),
            "$BUILD_DIR": str(build_dir),
        }
        self._custom_asset_set = custom_asset_set
        self.replaced = {}

    def subst(self, key):
        return self._vars[key]

    def GetProjectOption(self, name, default=None):
        if name == "custom_asset_set":
            return self._custom_asset_set
        return default

    def Replace(self, **kwargs):
        self.replaced.update(kwargs)


def _run_gzip_fsdata(fake_env):
    """Run gzip_fsdata.py's module body -- including its unconditional
    ``main()`` call at the bottom -- against a fake env, standing in for the
    SCons runner that would otherwise exec it with a real one. ``Import()``
    is SCons's own builtin, injecting a variable into the calling script's
    globals as a side effect rather than returning it; there is no such
    builtin under plain ``python3 -m unittest``, so this supplies one for the
    single name the script asks for.
    """
    source = GZIP_FSDATA.read_text(encoding="utf-8")
    namespace = {"__name__": "gzip_fsdata_under_test", "__file__": str(GZIP_FSDATA)}

    def fake_import(name):
        assert name == "env", "gzip_fsdata.py now Imports something other than 'env'"
        namespace["env"] = fake_env

    namespace["Import"] = fake_import
    exec(compile(source, str(GZIP_FSDATA), "exec"), namespace)
    return namespace


class PartialIncludeResolution(unittest.TestCase):
    """PA:INCLUDE must resolve a partial the same way whole-file staging
    resolves a path: this environment's asset set before the common data
    root (#382). Before this, _expand_includes searched only
    $PROJECT_DATA_DIR, so a partial living inside a set was unreachable no
    matter which set an environment named."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.src = Path(self.tmp.name) / "data"
        self.build = Path(self.tmp.name) / "build"
        self.src.mkdir()
        self.build.mkdir()
        # Every page below must inline the kernel to clear the mandatory
        # guard; it lives in the common root exactly as on a real build.
        (self.src / "_recovery_kernel.html").write_text("KERNEL", encoding="utf-8")

    def _set_dir(self, name):
        set_dir = self.src / "asset-sets" / name
        set_dir.mkdir(parents=True)
        return set_dir

    def _build(self, custom_asset_set):
        _run_gzip_fsdata(_FakeSConsEnv(self.src, self.build, custom_asset_set))

    def _staged_html(self, name):
        with gzip.open(self.build / "fsdata_gz" / (name + ".gz"), "rt", encoding="utf-8") as fh:
            return fh.read()

    def test_partial_present_only_in_the_set_resolves_from_the_set(self):
        set_dir = self._set_dir("myset")
        (set_dir / "_only_in_set.html").write_text("SET-ONLY", encoding="utf-8")
        (self.src / "page.html").write_text(
            "<!-- PA:INCLUDE _recovery_kernel.html -->"
            "<!-- PA:INCLUDE _only_in_set.html -->",
            encoding="utf-8",
        )
        self._build(custom_asset_set="myset")
        self.assertIn("SET-ONLY", self._staged_html("page.html"))

    def test_partial_present_in_both_resolves_to_the_sets_copy(self):
        set_dir = self._set_dir("myset")
        (self.src / "_shared.html").write_text("COMMON-VERSION", encoding="utf-8")
        (set_dir / "_shared.html").write_text("SET-VERSION", encoding="utf-8")
        (self.src / "page.html").write_text(
            "<!-- PA:INCLUDE _recovery_kernel.html -->"
            "<!-- PA:INCLUDE _shared.html -->",
            encoding="utf-8",
        )
        self._build(custom_asset_set="myset")
        staged = self._staged_html("page.html")
        self.assertIn("SET-VERSION", staged)
        self.assertNotIn("COMMON-VERSION", staged)

    def test_partial_present_only_in_common_still_resolves_with_a_set_active(self):
        # The set exists and is active but carries no kernel of its own --
        # this is exactly how _recovery_kernel.html must keep working once
        # a build names a set.
        self._set_dir("myset")
        (self.src / "page.html").write_text(
            "<!-- PA:INCLUDE _recovery_kernel.html -->", encoding="utf-8"
        )
        self._build(custom_asset_set="myset")
        self.assertIn("KERNEL", self._staged_html("page.html"))

    def test_partial_in_neither_root_raises_systemexit_naming_both(self):
        set_dir = self._set_dir("myset")
        (self.src / "page.html").write_text(
            "<!-- PA:INCLUDE _recovery_kernel.html -->"
            "<!-- PA:INCLUDE _does_not_exist.html -->",
            encoding="utf-8",
        )
        with self.assertRaises(SystemExit) as ctx:
            self._build(custom_asset_set="myset")
        message = str(ctx.exception)
        self.assertIn("_does_not_exist.html", message)
        self.assertIn(str(set_dir), message)
        self.assertIn(str(self.src), message)


class _StagingCase(unittest.TestCase):
    """A throwaway data root with the kernel partial in it, staged on demand."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.src = Path(self.tmp.name) / "data"
        self.build = Path(self.tmp.name) / "build"
        self.src.mkdir()
        self.build.mkdir()
        (self.src / "_recovery_kernel.html").write_text("KERNEL", encoding="utf-8")

    def _build(self):
        _run_gzip_fsdata(_FakeSConsEnv(self.src, self.build))

    def _staged(self, name):
        with gzip.open(self.build / "fsdata_gz" / (name + ".gz"), "rt", encoding="utf-8") as fh:
            return fh.read()


class ShellDelegateKernel(_StagingCase):
    """A shell delegate carries no recovery kernel, and the build refuses one
    that does (#382, ADR 0048 amendment)."""

    DELEGATE = '<script>window.PAShellDelegate = true; location.replace("/#x");</script>'

    def test_a_delegate_carrying_the_kernel_is_refused(self):
        (self.src / "page.html").write_text(
            self.DELEGATE + "<!-- PA:INCLUDE _recovery_kernel.html -->", encoding="utf-8"
        )
        with self.assertRaises(SystemExit) as ctx:
            self._build()
        self.assertIn("shell delegate", str(ctx.exception))

    def test_a_delegate_without_the_kernel_is_staged_without_it(self):
        (self.src / "page.html").write_text(self.DELEGATE + "<body>surface</body>", encoding="utf-8")
        self._build()
        staged = self._staged("page.html")
        self.assertIn("surface", staged)
        self.assertNotIn("KERNEL", staged)

    def test_a_rendered_page_without_the_kernel_is_still_refused(self):
        (self.src / "index.html").write_text("<body>shell</body>", encoding="utf-8")
        with self.assertRaises(SystemExit) as ctx:
            self._build()
        self.assertIn("does not include '_recovery_kernel.html'", str(ctx.exception))


class Minification(_StagingCase):
    """JS and CSS are minified by esbuild without a string changing (#382)."""

    def test_js_is_minified_and_a_nested_template_keeps_its_whitespace(self):
        # The exact shape rjsmin broke in data/parts.js: whitespace inside a
        # template literal nested in another one's ${...}.
        nested = 'const row = (cls) => `<tr class="parts-row${cls ? ` ${cls}` : ""}">`;'
        (self.src / "a.js").write_text(
            "// a comment that must not be imaged\n"
            "/* and a block comment */\n" + nested + "\n",
            encoding="utf-8",
        )
        self._build()
        staged = self._staged("a.js")
        self.assertNotIn("comment", staged)
        self.assertIn("` ${cls}`", staged)
        self.assertIn('<tr class="parts-row${', staged)

    def test_css_is_minified(self):
        (self.src / "s.css").write_text(
            "/* a comment that must not be imaged */\n.a  {\n  color :  red ;\n}\n",
            encoding="utf-8",
        )
        self._build()
        staged = self._staged("s.css")
        self.assertNotIn("comment", staged)
        self.assertIn(".a{color:red}", staged)

    def test_a_missing_esbuild_fails_the_build(self):
        import os
        from unittest import mock

        (self.src / "a.js").write_text("var a = 1;\n", encoding="utf-8")
        with mock.patch.dict(os.environ, {"PATH": self.tmp.name}):
            with self.assertRaises(SystemExit) as ctx:
                self._build()
        self.assertIn("esbuild is not on PATH", str(ctx.exception))



class _BoardEnv(_FakeSConsEnv):
    """A fake env that also answers what a #board include asks: the env's
    build_flags and the project directory the Component Registry lives in."""

    def __init__(self, project_data_dir, build_dir, project_dir, flags, custom_asset_set="myset"):
        super().__init__(project_data_dir, build_dir, custom_asset_set)
        self._vars["$PROJECT_DIR"] = str(project_dir)
        self._flags = flags

    def GetProjectOption(self, name, default=None):
        if name == "build_flags":
            return self._flags
        return super().GetProjectOption(name, default)


class BoardDrawingInclude(unittest.TestCase):
    """`PA:INCLUDE _product_art.html#board` inlines only the running board's
    drawing (#411). Wiring pictures one board, and the whole sprite cost the
    4 MB board 11.5 KB of gzipped image to show it. Which board is the
    Component Registry's own (PA_BOARD == ...) gate, never a second map."""

    REGISTRY = (
        'PA_COMPONENT_PART( 1, "alpha_pcb", "Alpha", COMPONENT_CATEGORY_BODY_CONTROLLER, "none", '
        "COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_ALPHA))\n"
        'PA_COMPONENT_PART( 2, "beta_pcb", "Beta", COMPONENT_CATEGORY_BODY_CONTROLLER, "none", '
        "COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_BETA))\n"
        'PA_COMPONENT_PART( 3, "gamma_esc", "Gamma", COMPONENT_CATEGORY_DOME_ESC, "pwm", '
        "COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_BETA))\n"
    )
    SPRITE = (
        '<svg class="product-art-sprite" aria-hidden="true">'
        '<symbol id="art-alpha_pcb" viewBox="0 0 400 300"><path d="ALPHA"/></symbol>'
        '<symbol id="art-beta_pcb" viewBox="0 0 400 300"><path d="BETA"/></symbol>'
        '<symbol id="art-gamma_esc" viewBox="0 0 400 300"><path d="GAMMA"/></symbol>'
        "</svg>"
    )
    DELEGATE = '<script>window.PAShellDelegate = true; location.replace("/#w");</script>'

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.project = root
        self.src = root / "data"
        self.build = root / "build"
        (self.src / "asset-sets" / "myset").mkdir(parents=True)
        self.build.mkdir()
        (root / "include").mkdir()
        (root / "include" / "component_registry.inc").write_text(self.REGISTRY, encoding="utf-8")
        (self.src / "_recovery_kernel.html").write_text("KERNEL", encoding="utf-8")

    def _stage(self, sprite, flags, include="_art.html#board"):
        (self.src / "asset-sets" / "myset" / "_art.html").write_text(sprite, encoding="utf-8")
        (self.src / "page.html").write_text(
            self.DELEGATE + "<body><!-- PA:INCLUDE %s --></body>" % include, encoding="utf-8"
        )
        _run_gzip_fsdata(_BoardEnv(self.src, self.build, self.project, flags))
        with gzip.open(self.build / "fsdata_gz" / "page.html.gz", "rt", encoding="utf-8") as fh:
            return fh.read()

    def test_only_the_running_boards_drawing_is_inlined_in_the_sprites_own_wrapper(self):
        staged = self._stage(self.SPRITE, ["-DPA_LOG_LEVEL=2", "-DPA_BOARD=PA_BOARD_BETA"])
        self.assertIn('<symbol id="art-beta_pcb"', staged)
        self.assertNotIn("art-alpha_pcb", staged, "a peer board's drawing is not this board's")
        self.assertNotIn("art-gamma_esc", staged, "another family gated on the same board is not the board")
        self.assertIn('<svg class="product-art-sprite" aria-hidden="true">', staged)

    def test_a_sprite_with_no_drawing_for_the_board_inlines_nothing(self):
        staged = self._stage("<!-- photographs, no drawings -->", ["-DPA_BOARD=PA_BOARD_ALPHA"])
        self.assertEqual(staged, self.DELEGATE + "<body></body>")

    def test_an_env_with_no_board_or_an_unknown_fragment_fails_the_build(self):
        with self.assertRaises(SystemExit) as ctx:
            self._stage(self.SPRITE, ["-DPA_LOG_LEVEL=2"])
        self.assertIn("-DPA_BOARD=", str(ctx.exception))
        with self.assertRaises(SystemExit) as ctx:
            self._stage(self.SPRITE, ["-DPA_BOARD=PA_BOARD_BETA"], include="_art.html#dome")
        self.assertIn("'#dome' is not a fragment", str(ctx.exception))


class MarkupComments(_StagingCase):
    """A staged page carries no markup comment, and nothing that only looks
    like one is touched (#461). A whole-file non-greedy comment regex passes
    the comment case and fails every other one here; the scanner exists for
    those."""

    PAGE = (
        "<!DOCTYPE html>\n"
        "<html><head>\n"
        "  <!-- PA:INCLUDE _recovery_kernel.html -->\n"
        "  <title>Droid <!-- not a comment in a title --></title>\n"
        "  <!-- a note for whoever edits this page -->\n"
        "  <script>\n"
        '    var s = "<!-- keep -->";\n'
        "    var t = `<!-- keep ${s} -->`;\n"
        "  </script>\n"
        "</head><body>\n"
        '  <div data-note="<!-- keep -->" title=\'a <!-- b\'>x</div>\n'
        "  <textarea><!-- typed by the operator --></textarea>\n"
        "  <button>One</button>\n"
        "  <!-- between two buttons -->\n"
        "  <button>Two</button>\n"
        "  <pre>\n  keep\n\n</pre>\n"
        "</body></html>\n"
    )

    def test_comments_go_and_what_only_looks_like_one_stays(self):
        (self.src / "page.html").write_text(self.PAGE, encoding="utf-8")
        self._build()
        staged = self._staged("page.html")
        self.assertIn("KERNEL", staged, "the include is expanded, not stripped")
        self.assertNotIn("a note for whoever", staged)
        self.assertNotIn("between two buttons", staged)
        self.assertIn("<title>Droid <!-- not a comment in a title --></title>", staged)
        self.assertIn('var s = "<!-- keep -->";\n    var t = `<!-- keep ${s} -->`;', staged)
        self.assertIn('<div data-note="<!-- keep -->" title=\'a <!-- b\'>x</div>', staged)
        self.assertIn("<textarea><!-- typed by the operator --></textarea>", staged)
        # One newline, not none: the gap between two inline buttons is a node.
        self.assertIn("<button>One</button>\n<button>Two</button>", staged)
        self.assertIn("<pre>\n  keep\n\n</pre>", staged)
        self.assertEqual(
            (self.src / "page.html").read_text(encoding="utf-8"), self.PAGE, "data/ keeps its comments"
        )

    def test_markup_it_does_not_read_fails_and_leaves_the_last_stage(self):
        (self.src / "page.html").write_text(
            "<!-- PA:INCLUDE _recovery_kernel.html --><p>ok</p>", encoding="utf-8"
        )
        self._build()
        refused = {
            "an unterminated comment": "<p>a</p><!-- never closed",
            "closed by '--!>'": "<!-- a --!> b -->",
            "inside script data": "<script>var a = '<!--'; var b = '<script>';</script>",
            "a self-closed <script/>": '<script src="a.js"/>',
            "not a start tag": '<p class="a>b</p>',
        }
        for why, body in refused.items():
            with self.subTest(why):
                (self.src / "page.html").write_text(
                    "<!-- PA:INCLUDE _recovery_kernel.html -->" + body, encoding="utf-8"
                )
                with self.assertRaises(SystemExit) as ctx:
                    self._build()
                self.assertIn(why, str(ctx.exception))
                self.assertIn("<p>ok</p>", self._staged("page.html"), "the last good stage is kept")


class ZopfliStaging(_StagingCase):
    """Every gzipped asset is written by zopfli (#461)."""

    def test_a_missing_zopfli_fails_the_build(self):
        import os
        from unittest import mock

        # A .txt asset is gzipped but never minified, so esbuild is not asked.
        (self.src / "a.txt").write_text("text\n", encoding="utf-8")
        with mock.patch.dict(os.environ, {"PATH": self.tmp.name}):
            with self.assertRaises(SystemExit) as ctx:
                self._build()
        self.assertIn("zopfli is not on PATH", str(ctx.exception))

    def test_the_stage_is_written_largest_file_first(self):
        """Creation order is image order, so it is part of the block count and
        must be a function of the commit (#429); largest first packs best."""
        (self.src / "b.webp").write_bytes(b"\0" * 10)
        (self.src / "a.webp").write_bytes(b"\0" * 10)
        (self.src / "c.webp").write_bytes(b"\0" * 300)
        (self.src / "d.txt").write_text("x" * 5000, encoding="utf-8")
        self._build()
        stage = self.build / "fsdata_gz"
        created = [e.name for e in sorted(stage.iterdir(), key=lambda e: e.stat().st_ino)]
        self.assertEqual(created, ["c.webp", "d.txt.gz", "a.webp", "b.webp"])


class RealPagesStage(unittest.TestCase):
    """The scanner reads every real page in both asset sets, and what it must
    leave alone it leaves alone (#461)."""

    BUILDS = {"artoo_esp32": "legacy", "firebeetle2": "default"}

    @classmethod
    def setUpClass(cls):
        try:
            cls.config = _config()
        except ImportError:  # pragma: no cover - depends on the runner
            raise unittest.SkipTest("platformio is not installed; this check needs its config parser")

    def test_both_sets_stage_and_the_kernel_is_as_written(self):
        import re

        kernel = (DATA / "_recovery_kernel.html").read_text(encoding="utf-8")
        kernel_blocks = re.findall(r"<style>[\s\S]*?</style>|<script>[\s\S]*?</script>", kernel)
        self.assertEqual(len(kernel_blocks), 2)
        sources = {p.name: p.read_bytes() for p in DATA.glob("*.html")}
        for env, asset_set in self.BUILDS.items():
            with self.subTest(env), tempfile.TemporaryDirectory() as tmp:
                build = Path(tmp)
                flags = self.config.get("env:%s" % env, "build_flags")
                self.assertEqual(self.config.get("env:%s" % env, "custom_asset_set", "default"), asset_set)
                _run_gzip_fsdata(_BoardEnv(DATA, build, ROOT, flags, custom_asset_set=asset_set))
                stage = build / "fsdata_gz"
                pages = sorted(stage.glob("*.html.gz"))
                self.assertGreater(len(pages), 10)
                for page in pages:
                    with gzip.open(page, "rt", encoding="utf-8") as fh:
                        html = fh.read()
                    self.assertNotIn("PA:INCLUDE", html, page.name)
                    if page.name == "index.html.gz":
                        for block in kernel_blocks:
                            self.assertIn(block, html, "the kernel's style and script are staged as written")
                self.assertEqual(
                    (stage / "console_help.txt").read_bytes(),
                    (DATA / "console_help.txt").read_bytes(),
                    "console_help.txt is read at an offset, so it is staged raw",
                )
        self.assertEqual({p.name: p.read_bytes() for p in DATA.glob("*.html")}, sources)


if __name__ == "__main__":
    unittest.main()
