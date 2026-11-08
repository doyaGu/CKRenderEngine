#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

TEST(Entity3dParentTransformTest, ParentingComputesLocalMatrixFromInverseParentAndWorldMatrix) {
    VxInitializeMathEnvironment();
    SetProcessorSpecific_FunctionsPtr();
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr;
    ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    auto *parent = static_cast<RCK3dEntity *>(context->CreateObject(CKCID_3DENTITY));
    auto *reference = static_cast<RCK3dEntity *>(context->CreateObject(CKCID_3DENTITY));
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(-4.37110828e-08f, 9.99992949e-07f, 0.999992967f);
    pose[1].Set(9.99992949e-07f, 0.999992967f, 9.99992949e-07f);
    pose[2].Set(-0.999992967f, 9.99992949e-07f, -4.37110828e-08f);
    parent->SetWorldMatrix(pose);
    reference->SetWorldMatrix(pose);
    ASSERT_TRUE(reference->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    VxMatrix inverse, expected;
    Vx3DInverseMatrix(inverse, pose);
    Vx3DMultiplyMatrix(expected, inverse, pose);
    const VxMatrix &local = reference->GetLocalMatrix();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_EQ(expected[row][column], local[row][column]);
    CKCloseContext(context);
    CKShutdown();
}
