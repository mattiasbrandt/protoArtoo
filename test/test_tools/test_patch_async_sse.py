import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[2] / "tools" / "patch_async_sse.py"
SPEC = importlib.util.spec_from_file_location("patch_async_sse", MODULE_PATH)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class PatchAsyncWebServerTest(unittest.TestCase):
    def test_sse_patch_collapses_nested_guards_and_is_idempotent(self):
        source = (
            "header\n"
            + PATCH.SSE_PREAMBLE_START
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#define SSE_MAX_INFLIGH 16 * 1024"
            + "  // but no more than 16k, no need to blow it, since same data is kept in local Q\n"
            + "#endif\n"
            + "#endif\n"
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#define SSE_MAX_INFLIGH 8 * 1024"
            + "  // but no more than 8k, no need to blow it, since same data is kept in local Q\n"
            + "#endif\n"
            + "#define SSE_MAX_INFLIGH 16 * 1024"
            + "  // but no more than 16k, no need to blow it, since same data is kept in local Q\n"
            + PATCH.SSE_PREAMBLE_END
            + "body\n"
        )

        patched = PATCH.patch_sse_header(source)

        self.assertEqual(patched.count("#ifndef SSE_MAX_INFLIGH"), 3)
        self.assertEqual(PATCH.patch_sse_header(patched), patched)

    def test_static_handler_patch_removes_exists_and_is_idempotent(self):
        source = "prefix\n" + PATCH.STATIC_SEARCH_BEFORE + "suffix\n"

        patched = PATCH.patch_static_handler(source)

        self.assertNotIn("_fs.exists", patched)
        self.assertEqual(PATCH.patch_static_handler(patched), patched)


if __name__ == "__main__":
    unittest.main()
