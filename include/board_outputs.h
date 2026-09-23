// =============================================================================
// include/board_outputs.h
//
// The body controller's Outputs, and what the running board calls each one.
//
// An Output is called by what its board prints beside its pin - its Board
// Component Label - on every screen, in the Console's list and completion, and
// as the word typed in the Console and sent to POST /api/servo: ARM1..ARM5 on
// the Artoo PCB, GPIO 49 / GPIO 50 / GPIO 4 / GPIO 5 / GPIO 51 on the
// FireBeetle 2 (CONTEXT.md "Output Address", "Board Component Label"; ADR 0033
// Amendment 2026-09-19). There is no protoArtoo-wide name for an Output and no
// split into arm and AUX kinds.
//
// ONE SOURCE PER BOARD. What a board prints lives in include/component_labels.inc
// and nowhere else; this header reads it through boardComponentLabel(), and
// GET /api/config, GET /api/servo/outputs, POST /api/servo and the Console all
// ask the same function. No second label table exists, in the firmware or in
// the browser.
//
// The ids (arm1..aux3), the component keys (enable_arm1..enable_aux3) and the
// config field names are stored identifiers - config keys, NVS keys, the
// components{} JSON keys, RC tokens - and are never a name a builder reads.
//
// HEADER-ONLY and constexpr, the way include/component_registry.h reads its own
// manifest: the lookup takes the board as a parameter, so a native test asks
// what the FireBeetle 2 calls an Output from an image built for the Artoo, and
// the running image passes runningBoardName().
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"    // PA_BOARD
#include "ledc_pwm.h"  // LEDC_CH_*

namespace board_outputs_detail {
// constexpr string compare. Only ever fed manifest tokens, string literals and
// NUL-terminated request text.
constexpr bool equals(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

constexpr char foldCase(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}
}  // namespace board_outputs_detail

// The Board Variant this image is built for, spelled the way
// include/component_labels.inc names it.
constexpr const char* runningBoardName() {
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
    return "artoo_esp32";
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
    return "firebeetle2";
#else
#error "PA_BOARD value not recognized by board_outputs.h"
#endif
}

// The Board Component Label `board` declares for `component`, or nullptr where
// that board declares none.
constexpr const char* boardComponentLabel(const char* board, const char* component) {
#define PA_COMPONENT_LABEL(board_name, component_name, label_text)             \
    if (board_outputs_detail::equals(board, #board_name) &&                    \
        board_outputs_detail::equals(component, #component_name)) {            \
        return (label_text);                                                   \
    }
#include "component_labels.inc"
#undef PA_COMPONENT_LABEL
    return nullptr;
}

// -----------------------------------------------------------------------------
// The Outputs, in the order every surface draws them. The order is also
// ServoCommand::armId's (include/servo_helpers.h is the channel <-> armId
// bridge), which is why the table is not sorted by anything a reader sees.
// -----------------------------------------------------------------------------
struct BoardOutput {
    const char* id;            // stored config key, the components{} key; never shown
    const char* component;     // its key in include/component_labels.inc
    uint8_t channel;           // the LEDC channel this image drives it on
    // Whether a Light Type may go on this wire at all (ADR 0067). It is a board
    // fact, not a builder's answer: these are the lines each board's pin plan
    // reserves for a WS2812B's timing (include/config.h PIN_ARM3..5_SERVO), and
    // which of them actually carries a light is the Output's own `component`,
    // one per wire. It replaced a single aux_led_pin slot number, which was a
    // second store of the same fact and could only ever name one (#413).
    bool lightCapable;
    const char* enabledField;  // the POST /api/config field that saves it as wired
    const char* typeField;     // the POST /api/config field that saves what it carries
    // The POST /api/config field that saves the Light Type's settings - how
    // many LEDs the wire carries. nullptr where a light cannot go, so an Output
    // that could never be lit reports no field for it and no surface draws one.
    const char* ledCountField;
};

inline constexpr BoardOutput BOARD_OUTPUTS[] = {
    {"arm1", "enable_arm1", LEDC_CH_ARM1, false, "enableArm1", "arm1Type", nullptr},
    {"arm2", "enable_arm2", LEDC_CH_ARM2, false, "enableArm2", "arm2Type", nullptr},
    {"aux1", "enable_aux1", LEDC_CH_AUX1, true, "enableAux1", "aux1Type", "aux1LedCount"},
    {"aux2", "enable_aux2", LEDC_CH_AUX2, true, "enableAux2", "aux2Type", "aux2LedCount"},
    {"aux3", "enable_aux3", LEDC_CH_AUX3, true, "enableAux3", "aux3Type", "aux3LedCount"},
};

inline constexpr size_t BOARD_OUTPUT_COUNT = sizeof(BOARD_OUTPUTS) / sizeof(BOARD_OUTPUTS[0]);

// What `board` prints beside `output`. Never nullptr for a board that builds:
// the static_asserts below refuse a board with an unlabelled Output.
constexpr const char* boardOutputLabel(const char* board, const BoardOutput& output) {
    return boardComponentLabel(board, output.component);
}

inline const char* boardOutputLabel(const BoardOutput& output) {
    return boardOutputLabel(runningBoardName(), output);
}

// Whether a typed word names the Output labelled `label`: the board's own word,
// matched without regard to case or spaces, so "gpio49", "GPIO 49" and
// "Gpio 49" all name the FireBeetle 2's GPIO 49, and "arm3" names ARM3.
constexpr bool boardOutputWordMatches(const char* typed, const char* label) {
    if (typed == nullptr || label == nullptr) {
        return false;
    }
    for (;;) {
        while (*typed == ' ') ++typed;
        while (*label == ' ') ++label;
        if (*typed == '\0' || *label == '\0') {
            return *typed == *label;
        }
        if (board_outputs_detail::foldCase(*typed) != board_outputs_detail::foldCase(*label)) {
            return false;
        }
        ++typed;
        ++label;
    }
}

// The Output `word` names on `board`, or nullptr when it names none of them -
// including every old protoArtoo-wide word (aux1..aux3 on the Artoo, all of
// arm1..aux3 on the FireBeetle 2), which is not an alias of anything (ADR 0033
// Amendment 2026-09-19).
constexpr const BoardOutput* boardOutputForWord(const char* board, const char* word) {
    for (const BoardOutput& output : BOARD_OUTPUTS) {
        if (boardOutputWordMatches(word, boardOutputLabel(board, output))) {
            return &output;
        }
    }
    return nullptr;
}

inline const BoardOutput* boardOutputForWord(const char* word) {
    return boardOutputForWord(runningBoardName(), word);
}

// The Output driven on this LEDC channel, or nullptr (the dome ESC's channel is
// not an Output).
constexpr const BoardOutput* boardOutputOnChannel(uint8_t channel) {
    for (const BoardOutput& output : BOARD_OUTPUTS) {
        if (output.channel == channel) {
            return &output;
        }
    }
    return nullptr;
}

// The Output stored under `id` (arm1..aux3), or nullptr.
constexpr const BoardOutput* boardOutputById(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    for (const BoardOutput& output : BOARD_OUTPUTS) {
        if (board_outputs_detail::equals(output.id, id)) {
            return &output;
        }
    }
    return nullptr;
}

// A registry row about one Output names it `{output}` rather than by a name,
// because the registry is one file for every board and the name is the board's
// (docs/action-registry.yaml header, `output:`). This writes `text` (textLen
// bytes, not necessarily terminated) into `out` with every `{output}` replaced
// by the running board's label for the Output stored under `outputId`, and
// returns the length the whole composed text needs, as snprintf does, so a
// caller can tell a clamp. `out` is always terminated. With no `outputId`, or
// no placeholder in the text, it is a plain copy.
inline size_t boardOutputComposeText(const char* text, size_t textLen, const char* outputId,
                                     char* out, size_t outSize) {
    static const char kPlaceholder[] = "{output}";
    const size_t placeholderLen = sizeof(kPlaceholder) - 1;
    const BoardOutput* output = boardOutputById(outputId);
    const char* label = output != nullptr ? boardOutputLabel(*output) : "";
    const size_t labelLen = strlen(label);

    size_t needed = 0;
    size_t written = 0;
    size_t i = 0;
    while (text != nullptr && i < textLen) {
        const char* piece = text + i;
        size_t pieceLen = 1;
        if (output != nullptr && textLen - i >= placeholderLen &&
            memcmp(text + i, kPlaceholder, placeholderLen) == 0) {
            piece = label;
            pieceLen = labelLen;
            i += placeholderLen;
        } else {
            ++i;
        }
        for (size_t k = 0; k < pieceLen; ++k) {
            if (written + 1 < outSize) {
                out[written++] = piece[k];
            }
        }
        needed += pieceLen;
    }
    if (out != nullptr && outSize > 0) {
        out[written] = '\0';
    }
    return needed;
}

// `board`'s words for its Outputs, in table order, joined by `separator` into
// `out` - what a refusal names so the builder sees what the board takes.
// Returns false when `out` is too small, leaving it cut short but terminated.
inline bool boardOutputWordList(const char* board, const char* separator, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        const int wrote = snprintf(out + used, outSize - used, "%s%s", i == 0 ? "" : separator,
                                   boardOutputLabel(board, BOARD_OUTPUTS[i]));
        if (wrote < 0 || (size_t)wrote >= outSize - used) {
            return false;
        }
        used += (size_t)wrote;
    }
    return true;
}

inline bool boardOutputWordList(const char* separator, char* out, size_t outSize) {
    return boardOutputWordList(runningBoardName(), separator, out, outSize);
}

// -----------------------------------------------------------------------------
// Every board declares a label for each of its Outputs, and no two of one
// board's labels are the same word once case and spaces are set aside - either
// would leave an Output nobody can name, on screen or in a typed command.
// Checked for every board, whichever one this image is for.
// -----------------------------------------------------------------------------
namespace board_outputs_detail {
constexpr bool everyOutputLabelled(const char* board) {
    for (const BoardOutput& output : BOARD_OUTPUTS) {
        const char* label = boardOutputLabel(board, output);
        if (label == nullptr || label[0] == '\0') {
            return false;
        }
    }
    return true;
}

constexpr bool everyOutputWordDistinct(const char* board) {
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        for (size_t j = i + 1; j < BOARD_OUTPUT_COUNT; ++j) {
            if (boardOutputWordMatches(boardOutputLabel(board, BOARD_OUTPUTS[i]),
                                       boardOutputLabel(board, BOARD_OUTPUTS[j]))) {
                return false;
            }
        }
    }
    return true;
}
}  // namespace board_outputs_detail

static_assert(board_outputs_detail::everyOutputLabelled("artoo_esp32"),
              "include/component_labels.inc: artoo_esp32 leaves an Output unlabelled");
static_assert(board_outputs_detail::everyOutputLabelled("firebeetle2"),
              "include/component_labels.inc: firebeetle2 leaves an Output unlabelled");
static_assert(board_outputs_detail::everyOutputWordDistinct("artoo_esp32"),
              "include/component_labels.inc: two artoo_esp32 Outputs share a word");
static_assert(board_outputs_detail::everyOutputWordDistinct("firebeetle2"),
              "include/component_labels.inc: two firebeetle2 Outputs share a word");
