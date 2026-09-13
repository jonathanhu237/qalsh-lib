#include "src/window_clamp.h"
extern "C" __attribute__((noinline)) void probe(qalsh::detail::HitRange& range, float bound) {
    qalsh::window_detail::ClampToWindow(range, bound);
}
