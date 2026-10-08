// =============================================================================
// src/config_record_droid_build.cpp
//
// The Droid Build Record (ADR 0047): its fields, their check, their merge onto
// the live answer, its GET answer and its NVS save and load, in one module
// (include/config_records.h, include/config_record_droid_build.h).
//
// Nothing downstream is gated on any of it. The Fitted Parts are checked
// against the catalog vocabulary only so a Part id this build cannot name never
// reaches storage - the same form check droidPartIdIsKnown() is, and NOT a
// narrowing of it: a Part outside the fitted set stays authorable, saveable and
// wirable, which is the decision this whole field exists to keep (ADR 0047,
// #333).
//
// The two halves are never compared. An MK4.1 dome on an MK4 Basic body is an
// ordinary droid, and refusing that pairing is the other way this could quietly
// undo itself.
// =============================================================================

#include "config_record_droid_build.h"

#include <freertos/FreeRTOS.h>
#include <stdlib.h>
#include <string.h>

#include "config_records.h"
#include "config_write_window_check.h"  // the merge is a config write, inside its Write Window

namespace {

// Five records, each twelve characters and so three clear of the 15-character
// Preferences ceiling, named so an NVS dump reads as the answer it is: which
// design each half was built from, at which variant, and which Parts are on the
// droid. Unchanged since ADR 0047, so a stored Droid Build loads as it is.
constexpr char DROID_BUILD_DOME_DESIGN_KEY[] = "dbuild_domed";
constexpr char DROID_BUILD_DOME_VARIANT_KEY[] = "dbuild_domev";
constexpr char DROID_BUILD_BODY_DESIGN_KEY[] = "dbuild_bodyd";
constexpr char DROID_BUILD_BODY_VARIANT_KEY[] = "dbuild_bodyv";
constexpr char DROID_BUILD_FITTED_KEY[] = "dbuild_parts";

// The fields, in the order the check reads them. A half is a PAIR - a design
// and the variant of that design - because a variant only means anything
// against the design it belongs to; the Fitted Parts arrive whole for the same
// reason a set does: there is no merge to do and nothing here has to know what
// was fitted before. The examples are a droid no fresh controller holds: a
// fresh one is MK4 Complex on both halves (droidBuildDefaults()).
enum Field : uint8_t { DomeDesign, DomeVariant, BodyDesign, BodyVariant, FittedParts, FieldCount };

const ConfigRecordField kFields[FieldCount] = {
    {"domeDesign", "droidBuild.domeDesign", ApplyTiming::Immediate, "mk41"},
    {"domeVariant", "droidBuild.domeVariant", ApplyTiming::Immediate, ""},
    {"bodyDesign", "droidBuild.bodyDesign", ApplyTiming::Immediate, "own"},
    {"bodyVariant", "droidBuild.bodyVariant", ApplyTiming::Immediate, ""},
    {"fittedParts", "droidBuild.fitted", ApplyTiming::Immediate, "gripArm,utilUp"},
};

constexpr uint32_t fieldBit(Field field) { return (uint32_t)1u << field; }

// The live answer. Filled by configRecordDroidBuildLoad() from main's boot
// path, before any task that reads it exists, and changed at runtime only by
// the Commit Step's merge. Nothing on a real-time path reads it: it exists so
// the surfaces that draw a builder's droid meet the same answer from any
// browser, and no firmware behaviour branches on it. Sixty bytes, handed out
// whole: its readers draw a screen rather than drive a motor.
DroidBuildConfig live = {};
portMUX_TYPE liveMux = portMUX_INITIALIZER_UNLOCKED;

// One half of a Droid Build - a design and the variant of that design - read,
// checked against the catalog vocabulary, and staged. False, with the refusal
// written, when the request named this half and got it wrong. A request that
// named neither field of the half is not an error: a POST that is not about
// the Droid Build is most of them.
//
// Sending one field without the other would ask this check to validate half an
// answer against the other half's stored design, which is a pairing the
// builder never stated - and on a design change it is exactly the pairing that
// is wrong.
bool checkHalf(const ConfigRecordCheck& check, Field designField, Field variantField,
               const char* refusal, DroidDesignChoice* out, uint32_t* stated) {
    const char* designName = kFields[designField].form;
    const char* variantName = kFields[variantField].form;
    const bool hasDesign = configParamHas(check.params, designName);
    const bool hasVariant = configParamHas(check.params, variantName);
    if (!hasDesign && !hasVariant) {
        return true;
    }
    if (!hasDesign || !hasVariant) {
        configRecordRefuse(check, refusal, ApplyRefusalReason::MissingArgument,
                           hasDesign ? variantName : designName);
        return false;
    }
    DroidDesignChoice choice = {};
    if (!droidDesignChoiceSet(&choice, configParamGet(check.params, designName),
                              configParamGet(check.params, variantName)) ||
        !droidDesignChoiceIsKnown(choice)) {
        // The catalog answers for the pair, not for either half alone, so the
        // design names the refusal.
        configRecordRefuse(check, refusal, ApplyRefusalReason::OutOfRange, designName);
        return false;
    }
    *out = choice;
    *stated |= fieldBit(designField) | fieldBit(variantField);
    configRecordLog(check, "[CFG] %s updated to %s/%s", designName, choice.design, choice.variant);
    return true;
}

// One stored half, or the default when what is stored is not an answer this
// image's catalog can name. True when it had to repair.
//
// Both fields move together: a design and the variant of that design are one
// answer, and keeping a stored variant beside a defaulted design would produce
// a pairing neither the builder nor the catalog ever stated.
bool readDroidDesignChoice(const ConfigReader& r, const char* designKey, const char* variantKey,
                           DroidDesignChoice* out) {
    const String design = r.readStr(designKey, out->design);
    const String variant = r.readStr(variantKey, out->variant);
    DroidDesignChoice stored = {};
    if (!droidDesignChoiceSet(&stored, design.c_str(), variant.c_str()) ||
        !droidDesignChoiceIsKnown(stored)) {
        return true;  // *out is left holding the default it arrived with
    }
    *out = stored;
    return false;
}

}  // namespace

const ConfigRecordField* configRecordDroidBuildFields(size_t* count) {
    *count = FieldCount;
    return kFields;
}

bool configRecordDroidBuildCheck(const ConfigRecordCheck& check, DroidBuildConfig* staged,
                                 uint32_t* stated) {
    if (!checkHalf(check, DomeDesign, DomeVariant,
                   "domeDesign and domeVariant must be sent together, and name a design and one "
                   "of its own variants",
                   &staged->dome, stated)) {
        return false;
    }
    if (!checkHalf(check, BodyDesign, BodyVariant,
                   "bodyDesign and bodyVariant must be sent together, and name a design and one "
                   "of its own variants",
                   &staged->body, stated)) {
        return false;
    }

    // The Fitted Parts arrive whole, as a comma-separated Part id list. An
    // EMPTY value is a real answer - a droid with nothing fitted yet - and is
    // staged; the field being absent is what means "this request is not about
    // the Fitted Parts".
    const char* fittedName = kFields[FittedParts].form;
    if (configParamHas(check.params, fittedName)) {
        if (droidFittedPartsParse(configParamGet(check.params, fittedName), &staged->fitted) != 0) {
            configRecordRefuse(check, "fittedParts names a Part this build does not model",
                               ApplyRefusalReason::OutOfRange, fittedName);
            return false;
        }
        *stated |= fieldBit(FittedParts);
        configRecordLog(check, "[CFG] fittedParts updated to %u part(s)",
                        (unsigned)droidFittedPartsCount(staged->fitted));
    }
    return true;
}

// A half the request did not name is left exactly as it stood: a builder
// changing their Dome Design is not saying anything about their body, and this
// merge is what keeps that true. Nothing to check here: the check refused
// anything the catalog does not declare.
void configRecordDroidBuildMerge(const DroidBuildConfig& staged, uint32_t stated) {
    configWriteWindowExpectHeld("configRecordDroidBuildMerge");
    taskENTER_CRITICAL(&liveMux);
    if ((stated & fieldBit(DomeDesign)) != 0) {
        live.dome = staged.dome;
    }
    if ((stated & fieldBit(BodyDesign)) != 0) {
        live.body = staged.body;
    }
    if ((stated & fieldBit(FittedParts)) != 0) {
        live.fitted = staged.fitted;
    }
    taskEXIT_CRITICAL(&liveMux);
}

void configRecordDroidBuildRead(DroidBuildConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&liveMux);
    *out = live;
    taskEXIT_CRITICAL(&liveMux);
}

// The Fitted Parts go out as ids rather than as the bitmap they are held in:
// the bits are emission order, and firmware and the browser module are shipped
// by two separate steps ('make ota' and 'make uploadfs'), so a bit index is the
// one form that could mean a different Part at each end of the wire.
//
// An empty `fitted` array is a real answer - a droid with nothing fitted yet -
// and every Part the catalog declares stays nameable regardless: this reports
// what is ON the droid, never what may be authored for it.
void configRecordDroidBuildAnswer(JsonObject out) {
    DroidBuildConfig build = {};
    configRecordDroidBuildRead(&build);

    // Char arrays of a local struct, so ArduinoJson copies them: it stores a
    // `const char*` by pointer and DUPLICATES a `char*`, and this frame is gone
    // by the time the document serializes.
    out["domeDesign"] = build.dome.design;
    out["domeVariant"] = build.dome.variant;
    out["bodyDesign"] = build.body.design;
    out["bodyVariant"] = build.body.variant;

    JsonArray fitted = out["fitted"].to<JsonArray>();
    for (size_t i = droidFittedPartsNextIndex(build.fitted, 0); i < DROID_PART_COUNT;
         i = droidFittedPartsNextIndex(build.fitted, i + 1)) {
        fitted.add(droidPartIdAt(i));
    }
}

// Inside the config Write Window, which configRecordsSave() checks for every
// Record's save.
bool configRecordDroidBuildSave(ConfigWriter& writer) {
    DroidBuildConfig build = {};
    configRecordDroidBuildRead(&build);
    return configSerializeDroidBuild(build, writer);
}

// Straight into the live copy: it runs once from setup(), before anything that
// reads it exists, so the answer never becomes a frame on loopTask's stack.
bool configRecordDroidBuildLoad(const ConfigReader& reader, char* repaired, size_t repairedSize) {
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &live, &report);
    if (droidBuildRepairReportIsClean(report)) {
        return false;
    }
    // A stored Droid Build this image's catalog can no longer name has taken
    // the pre-selected design instead. Said out loud: a builder whose stated
    // design vanished under a firmware update should hear it at boot rather
    // than discover it on the parts list.
    snprintf(repaired, repairedSize,
             "droid build repaired: dome=%s body=%s, %u fitted part(s) this build does not declare",
             report.domeRepaired ? "default" : "kept", report.bodyRepaired ? "default" : "kept",
             (unsigned)report.partsDropped);
    return true;
}

// -----------------------------------------------------------------------------
// The storage form
//
// The Fitted Parts are joined on the heap rather than in a local char buffer on
// purpose: the joined list is DROID_FITTED_PARTS_STR_MAX bytes, and a frame
// that size would sit on the serial config-write path, whose task stack chain
// is a measured constant that one ConfigSnapshot-sized frame per nesting level
// already nearly overran once (include/config.h, include/config_store.h, #226).
// One bounded allocation on a Core 0 write path costs that chain nothing, and a
// failed one is reported rather than swallowed - the caller answers "not
// persisted" and the writer has touched nothing.
// -----------------------------------------------------------------------------
bool configSerializeDroidBuild(const DroidBuildConfig& cfg, ConfigWriter& w) {
    // The one step that can fail for a reason other than storage goes first, so
    // a controller that could not allocate has not had half an answer written
    // over the one it already held.
    char* fitted = (char*)malloc(DROID_FITTED_PARTS_STR_MAX + 1);
    if (fitted == nullptr) {
        return false;  // the caller reports a failed persist; nothing was written
    }
    size_t used = 0;
    for (size_t i = droidFittedPartsNextIndex(cfg.fitted, 0); i < DROID_PART_COUNT;
         i = droidFittedPartsNextIndex(cfg.fitted, i + 1)) {
        const int n = snprintf(fitted + used, DROID_FITTED_PARTS_STR_MAX + 1 - used, "%s%s",
                               used == 0 ? "" : ",", droidPartIdAt(i));
        if (n <= 0 || (size_t)n >= DROID_FITTED_PARTS_STR_MAX + 1 - used) {
            free(fitted);
            return false;  // the bound above is arithmetic, so this is unreachable
        }
        used += (size_t)n;
    }
    // A droid with nothing fitted is an answer, and writing it as an empty
    // string would read back as a controller nobody has answered yet.
    if (used == 0) {
        snprintf(fitted, DROID_FITTED_PARTS_STR_MAX + 1, "%s", DROID_FITTED_PARTS_NONE);
    }
    bool ok = w.writeStr(DROID_BUILD_DOME_DESIGN_KEY, cfg.dome.design);
    ok = w.writeStr(DROID_BUILD_DOME_VARIANT_KEY, cfg.dome.variant) && ok;
    ok = w.writeStr(DROID_BUILD_BODY_DESIGN_KEY, cfg.body.design) && ok;
    ok = w.writeStr(DROID_BUILD_BODY_VARIANT_KEY, cfg.body.variant) && ok;
    ok = w.writeStr(DROID_BUILD_FITTED_KEY, fitted) && ok;
    free(fitted);
    return ok;
}

void configDeserializeDroidBuild(const ConfigReader& r, DroidBuildConfig* out,
                                 DroidBuildRepairReport* report) {
    if (out == nullptr) {
        return;
    }
    droidBuildDefaults(out);

    DroidBuildRepairReport local = {};
    local.domeRepaired = readDroidDesignChoice(r, DROID_BUILD_DOME_DESIGN_KEY,
                                               DROID_BUILD_DOME_VARIANT_KEY, &out->dome);
    local.bodyRepaired = readDroidDesignChoice(r, DROID_BUILD_BODY_DESIGN_KEY,
                                               DROID_BUILD_BODY_VARIANT_KEY, &out->body);

    // Three states, and the middle one is the whole reason this record is not
    // read as a plain string: absent means nobody has answered, so the
    // pre-selected design's complement stands; "-" means a builder said their
    // droid carries nothing yet, which is theirs to say and is kept.
    const String fitted = r.readStr(DROID_BUILD_FITTED_KEY, "");
    if (fitted.length() > 0) {
        if (strcmp(fitted.c_str(), DROID_FITTED_PARTS_NONE) == 0) {
            droidFittedPartsClear(&out->fitted);
        } else {
            local.partsDropped = droidFittedPartsParse(fitted.c_str(), &out->fitted);
        }
    }

    if (report != nullptr) {
        *report = local;
    }
}
