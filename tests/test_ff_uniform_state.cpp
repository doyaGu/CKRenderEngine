#include <stdio.h>

#include "CKFFUniformState.h"
#include "TestTriangleMultiset.h"

namespace {

void AlphaRefUsesLowByteOnly() {
    TestCheck(CKFFNormalizeAlphaRef(0x00000080) > 0.501f &&
                  CKFFNormalizeAlphaRef(0x00000080) < 0.503f,
              "Alpha ref 0x80 must normalize to 128/255");
    TestCheck(CKFFNormalizeAlphaRef(0x12345680) == CKFFNormalizeAlphaRef(0x00000080),
              "Alpha ref must ignore high DWORD bits");
    TestCheck(CKFFNormalizeAlphaRef(0xFFFFFF00) == 0.0f,
              "Alpha ref low byte 0 must normalize to 0");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Alpha ref uses low byte only",
              &AlphaRefUsesLowByteOnly);
    return tests.ExitCode();
}
