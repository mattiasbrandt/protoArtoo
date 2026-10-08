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
#
# The nano link also needs the ROM's memset/memcpy/memmove/memcmp, measured on
# the #355 bench (2026-09-28): the #430 image panicked on every boot with
# "Cache disabled but cached memory region accessed", in esp_flash_read_chip_id
# -> memset. sections.ld places those four in IRAM only from an archive named
# *libc.a; with nano they come from libc_nano.a, match nothing, and land in
# flash, which the flash driver calls with the cache off. pioarduino's
# flags/ld_scripts omits esp32.rom.libc-funcs.ld, which ESP-IDF itself links
# on an ESP32 without CONFIG_SPIRAM_CACHE_WORKAROUND
# (framework-espidf/components/esp_rom/CMakeLists.txt, "Regular app build"),
# and this envelope has no SPIRAM. The script binds the ROM's pure string and
# memory functions only, no printf, so it does not undo nano. Appended once,
# for the same reason as the specs flag.
Import("env")  # noqa: F821 - provided by SCons

NANO_KCONFIG = "CONFIG_LIBC_NEWLIB_NANO_FORMAT=y"
ROM_LIBC_SCRIPT = "esp32.rom.libc-funcs.ld"


def _sdkconfig_wants_nano() -> bool:
    lines = env.GetProjectOption("custom_sdkconfig", "").splitlines()  # noqa: F821
    return any(line.split(";", 1)[0].strip() == NANO_KCONFIG for line in lines)


if _sdkconfig_wants_nano():
    flags = " ".join(str(f) for f in env.get("LINKFLAGS", []))  # noqa: F821
    if "nano.specs" not in flags:
        env.Append(LINKFLAGS=["--specs=nano.specs"])  # noqa: F821
    if ROM_LIBC_SCRIPT not in flags:
        env.Append(LINKFLAGS=["-T", ROM_LIBC_SCRIPT])  # noqa: F821
