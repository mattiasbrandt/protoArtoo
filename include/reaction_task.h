// =============================================================================
// include/reaction_task.h
//
// ReactionTask: fires the droid's Reactions (ADR 0053, #450).
// =============================================================================
#pragma once

// Task entry point - pin to Core 0, priority 2. Created on every droid, with or
// without a radio: a Reaction's condition is the droid's own state.
void reactionTask(void* pvParameters);
