#include <stdlib.h>

#include "CKContext.h"
#include "CKGlobals.h"
#include "RCK2dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class ControlledDraw2dEntity : public RCK2dEntity {
public:
    ControlledDraw2dEntity(CKContext *context, CKERROR result)
        : RCK2dEntity(context, "ControlledDraw2dEntity"),
          m_Result(result), m_DrawCount(0) {}

    CKERROR Draw(CKRenderContext *) override {
        ++m_DrawCount;
        return m_Result;
    }

    CKERROR m_Result;
    int m_DrawCount;
};

void RenderPropagatesDrawFailure() {
    CKContext *context = nullptr;
    TestCheck(CKCreateContext(&context, nullptr, 0, 0) == CK_OK && context,
              "CKCreateContext failed");

    RCKRenderManager *renderManager =
        static_cast<RCKRenderManager *>(context->GetRenderManager());
    if (!renderManager)
        renderManager = new RCKRenderManager(context);
    TestCheck(renderManager != nullptr, "RCKRenderManager creation failed");

    RCKRenderContext *renderContext = new RCKRenderContext(context);
    renderContext->m_Settings.m_Rect.left = 0;
    renderContext->m_Settings.m_Rect.top = 0;
    renderContext->m_Settings.m_Rect.right = 64;
    renderContext->m_Settings.m_Rect.bottom = 64;
    renderContext->SetFullViewport(&renderContext->m_ViewportData, 64, 64);

    {
        ControlledDraw2dEntity entity(context, CKERR_INVALIDOPERATION);
        entity.ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
        VxRect rect(0.0f, 0.0f, 16.0f, 16.0f);
        entity.SetRect(rect);

        TestCheck(entity.Render(renderContext) == CKERR_INVALIDOPERATION,
                  "Render must propagate its Draw failure");
        TestCheck(entity.m_DrawCount == 1,
                  "Visible interface entity must call Draw exactly once");
    }

    delete renderContext;
    CKCloseContext(context);
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();
    TestCheck(CKStartUp() == CK_OK, "CKStartUp failed");
    CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
    CKBuildClassHierarchyTable();

    TestFramework tests;
    tests.Run("Render propagates Draw failure", &RenderPropagatesDrawFailure);

    const int exitCode = tests.ExitCode();
    TestCheck(CKShutdown() == CK_OK, "CKShutdown failed");
    return exitCode;
}
