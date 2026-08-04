// =============================================================================
// include/psychic_adapter.h
//
// PsychicHttp adapter prototype for issue #72.
// =============================================================================

#pragma once

#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE

/// Initialize and start the PsychicHttp server (issue #72 prototype).
/// Mutually exclusive with webServerInit() -- called instead of it, not
/// alongside it, since both would try to bind port 80.
void initPsychicHttpServer();

#endif  // PA_USE_PSYCHICHTTP_PROTOTYPE
