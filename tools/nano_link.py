# PlatformIO post: extra script -- link newlib nano where the env's sdkconfig asks for it.
#
# CONFIG_LIBC_NEWLIB_NANO_FORMAT=y ([artoo_envelope] in platformio.ini) builds the
# ESP-IDF libs against newlib nano's printf/scanf, and ESP-IDF itself adds the
# matching `--specs=nano.specs` to its own link
# (framework-espidf/components/newlib/CMakeLists.txt, under
# `if(CONFIG_LIBC_NEWLIB_NANO_FORMAT)`). pioarduino's final Arduino link does not
# carry that flag, and the two failure modes, both measured on #430, are why this
# script is shaped the way it is:
#
#   1. Without the flag the final link fails: `undefined reference to
#      _printf_float` from libnewlib.a(newlib_init.c.o)'s s_stub_table, which
#      was compiled for nano and names nano's float hook.
#   2. Appending the flag unconditionally fails the framework-library rebuild,
#      whose link already carries it from ESP-IDF: `attempt to rename spec
#      'link' to already defined spec 'nano_link'`.
#
# So the flag is appended only when it is not already there. It is also appended
# only when THIS env's custom_sdkconfig turns nano on: every firebeetle2 env
# inherits [env:artoo_esp32]'s extra_scripts through `extends`, and linking the
# ESP32-P4 image against nano while its libs were built for full newlib would
# be a silent change to the other board.
Import("env")  # noqa: F821 - provided by SCons

NANO_KCONFIG = "CONFIG_LIBC_NEWLIB_NANO_FORMAT=y"


def _sdkconfig_wants_nano() -> bool:
    lines = env.GetProjectOption("custom_sdkconfig", "").splitlines()  # noqa: F821
    return any(line.split(";", 1)[0].strip() == NANO_KCONFIG for line in lines)


if _sdkconfig_wants_nano():
    flags = " ".join(str(f) for f in env.get("LINKFLAGS", []))  # noqa: F821
    if "nano.specs" not in flags:
        env.Append(LINKFLAGS=["--specs=nano.specs"])  # noqa: F821
