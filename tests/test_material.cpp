#include <stdio.h>
#include <stdlib.h>

#include "CKContext.h"
#include "RCKMaterial.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

void DepthWritingAlphaTestCutoutsAreNotAlphaTransparent() {
    CKContext context(nullptr, 0, 0);
    RCKMaterial material(&context, "CutoutMaterial");

    material.EnableAlphaBlend(TRUE);
    material.SetSourceBlend(VXBLEND_SRCALPHA);
    material.SetDestBlend(VXBLEND_INVSRCALPHA);
    material.EnableAlphaTest(TRUE);
    material.EnableZWrite(TRUE);

    TestCheck(!material.IsAlphaTransparent(),
              "Depth-writing alpha-test cutouts must not be sorted with true alpha-blend objects");

    material.EnableZWrite(FALSE);
    TestCheck(material.IsAlphaTransparent(),
              "Non-depth-writing alpha-blend materials must still be sorted as transparent");
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();

    TestFramework tests;
    tests.Run("Depth-writing alpha-test cutouts are not alpha transparent",
              &DepthWritingAlphaTestCutoutsAreNotAlphaTransparent);
    return tests.ExitCode();
}
