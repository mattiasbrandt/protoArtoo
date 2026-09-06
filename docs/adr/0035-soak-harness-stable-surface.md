# The soak harness's stable surface is its exit codes and JSON keys, not its verdict wording

`tools/soak.py` is a permanent protoArtoo instrument rather than scaffolding for the epic that
produced it, so it needs a stated contract: **its exit codes and the keys of its JSON artefact are
stable and may be relied on; its verdict wording is prose and may be reworded.** A **Run Verdict**
speaks the same words its **Soak Driver** verdicts do (`PASS`, `FAIL`, `INVALID`), and an
unavailable driver still collapses to `INVALID` — the rule that a coverage gap is never a pass is
contract, the sentence expressing it is not.

## Considered options

- **Freeze the verdict strings too.** Rejected: it would freeze wording chosen under time pressure,
  and the first string that reads badly forces a choice between breaking the promise and living
  with it. The strings exist for a human reading stderr; the JSON is what a machine reads.
- **Promise nothing — it is a bench tool, read the source.** Rejected: honest about how the tool is
  used today (interactively), but it forecloses ever wiring a soak into CI or a scheduled
  regression run without first inventing the contract we declined to write.

## Consequences

- Renaming the verdict vocabulary is a legitimate, repeatable act rather than a one-off exception.
  The #184 go/no-go wording is retired on that basis, exactly as its pass-tier wording was.
- **A rename cannot invalidate a measurement.** Evidence from a run is its driver verdicts and the
  numbers beside them; those survive a rewording verbatim. Only a change to what the harness
  *judges* — a threshold, a classification, a field map — invalidates prior evidence and forces a
  re-run. This distinction was got wrong once, in the reasoning for sequencing a device session,
  and is recorded here so it is not got wrong again.
- Anything the harness could not measure is reported as **Not Assessed**, never as a pass. This
  binds new drivers too: a scenario that stops looking must say so in its verdict.
