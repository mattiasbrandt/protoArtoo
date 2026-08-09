// =============================================================================
// seq_last_run_json.cpp  --  pure JSON view of SeqRunEvidence.
// =============================================================================

#include "seq_last_run_json.h"

#include "sequence_engine.h"  // seqEngineRingPanelCount/Number

bool populateSeqLastRunJson(JsonDocument& doc, const SeqRunEvidence& ev, bool have) {
    doc.clear();
    doc["valid"] = have;
    if (!have) {
        doc["note"] = "no sequence run recorded since boot";
        return !doc.overflowed();
    }

    doc["name"] = ev.name;
    doc["source"] = ev.source;
    doc["outcome"] = seqRunOutcomeName(ev.outcome);
    doc["running"] = (ev.outcome == SEQ_RUN_RUNNING);
    if (ev.reason[0] != '\0') doc["reason"] = ev.reason;
    doc["startMs"] = ev.startMs;
    if (ev.endMs != 0) doc["endMs"] = ev.endMs;

    JsonArray scopes = doc["fxScopes"].to<JsonArray>();
    if (ev.fxScopes & SEQ_EVID_FX_PANEL)     scopes.add("panel");
    if (ev.fxScopes & SEQ_EVID_FX_LOGIC_PSI) scopes.add("logic_psi");
    if (ev.fxScopes & SEQ_EVID_FX_HOLO)      scopes.add("holo");
    if (ev.fxScopes & SEQ_EVID_FX_AUDIO)     scopes.add("audio");
    if (ev.fxScopes & SEQ_EVID_FX_DOME_SEQ)  scopes.add("dome_seq");

    JsonArray netOpen = doc["netOpenRingPanels"].to<JsonArray>();
    JsonArray touched = doc["touchedRingPanels"].to<JsonArray>();
    const uint8_t rc = seqEngineRingPanelCount();
    for (uint8_t i = 0; i < rc; ++i) {
        const int n = seqEngineRingPanelNumber(i);
        if (n < 0) continue;
        if (ev.netOpenRingMask & (uint16_t)(1u << i)) netOpen.add(n);
        if (ev.touchedRingMask & (uint16_t)(1u << i)) touched.add(n);
    }

    JsonObject cu = doc["cleanup"].to<JsonObject>();
    cu["count"] = ev.cleanupCount;
    cu["total"] = ev.cleanupTotalCount;
    cu["truncated"] = ev.cleanupTruncated;
    JsonArray cuArr = cu["cmds"].to<JsonArray>();
    for (uint8_t i = 0; i < ev.cleanupCount; ++i) cuArr.add(ev.cleanup[i]);

    JsonObject tx = doc["tx"].to<JsonObject>();
    tx["total"] = ev.txTotalCount;
    tx["capacity"] = (uint16_t)SEQ_EVID_TX_CAP;
    tx["omittedFromRecent"] = ev.txOmittedRecentCount;
    tx["truncated"] = (ev.txOmittedRecentCount > 0);
    const uint16_t stored =
        (ev.txTotalCount < SEQ_EVID_TX_CAP) ? ev.txTotalCount : SEQ_EVID_TX_CAP;
    tx["retained"] = stored;
    JsonArray txArr = tx["recent"].to<JsonArray>();
    const uint8_t start =
        (ev.txTotalCount < SEQ_EVID_TX_CAP) ? 0 : ev.txHead;
    for (uint16_t k = 0; k < stored; ++k) {
        txArr.add(ev.tx[(uint8_t)((start + k) % SEQ_EVID_TX_CAP)]);
    }

    JsonObject warn = doc["warnings"].to<JsonObject>();
    warn["bodyQueueFullDelta"] = ev.bodyQueueFullDelta;
    warn["dispatchRetryCount"] = ev.dispatchRetryCount;
    JsonObject remote = warn["remoteDomeQueue"].to<JsonObject>();
    remote["sampled"] = false;
    remote["queueFullDelta"] = nullptr;

    return !doc.overflowed();
}
