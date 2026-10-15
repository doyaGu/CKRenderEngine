#include "TextureTestObserver.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include <cmath>

#include "RCKKinematicChain.h"

namespace {
// Explicit template instantiation permits obtaining member pointers without
// changing private declarations or MSVC's access-encoded symbol names.
template <class Tag, typename Tag::Type Member> struct PrivateMember {
    friend typename Tag::Type Access(Tag) { return Member; }
};
struct DecomposeTag {
    using Type = void (*)(float **, int, int, float *, float **);
    friend Type Access(DecomposeTag);
};
struct SolveTag {
    using Type = void (*)(float **, float *, float **, int, int, float *, float *);
    friend Type Access(SolveTag);
};
struct RotateTag {
    using Type = CKERROR (RCKKinematicChain::*)(VxVector *, CKBodyPart *);
    friend Type Access(RotateTag);
};
struct CountTag {
    using Type = CKDWORD RCKKinematicChain::*;
    friend Type Access(CountTag);
};
struct DataTag {
    using Type = CKIKChainBodyData *RCKKinematicChain::*;
    friend Type Access(DataTag);
};
template struct PrivateMember<DecomposeTag, &RCKKinematicChain::SVDDecompose>;
template struct PrivateMember<SolveTag, &RCKKinematicChain::SVDSolve>;
template struct PrivateMember<RotateTag, &RCKKinematicChain::IKRotateToward>;
template struct PrivateMember<CountTag, &RCKKinematicChain::m_ChainBodyCount>;
template struct PrivateMember<DataTag, &RCKKinematicChain::m_ChainData>;
CKDWORD &Count(RCKKinematicChain &chain) { return chain.*Access(CountTag()); }
CKIKChainBodyData *&Data(RCKKinematicChain &chain) { return chain.*Access(DataTag()); }
CKERROR Rotate(RCKKinematicChain &chain, VxVector *delta, CKBodyPart *end) {
    return (chain.*Access(RotateTag()))(delta, end);
}
struct Decomposition {
    float Original[9][9] = {}, U[9][9] = {}, V[9][9] = {}, W[9] = {};
    float *Rows[9] = {}, *Right[9] = {};
    int M, N;
    Decomposition(int m, int n) : M(m), N(n) {
        for (int i = 0; i < 9; ++i) { Rows[i] = U[i]; Right[i] = V[i]; }
    }
    void Set(int row, int col, float value) { Original[row][col] = U[row][col] = value; }
    void Run() { Access(DecomposeTag())(Rows, M, N, W, Right); }
    void Check(float tolerance = .0001f) {
        for (int row = 0; row < M; ++row) {
            for (int col = 0; col < N; ++col) {
                double actual = 0;
                for (int k = 0; k < N; ++k) actual += double(U[row][k]) * W[k] * V[col][k];
                EXPECT_TRUE(std::isfinite(actual));
                EXPECT_NEAR(Original[row][col], actual, tolerance) << row << ',' << col;
            }
        }
        for (int i = 0; i < N; ++i) {
            EXPECT_GE(W[i], 0);
            for (int j = 0; j < N; ++j) {
                double dot = 0;
                for (int k = 0; k < N; ++k) dot += double(V[k][i]) * V[k][j];
                EXPECT_NEAR(i == j ? 1 : 0, dot, .0002);
            }
        }
    }
    void Solve(float *b, float *x) {
        float maximum = 0;
        for (int i = 0; i < N; ++i) if (W[i] > maximum) maximum = W[i];
        for (int i = 0; i < N; ++i) if (W[i] < maximum * .00001f) W[i] = 0;
        Access(SolveTag())(Rows, W, Right, M, N, b, x);
    }
};

TEST(KinematicChainMathTest, NonsymmetricSquareReconstructsAndSolvesKnownVector) {
    Decomposition d(3, 3);
    const float matrix[3][3] = {{3, -2, 1}, {1, 4, -2}, {2, 1, 5}};
    const float expected[3] = {2, -1, 3};
    float b[3] = {}, x[3] = {};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        d.Set(i, j, matrix[i][j]); b[i] += matrix[i][j] * expected[j];
    }
    d.Run(); d.Check(); d.Solve(b, x);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(expected[i], x[i], .0001);
}

TEST(KinematicChainMathTest, WideThreeBySixReconstructsAndReturnsMinimumNormSolution) {
    // [I 2I] has minimum-norm x=[b/5, 2b/5], independently of an SVD implementation.
    Decomposition d(3, 6);
    for (int i = 0; i < 3; ++i) { d.Set(i, i, 1); d.Set(i, i + 3, 2); }
    float b[3] = {3, -5, 7}, x[6] = {};
    d.Run(); d.Check(); d.Solve(b, x);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(b[i] / 5, x[i], .0001);
        EXPECT_NEAR(2 * b[i] / 5, x[i + 3], .0001);
    }
}

TEST(KinematicChainMathTest, WideThreeByNineReconstructsWithThreeIndependentRepeatedAxes) {
    Decomposition d(3, 9);
    for (int i = 0; i < 3; ++i) {
        d.Set(i, i, -2); d.Set(i, i + 3, 1); d.Set(i, i + 6, 2);
    }
    float b[3] = {9, -18, 27}, x[9] = {};
    d.Run(); d.Check(); d.Solve(b, x);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(-2 * b[i] / 9, x[i], .0001);
        EXPECT_NEAR(b[i] / 9, x[i + 3], .0001);
        EXPECT_NEAR(2 * b[i] / 9, x[i + 6], .0001);
    }
}

TEST(KinematicChainMathTest, RankDeficientLeastSquaresProjectsOntoReachableSubspace) {
    Decomposition d(3, 3);
    d.Set(0, 0, 1); d.Set(0, 1, 2);
    d.Set(1, 0, 2); d.Set(1, 1, 4);
    float b[3] = {3, 6, 9}, x[3] = {};
    d.Run(); d.Check(); d.Solve(b, x);
    EXPECT_NEAR(.6, x[0], .0001); EXPECT_NEAR(1.2, x[1], .0001); EXPECT_NEAR(0, x[2], .0001);
    // The residual (0,0,9) is orthogonal to every original column.
    for (int col = 0; col < 3; ++col) {
        double dot = 0;
        for (int row = 0; row < 3; ++row) {
            double predicted = 0;
            for (int k = 0; k < 3; ++k) predicted += d.Original[row][k] * x[k];
            dot += d.Original[row][col] * (b[row] - predicted);
        }
        EXPECT_NEAR(0, dot, .0001);
    }
}

TEST(KinematicChainMathTest, ZeroJacobianProducesFiniteZeroMinimumNormMotion) {
    Decomposition d(3, 6); float b[3] = {2, 3, -4}, x[6] = {};
    d.Run(); d.Check(); d.Solve(b, x);
    for (int i = 0; i < 6; ++i) EXPECT_FLOAT_EQ(0, x[i]);
}

TEST(KinematicChainMathTest, TallLeastSquaresReconstructsAndLeavesOrthogonalResidual) {
    Decomposition d(4, 2);
    d.Set(0, 0, 1); d.Set(1, 0, 1); d.Set(2, 1, 2); d.Set(3, 1, 2);
    float b[4] = {1, 3, 6, 10}, x[2] = {};
    d.Run(); d.Check(); d.Solve(b, x);
    EXPECT_NEAR(2, x[0], .0001); EXPECT_NEAR(4, x[1], .0001);
}



class KinematicChainJointTest : public TextureTestWorld {
protected:
    RCKBodyPart *Root = nullptr, *End = nullptr;
    RCKKinematicChain *Chain = nullptr;
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKKinematicChain, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Root = reinterpret_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        End = reinterpret_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        Chain = reinterpret_cast<RCKKinematicChain *>(Context->CreateObject(CKCID_KINEMATICCHAIN));
        ASSERT_NE(nullptr, Root); ASSERT_NE(nullptr, End); ASSERT_NE(nullptr, Chain);
        ASSERT_TRUE(End->SetParent(reinterpret_cast<CK3dEntity *>(Root), FALSE));
        VxMatrix local = VxMatrix::Identity(); local[3].x = 1; End->SetLocalMatrix(local);
        Chain->SetStartEffector(reinterpret_cast<CKBodyPart *>(Root));
        Chain->SetEndEffector(reinterpret_cast<CKBodyPart *>(End));
        Count(*Chain) = 2;
        Data(*Chain) = new CKIKChainBodyData[2]();
        Data(*Chain)[0].m_BodyPart = reinterpret_cast<CKBodyPart *>(Root);
        Data(*Chain)[1].m_BodyPart = reinterpret_cast<CKBodyPart *>(End);
    }
    void Axes(CKDWORD flags) { Data(*Chain)[0].m_RotationJoint.m_Flags = flags; }
    static void MatrixEqual(const VxMatrix &a, const VxMatrix &b) {
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
            EXPECT_NEAR(a[row][col], b[row][col], .00001);
    }
};

TEST_F(KinematicChainJointTest, RankOneJacobianMovesActualChildAroundEnabledZAxis) {
    Axes(CK_IKJOINT_ACTIVE_Z); VxVector delta(0, .1f, 0);
    ASSERT_EQ(CK_OK, Rotate(*Chain, &delta, nullptr));
    VxVector actual; End->GetPosition(&actual);
    EXPECT_NEAR(std::cos(.1), actual.x, .0001); EXPECT_NEAR(std::sin(.1), actual.y, .0001);
    EXPECT_NEAR(0, actual.z, .0001);
}

TEST_F(KinematicChainJointTest, RotatedWorldBasisUsesRowsForActualLocalAxes) {
    // Root's local Z points down world Y. A positive local Z twist moves the tip toward +world Z.
    VxMatrix orientation = VxMatrix::Identity();
    orientation[1].Set(0, 0, 1, 0); orientation[2].Set(0, -1, 0, 0);
    Root->SetLocalMatrix(orientation);
    Axes(CK_IKJOINT_ACTIVE_Z); VxVector delta(0, 0, .1f);
    ASSERT_EQ(CK_OK, Rotate(*Chain, &delta, nullptr));
    VxVector actual; End->GetPosition(&actual);
    EXPECT_NEAR(std::cos(.1), actual.x, .0001); EXPECT_NEAR(0, actual.y, .0001);
    EXPECT_NEAR(std::sin(.1), actual.z, .0001);
}





TEST_F(KinematicChainJointTest, NoEnabledAxesReturnsFailureWithoutChangingHierarchy) {
    Axes(0); const VxMatrix before = Root->GetLocalMatrix(); VxVector delta(0, .4f, 0);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Rotate(*Chain, &delta, nullptr));
    MatrixEqual(before, Root->GetLocalMatrix());
}
}
