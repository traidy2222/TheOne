#pragma once
#include <cstdint>
namespace one::render {
// Keep detail through short camera oscillations; pressure reclamation stays immediate.
inline bool residencyTransitionReady(uint32_t current, uint32_t wanted, uint64_t stableFrames, bool pressure,
                                     bool streaming, bool reference = false) {
    if (current == wanted)
        return false;
    if (!streaming || pressure)
        return true;
    return stableFrames >= (reference || wanted < current ? 8u : 32u);
}
} // namespace one::render
