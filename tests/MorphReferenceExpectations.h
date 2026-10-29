#pragma once

#include "MorphReferenceSamples.h"

namespace MorphReference {

// Keep the captured DLL words unchanged. Its positive yaw-wrap defect sends
// vertex 1 from 400 toward 1184 instead of backward toward 16000 (or -384).
// Only these eight interpolated samples differ after correcting that arc;
// pitch, other normals, endpoint copies, and buffer guards stay byte-exact.
static unsigned int ExpectedNormalWord(const Fixture &fixture, int sampleIndex, int bufferIndex) {
    const unsigned int captured = fixture.samples[sampleIndex].normals[bufferIndex];
    if (bufferIndex != 2 || sampleIndex < 2 || sampleIndex > 9)
        return captured;
    if (fixture.words != PackedNormalsWords && fixture.words != PaddedVerticesWords &&
        fixture.words != VertexSubsetWords && fixture.words != NormalsOnlyWords)
        return captured;

    static const short correctedYaw[] = {351, 302, 269, 191, 8, -360, -384, -384};
    return (captured & 0xffffu) |
        (static_cast<unsigned int>(static_cast<unsigned short>(correctedYaw[sampleIndex - 2])) << 16);
}

} // namespace MorphReference
