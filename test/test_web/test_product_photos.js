// =============================================================================
// test/test_web/test_product_photos.js
//
// The default asset set's Component Picker photographs (#316, ADR 0065).
//
// These are the files a picker card fetches at /<registry-id>.webp. A missing
// file is a cosmetic gap, never "your board cannot do this"; the files that
// do ship must be WebP, 400x300, and at most two LittleFS blocks (8 KiB).
// Deleting or replacing one with a JPEG turns this suite red.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, readdirSync, existsSync } from "fs";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const DEFAULT_SET = join(ROOT, "data/asset-sets/default");
const CAP_BYTES = 8192;

// Every Component Registry product that is a physical product. esp32_gpio_ledc
// is the MCU's own PWM and has no photograph.
const PRODUCT_IDS = [
  "artoo_pcb",
  "firebeetle2",
  "hotrc_ds650",
  "rc_transmitter_pwm",
  "rc_transmitter_sbus",
  "rc_transmitter_elrs",
  "xbox_controller",
  "pca9685",
  "pololu_maestro",
  "isdt_esc70",
  "syren10",
  "astropixels_plus",
  "teeces",
  "hoverboard",
  "sabertooth_2x25",
  "flipsky_mini_v6_vesc",
  "dy_sv5w",
  "mp3_trigger",
  "chirp",
  "dfplayer_mini",
];

function readVp8Size(buf) {
  // PIL's lossy WebP writer emits a VP8 chunk (not VP8X). RFC 6386 §9.1:
  // 3-byte frame tag, then 0x9d 0x01 0x2a, then 14-bit little-endian
  // width and height.
  if (buf.length < 30) {
    return null;
  }
  if (buf.toString("ascii", 0, 4) !== "RIFF" || buf.toString("ascii", 8, 12) !== "WEBP") {
    return null;
  }
  if (buf.toString("ascii", 12, 16) !== "VP8 ") {
    return null;
  }
  if ((buf[20] & 1) !== 0) {
    return null;
  }
  if (buf[23] !== 0x9d || buf[24] !== 0x01 || buf[25] !== 0x2a) {
    return null;
  }
  return {
    width: buf.readUInt16LE(26) & 0x3fff,
    height: buf.readUInt16LE(28) & 0x3fff,
  };
}

test("the default set carries a WebP photograph for every physical product", () => {
  assert.equal(existsSync(DEFAULT_SET), true, "data/asset-sets/default must exist");
  for (const id of PRODUCT_IDS) {
    const path = join(DEFAULT_SET, `${id}.webp`);
    assert.equal(existsSync(path), true, `missing ${path}`);
    const buf = readFileSync(path);
    assert.equal(buf.toString("ascii", 0, 4), "RIFF", `${id} is not RIFF`);
    assert.equal(buf.toString("ascii", 8, 12), "WEBP", `${id} is not WebP`);
    assert.ok(buf.length <= CAP_BYTES, `${id} is ${buf.length} B, cap is ${CAP_BYTES}`);
    assert.ok(buf.length > 0, `${id} is empty`);
  }
});

test("each photograph is 400x300", () => {
  for (const id of PRODUCT_IDS) {
    const buf = readFileSync(join(DEFAULT_SET, `${id}.webp`));
    const size = readVp8Size(buf);
    assert.ok(size, `${id} is not a VP8 key frame we can size`);
    assert.equal(size.width, 400, `${id} width ${size.width}`);
    assert.equal(size.height, 300, `${id} height ${size.height}`);
  }
});

test("the default set does not carry extra product photographs", () => {
  const present = readdirSync(DEFAULT_SET)
    .filter((name) => name.endsWith(".webp"))
    .sort();
  const expected = PRODUCT_IDS.map((id) => `${id}.webp`).sort();
  assert.deepEqual(present, expected);
});
