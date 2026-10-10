#ifndef PHOTOC_HASH_INTERNAL_H
#define PHOTOC_HASH_INTERNAL_H

#include <stdbool.h>

/* Decides whether the x86 SHA-256 block compression may be dispatched, given
   the CPU instruction-set features it requires. process_block_x86 is compiled
   with the "sha,ssse3,sse4.1" target, so SHA support alone is not sufficient:
   a CPU or VM that exposes SHA while masking SSSE3 or SSE4.1 would fault with
   SIGILL. Kept here as a pure test seam so every capability combination can
   be checked regardless of the build or host architecture. */
bool photoc_hash_sha256_x86_dispatch(bool sha, bool ssse3, bool sse4_1);

#endif
