#ifndef CKBGFXBORDERPALETTE_H
#define CKBGFXBORDERPALETTE_H

#include "VxDefines.h"

// bgfx palette entries cannot be changed while earlier draws in the same
// submission reference them. Keep exact entries, then use the nearest color.
class CKBgfxBorderPalette {
public:
    struct Entry { CKDWORD Index; bool Added; bool Approximated; };
    void Reset() { m_Count = 0; }
    Entry Resolve(CKDWORD argb) {
        CKDWORD nearest = 0, distance = ~0u;
        for (CKDWORD i = 0; i < m_Count; ++i) {
            if (m_Colors[i] == argb) return {i, false, false};
            CKDWORD d = 0;
            for (unsigned shift = 0; shift < 32; shift += 8) {
                const int delta = int((argb >> shift) & 255) - int((m_Colors[i] >> shift) & 255);
                d += delta * delta;
            }
            if (d < distance) { nearest = i; distance = d; }
        }
        if (m_Count == 16) return {nearest, false, true};
        m_Colors[m_Count] = argb;
        return {m_Count++, true, false};
    }
private:
    CKDWORD m_Colors[16] = {};
    CKDWORD m_Count = 0;
};

#endif
