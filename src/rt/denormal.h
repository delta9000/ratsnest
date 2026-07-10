#pragma once

#if defined(__SSE2__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif

namespace ms::rt {

// Denormal floats cost 10-100x on x86; feedback tails decay into them.
// FTZ/DAZ live in the per-thread MXCSR, so this must run on the thread that
// does the math -- call it at the top of every audio callback (cheap), not
// once at startup: we don't own the RT thread's creation.
inline void flush_denormals_on_this_thread() {
#if defined(__SSE2__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

}  // namespace ms::rt
