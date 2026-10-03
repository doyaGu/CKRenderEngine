#ifndef CKSDLGPUFFJITUSAGE_H
#define CKSDLGPUFFJITUSAGE_H

#include <cstdint>

// A bounded frequency with decay after inactivity, measured in canonical
// program requests rather than wall time. Pausing the application doesn't
// discard its working set; a new working set can replace old hot programs.
struct CKSdlGpuFFJitUsage {
    uint64_t Last = 0;
    unsigned Hits = 0;

    unsigned Score(uint64_t now) const {
        const uint64_t age = (now - Last) / 256;
        return age >= 8 ? 0 : Hits >> unsigned(age);
    }
    void Touch(uint64_t now) {
        const unsigned score = Score(now);
        Hits = score < 255 ? score + 1 : 255;
        Last = now;
    }
};

#endif
