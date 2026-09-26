#ifndef CKBGFXBORDERPALETTE_H
#define CKBGFXBORDERPALETTE_H

#include "VxDefines.h"

// bgfx snapshots one 16-entry palette for the whole submitted frame. Entries
// cannot be recycled while earlier draws still reference them. Callers either
// receive an exact stable entry or must use shader-assisted border sampling.
class CKBgfxBorderPalette {
public:
    struct Entry { CKDWORD Index; bool Added; bool Available; };
    void Reset() { m_Count = 0; }
    Entry Resolve(CKDWORD argb) {
        for (CKDWORD i = 0; i < m_Count; ++i) {
            if (m_Colors[i] == argb) return {i, false, true};
        }
        if (m_Count == 16) return {0, false, false};
        m_Colors[m_Count] = argb;
        return {m_Count++, true, true};
    }
private:
    CKDWORD m_Colors[16] = {};
    CKDWORD m_Count = 0;
};

#endif
