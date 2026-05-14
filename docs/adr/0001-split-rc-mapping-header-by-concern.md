# Split rc_mapping.h by concern

`rc_mapping.h` grew to 1,004 lines by mixing two unrelated concerns: RC binding wire types
with calibration helpers (`RcBindingSource`, `RcBindingConfig`, `RcSwitchState`), and RC
action tokens with classification helpers (`RobotActionId`, `RcTriggerBinding`). Every
module that needed either concern had to drag in the whole file. We split it into
`rc_binding_types.h` (binding types + calibration) and `rc_action_types.h` (action tokens +
trigger bindings), keeping `rc_mapping.h` as a compatibility umbrella that re-exports both.
New code should include the specific header it needs; the umbrella exists for existing
consumers and stays in place until there is a concrete reason to do a full migration sweep.

## Considered options

- **Full consumer migration at split time** — update every `#include "rc_mapping.h"` to the
  correct split header immediately. Rejected: high churn across 20+ files for no behavioral
  gain; the umbrella achieves the same locality benefit with zero risk.
- **Split by module** (channel-mapper header, dispatcher header) instead of by concern —
  rejected: `RcTriggerBinding` bridges both worlds (it has binding fields AND an action
  target), so a module-based split would create a circular dependency or a third header.
  Concern-based split places `RcTriggerBinding` in `rc_action_types.h` which includes
  `rc_binding_types.h`, giving a clean one-way dependency.
