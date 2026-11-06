#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

TEST(Entity3dOriginalArithmeticTest, ParentInverseAndCompositionPreserveSavedReferenceBasis)
{
#if VXMATH_ENABLE_X87_EMULATION
    struct RestoreWord {
        XWORD value = VxGetFPUControlWord();
        ~RestoreWord() { VxSetFPUControlWord(value); }
    } restore;
    VxSetFPUControlWord(static_cast<XWORD>(restore.value & ~0x0300));
    SetProcessorSpecific_FunctionsPtr();
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr;
    ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    const XWORD precisions[] = {0x0000, 0x0200, 0x0300};
    // Original DLL snapshots under PC24, PC53 and PC64, with SSE enabled.
    const float expected[3][3][3] = {
        {{1,0,3.55271368e-15f},{0,1,-4.37123889e-14f},{-3.55271368e-15f,4.37113894e-14f,1}},
        {{1,1.13686838e-13f,0},{1.13686838e-13f,1,-4.37123922e-14f},{-3.55271368e-15f,4.3711386e-14f,1}},
        {{1,1.13686838e-13f,0},{1.13686838e-13f,1,-4.37123922e-14f},{-3.55271368e-15f,4.3711386e-14f,1}}
    };
    for (int precision = 0; precision < 3; ++precision) {
        VxSetFPUControlWord(static_cast<XWORD>((restore.value & ~0x0300) | precisions[precision]));
        auto *parent = static_cast<RCK3dEntity *>(context->CreateObject(CKCID_3DENTITY));
        auto *reference = static_cast<RCK3dEntity *>(context->CreateObject(CKCID_3DENTITY));
        VxMatrix pose = VxMatrix::Identity();
        pose[0].Set(-4.37110828e-08f, 9.99992949e-07f, 0.999992967f);
        pose[1].Set(9.99992949e-07f, 0.999992967f, 9.99992949e-07f);
        pose[2].Set(-0.999992967f, 9.99992949e-07f, -4.37110828e-08f);
        parent->SetWorldMatrix(pose);
        reference->SetWorldMatrix(pose);
        ASSERT_TRUE(reference->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
        const VxMatrix &local = reference->GetLocalMatrix();
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
            EXPECT_EQ(expected[precision][r][c], local[r][c]) << "precision " << precision << " row " << r << " column " << c;
    }
    CKCloseContext(context);
    CKShutdown();
#else
    GTEST_SKIP() << "Requires VXMATH_ENABLE_X87_EMULATION";
#endif
}
