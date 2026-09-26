// =============================================================================
// src/config_record_guided_setup.cpp
//
// Guided Setup's Record (#351): its fields, their check, their merge onto the
// live record, its GET answer and its NVS save and load, in one module
// (include/config_records.h, include/config_record_guided_setup.h).
//
// Nothing downstream is gated on it. Firmware stores this record and checks its
// FORM - a run state this image can name, step keys made of characters a key
// may contain - because an arbitrary request string would otherwise reach NVS
// and come back out in a JSON payload. Which steps EXIST is the browser's
// question, not this one's: the run is drawn there and the list grows
// (include/guided_setup.h).
// =============================================================================

#include "config_record_guided_setup.h"

#include <freertos/FreeRTOS.h>
#include <string.h>

#include "config_records.h"
#include "config_write_window_check.h"  // the merge is a config write, inside its Write Window

namespace {

// Where the run stands, and which of its steps have been on screen.
// "gsetup_visited" is fourteen characters and so one clear of the 15-character
// Preferences ceiling; each reads in an NVS dump as the answer it is. Unchanged
// since #351 and #371, so a stored record loads as it is.
constexpr char GUIDED_SETUP_RUN_KEY[] = "gsetup_run";
constexpr char GUIDED_SETUP_VISITED_KEY[] = "gsetup_visited";
// Whether the builder pressed Done on the ended run's summary (#371). Absent
// reads as not done, so a controller that ended its run before this key existed
// shows the summary once.
constexpr char GUIDED_SETUP_SUMMARY_DONE_KEY[] = "gsetup_done";
static_assert(sizeof(GUIDED_SETUP_SUMMARY_DONE_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");
static_assert(sizeof(GUIDED_SETUP_RUN_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");
static_assert(sizeof(GUIDED_SETUP_VISITED_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");

// Three independent facts, so three fields. A step being marked visited is the
// browser saying "this question has now actually been on screen", and it
// happens many times during one run; the run ending happens once; the summary
// is dismissed after that. A request that carries one says nothing about the
// others, which is what the merge keeps true. The examples are a run no fresh
// controller holds.
enum Field : uint8_t { Run, Visited, SummaryDone, FieldCount };

const ConfigRecordField kFields[FieldCount] = {
    {"guidedSetupRun", "guidedSetup.run", "completed"},
    {"guidedSetupVisited", "guidedSetup.visited", "drive,sound"},
    {"guidedSetupSummaryDone", "guidedSetup.summaryDone", "true"},
};

constexpr uint32_t fieldBit(Field field) { return (uint32_t)1u << field; }

// The live record. Filled by configRecordGuidedSetupLoad() from main's boot
// path, like the Droid Build, and changed at runtime only by the Commit Step's
// merge. Nothing on a real-time path reads it: it exists so a surface can tell a
// category the builder DECLARED not fitted from one they were never asked
// about - a difference invisible in the toggles themselves, because both read
// false.
GuidedSetupConfig live = {};
portMUX_TYPE liveMux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

const ConfigRecordField* configRecordGuidedSetupFields(size_t* count) {
    *count = FieldCount;
    return kFields;
}

bool configRecordGuidedSetupCheck(const ConfigRecordCheck& check, GuidedSetupConfig* staged,
                                  uint32_t* stated) {
    const char* runName = kFields[Run].form;
    if (configParamHas(check.params, runName)) {
        if (!guidedSetupRunFromId(configParamGet(check.params, runName), &staged->run)) {
            configRecordRefuse(check, "guidedSetupRun must be not-run, skipped or completed",
                               ApplyRefusalReason::OutOfRange, runName,
                               "not-run,skipped,completed");
            return false;
        }
        *stated |= fieldBit(Run);
        configRecordLog(check, "[CFG] guidedSetupRun updated to %s", guidedSetupRunId(staged->run));
    }

    // The visited list arrives WHOLE rather than as an addition, for the reason
    // the Fitted Parts do: the browser holds the run, an add-one wire would need
    // a remove-one to match it, and replacing the list keeps the two ends from
    // drifting into disagreement about what has been shown. An EMPTY value is a
    // real answer - the run has been drawn and nothing has been shown yet. A key
    // this image cannot read is refused rather than dropped: a shortened record
    // would report a step the builder WAS shown as one they never were, which is
    // the untruth the record exists to prevent.
    const char* visitedName = kFields[Visited].form;
    if (configParamHas(check.params, visitedName)) {
        if (guidedSetupVisitedSet(staged, configParamGet(check.params, visitedName)) > 0) {
            configRecordRefuse(check,
                               "guidedSetupVisited must be a comma-separated list of step keys, "
                               "each at most 12 characters of a-z, 0-9 and _",
                               ApplyRefusalReason::OutOfRange, visitedName);
            return false;
        }
        staged->recorded = true;
        *stated |= fieldBit(Visited);
        configRecordLog(check, "[CFG] guidedSetupVisited updated");
    }

    const char* doneName = kFields[SummaryDone].form;
    if (configParamHas(check.params, doneName)) {
        const char* value = configParamGet(check.params, doneName);
        if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
            configRecordRefuse(check, "guidedSetupSummaryDone must be true or false",
                               ApplyRefusalReason::OutOfRange, doneName, "true,false");
            return false;
        }
        staged->summaryDone = strcmp(value, "true") == 0;
        *stated |= fieldBit(SummaryDone);
        configRecordLog(check, "[CFG] guidedSetupSummaryDone updated to %s", value);
    }
    return true;
}

// Each fact merged on its own: marking a step visited says nothing about
// whether the run has ended, and ending the run says nothing about which steps
// were shown, so a request carrying one leaves the others exactly as they
// stood. A visited list stated at all is a record that now exists.
void configRecordGuidedSetupMerge(const GuidedSetupConfig& staged, uint32_t stated) {
    configWriteWindowExpectHeld("configRecordGuidedSetupMerge");
    taskENTER_CRITICAL(&liveMux);
    if ((stated & fieldBit(Run)) != 0) {
        live.run = staged.run;
    }
    if ((stated & fieldBit(SummaryDone)) != 0) {
        live.summaryDone = staged.summaryDone;
    }
    if ((stated & fieldBit(Visited)) != 0) {
        live.recorded = true;
        memcpy(live.visited, staged.visited, sizeof(live.visited));
    }
    taskEXIT_CRITICAL(&liveMux);
}

void configRecordGuidedSetupRead(GuidedSetupConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&liveMux);
    *out = live;
    taskEXIT_CRITICAL(&liveMux);
}

// `recorded` is the field that looks redundant and is not. A controller
// configured before guided Setup existed carries no record at all, and an empty
// `visited` array on its own cannot say whether that means "the run has shown
// nothing yet" or "the run has never been drawn here". Only the second of those
// may be read as "these answers were given before the record existed, and are
// real"; the browser, which is the only end that knows what the steps are, makes
// that call and needs this bit to make it. It is a reading, not a field: POST
// never takes it.
//
// The run goes out as a token rather than its stored number for the reason the
// Fitted Parts go out as ids: firmware and the browser module ship in two
// separate steps ('make ota' and 'make uploadfs'), so a number is the one form
// that could mean a different thing at each end of the wire.
void configRecordGuidedSetupAnswer(JsonObject out) {
    GuidedSetupConfig guided = {};
    configRecordGuidedSetupRead(&guided);

    out["run"] = guidedSetupRunId(guided.run);
    out["recorded"] = guided.recorded;
    out["summaryDone"] = guided.summaryDone;

    JsonArray visited = out["visited"].to<JsonArray>();
    const char* cursor = guided.visited;
    while (*cursor != '\0') {
        const char* comma = strchr(cursor, ',');
        const size_t span = (comma != nullptr) ? (size_t)(comma - cursor) : strlen(cursor);
        // A mutable char array, deliberately: ArduinoJson stores a `const char*`
        // by pointer and DUPLICATES a `char*`, and this buffer is gone by the
        // time the document serializes.
        char key[GUIDED_SETUP_STEP_KEY_MAX + 1] = {};
        if (span > 0 && span <= GUIDED_SETUP_STEP_KEY_MAX) {
            memcpy(key, cursor, span);
            visited.add(key);
        }
        if (comma == nullptr) {
            break;
        }
        cursor = comma + 1;
    }
}

// Inside the config Write Window, which configRecordsSave() checks for every
// Record's save.
bool configRecordGuidedSetupSave(ConfigWriter& writer) {
    GuidedSetupConfig guided = {};
    configRecordGuidedSetupRead(&guided);
    return configSerializeGuidedSetup(guided, writer);
}

// Straight into the live copy, like the Droid Build: it runs once from setup(),
// before anything that reads it exists.
bool configRecordGuidedSetupLoad(const ConfigReader& reader, char* repaired, size_t repairedSize) {
    GuidedSetupRepairReport report = {};
    configDeserializeGuidedSetup(reader, &live, &report);
    if (guidedSetupRepairReportIsClean(report)) {
        return false;
    }
    // A record that came back shorter than it went in. Said out loud rather
    // than swallowed: a dropped step key is a step the builder WAS shown that
    // this controller can no longer say they were, which is the same untruth
    // the record exists to prevent, arriving from the other side.
    snprintf(repaired, repairedSize,
             "guided setup record repaired: run=%s, %u step key(s) this build cannot read",
             report.runRepaired ? "reset" : "kept", (unsigned)report.stepsDropped);
    return true;
}

// -----------------------------------------------------------------------------
// The storage form
//
// The visited list is written even when it is empty, as the sentinel: NVS keeps
// every key a save does not touch, and "nothing visited" written as an empty
// string would read back on the next cold boot as a controller guided Setup has
// never drawn on - which is the one thing this record has to be able to tell
// apart (include/guided_setup.h).
//
// No heap here, unlike the Droid Build: this list is bounded at
// GUIDED_SETUP_VISITED_STR_MAX and is already held as the joined string, so
// there is nothing to build and nothing to free.
// -----------------------------------------------------------------------------
bool configSerializeGuidedSetup(const GuidedSetupConfig& cfg, ConfigWriter& w) {
    bool ok = w.writeU8(GUIDED_SETUP_RUN_KEY, (uint8_t)cfg.run);
    ok = w.writeStr(GUIDED_SETUP_VISITED_KEY, guidedSetupVisitedStored(cfg)) && ok;
    ok = w.writeBool(GUIDED_SETUP_SUMMARY_DONE_KEY, cfg.summaryDone) && ok;
    return ok;
}

void configDeserializeGuidedSetup(const ConfigReader& r, GuidedSetupConfig* out,
                                  GuidedSetupRepairReport* report) {
    if (out == nullptr) {
        return;
    }
    guidedSetupDefaults(out);

    GuidedSetupRepairReport local = {};

    const uint8_t storedRun = r.readU8(GUIDED_SETUP_RUN_KEY, (uint8_t)GUIDED_SETUP_NOT_RUN);
    out->run = guidedSetupRunFromStored(storedRun);
    local.runRepaired = ((uint8_t)out->run != storedRun);
    out->summaryDone = r.readBool(GUIDED_SETUP_SUMMARY_DONE_KEY, false);

    // Absent, sentinel, or a list - and the first of those is the one that
    // carries a fact nothing else can: guided Setup has never been drawn on this
    // controller. The writer never stores an empty string, so an empty read is
    // unambiguously "no record".
    const String visited = r.readStr(GUIDED_SETUP_VISITED_KEY, "");
    if (visited.length() > 0) {
        out->recorded = true;
        local.stepsDropped = guidedSetupVisitedSet(out, visited.c_str());
    }

    if (report != nullptr) {
        *report = local;
    }
}
