from pathlib import Path

Import("env")


def patch_async_event_source_header(*_args, **_kwargs):
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    header = libdeps_dir / env["PIOENV"] / "ESPAsyncWebServer" / "src" / "AsyncEventSource.h"
    if not header.exists():
        return

    text = header.read_text(encoding="utf-8")
    replacements = {
        "#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q":
            "#ifndef SSE_MAX_INFLIGH\n"
            "#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q\n"
            "#endif",
        "#define SSE_MAX_INFLIGH 8 * 1024  // but no more than 8k, no need to blow it, since same data is kept in local Q":
            "#ifndef SSE_MAX_INFLIGH\n"
            "#define SSE_MAX_INFLIGH 8 * 1024  // but no more than 8k, no need to blow it, since same data is kept in local Q\n"
            "#endif",
    }
    patched = text
    for before, after in replacements.items():
        patched = patched.replace(before, after)

    if patched != text:
        header.write_text(patched, encoding="utf-8")
        print("[patch_async_sse.py] ESPAsyncWebServer SSE_MAX_INFLIGH made build-flag overridable")


patch_async_event_source_header()
