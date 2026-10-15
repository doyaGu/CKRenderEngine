#include <gtest/gtest.h>
#include <cmath>
#include <climits>
#include "VxVector.h"

namespace {

const double PackedTau = 6.283185307179586476925286766559;

void CheckDirection(const VxVector &actual, double x, double y, double z, float tolerance = .000002f) {
    EXPECT_NEAR(x, actual.x, tolerance);
    EXPECT_NEAR(y, actual.y, tolerance);
    EXPECT_NEAR(z, actual.z, tolerance);
}

TEST(MorphPackedAngleTest, RadiansConvertToFourteenBitTurnsRatherThanDegrees) {
    const float halfPi = static_cast<float>(PackedTau / 4.0);
    EXPECT_EQ(0, radToAngle(0.0f));
    EXPECT_EQ(4096, radToAngle(halfPi));
    EXPECT_EQ(8192, radToAngle(halfPi * 2.0f));
    EXPECT_EQ(16384, radToAngle(halfPi * 4.0f));
    EXPECT_EQ(-4096, radToAngle(-halfPi));
    EXPECT_EQ(-16384, radToAngle(-halfPi * 4.0f));
}

TEST(MorphPackedAngleTest, RadianQuantizationTruncatesBothSignsTowardZero) {
    const float unit = static_cast<float>(PackedTau / 16384.0);
    EXPECT_EQ(0, radToAngle(unit * .75f));
    EXPECT_EQ(0, radToAngle(-unit * .75f));
    EXPECT_EQ(7, radToAngle(unit * 7.75f));
    EXPECT_EQ(-7, radToAngle(-unit * 7.75f));
    EXPECT_EQ(1234, radToAngle(unit * 1234.5f));
    EXPECT_EQ(-1234, radToAngle(-unit * 1234.5f));
}

TEST(MorphPackedAngleTest, LiteralPackedQuadrantsProduceTheExpectedAxes) {
    const int angles[] = {0, 4096, 8192, 12288, 16384, -4096};
    const float sine[] = {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, -1.0f};
    const float cosine[] = {1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(angles[i]);
        EXPECT_NEAR(sine[i], Tsin(angles[i]), .000001f);
        EXPECT_NEAR(cosine[i], Tcos(angles[i]), .000001f);
    }
}

TEST(MorphPackedAngleTest, TrigUsesOnlyTheLowFourteenBitsForPositiveAndNegativeTurns) {
    const int angles[] = {0, 1, 2048, 4095, 8191, 10000, 16383};
    for (int i = 0; i < 7; ++i) {
        const float sine = Tsin(angles[i]);
        const float cosine = Tcos(angles[i]);
        for (int turns = -17; turns <= 17; ++turns) {
            EXPECT_FLOAT_EQ(sine, Tsin(angles[i] + turns * 16384));
            EXPECT_FLOAT_EQ(cosine, Tcos(angles[i] + turns * 16384));
        }
    }
}

TEST(MorphPackedAngleTest, CompletePackedCircleAgreesWithIndependentRadianTrigonometry) {
    double maxSineError = 0.0, maxCosineError = 0.0;
    for (int angle = 0; angle < 16384; ++angle) {
        const double radians = PackedTau * angle / 16384.0;
        const double sineError = std::fabs(Tsin(angle) - std::sin(radians));
        const double cosineError = std::fabs(Tcos(angle) - std::cos(radians));
        if (sineError > maxSineError)
            maxSineError = sineError;
        if (cosineError > maxCosineError)
            maxCosineError = cosineError;
    }
    EXPECT_LT(maxSineError, .000002);
    EXPECT_LT(maxCosineError, .000002);
}

TEST(MorphPackedAngleTest, LiteralCompressedNormalDecodesHorizontalAndVerticalAxes) {
    const short angles[][2] = {{0, 0}, {0, 4096}, {0, 8192}, {0, -4096}, {-4096, 0}, {4096, 0}};
    const float directions[][3] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int i = 0; i < 6; ++i) {
        VxCompressedVector compressed;
        compressed.xa = angles[i][0];
        compressed.ya = angles[i][1];
        VxVector actual;
        actual = compressed;
        CheckDirection(actual, directions[i][0], directions[i][1], directions[i][2]);
    }
}

TEST(MorphPackedAngleTest, LiteralDiagonalNormalDecodesInTheExpectedHemisphere) {
    VxCompressedVector compressed;
    compressed.xa = -2048;
    compressed.ya = 2048;
    VxVector actual;
    actual = compressed;
    CheckDirection(actual, .5, std::sqrt(.5), .5);
    EXPECT_NEAR(1.0f, actual.SquareMagnitude(), .000004f);
}

TEST(MorphPackedAngleTest, CompressedNormalDecodeMasksBothStoredHighBits) {
    VxCompressedVector first;
    first.xa = 2048;
    first.ya = 1000;
    VxVector expected;
    expected = first;
    for (unsigned int x = 0; x < 4; ++x) {
        for (unsigned int y = 0; y < 4; ++y) {
            VxCompressedVector compressed;
            compressed.xa = static_cast<short>(2048u | (x << 14));
            compressed.ya = static_cast<short>(1000u | (y << 14));
            VxVector actual;
            actual = compressed;
            EXPECT_FLOAT_EQ(expected.x, actual.x);
            EXPECT_FLOAT_EQ(expected.y, actual.y);
            EXPECT_FLOAT_EQ(expected.z, actual.z);
        }
    }
}

TEST(MorphPackedAngleTest, EncodingUnitAxesProducesTheNativePackedAngleUnits) {
    const float directions[][3] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const short expected[][2] = {{0, 0}, {0, 4096}, {0, 8192}, {0, -4096}, {-4096, 0}, {4096, 0}};
    for (int i = 0; i < 6; ++i) {
        VxCompressedVector compressed(directions[i][0], directions[i][1], directions[i][2]);
        EXPECT_EQ(expected[i][0], compressed.xa);
        EXPECT_EQ(expected[i][1], compressed.ya);
        VxVector actual;
        actual = compressed;
        CheckDirection(actual, directions[i][0], directions[i][1], directions[i][2]);
    }
}

TEST(MorphPackedAngleTest, EncodingAndDecodingObliqueUnitNormalsLoseAtMostOneAngularQuantumPerAxis) {
    const double elevations[] = {-.91, -.37, .11, .53, .92};
    const double azimuths[] = {-2.91, -1.67, -.13, .71, 1.89, 3.01};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 6; ++j) {
            const double radius = std::cos(elevations[i]);
            const float x = static_cast<float>(std::sin(azimuths[j]) * radius);
            const float y = static_cast<float>(std::sin(elevations[i]));
            const float z = static_cast<float>(std::cos(azimuths[j]) * radius);
            VxCompressedVector compressed;
            compressed.Set(x, y, z);
            VxVector actual;
            actual = compressed;
            CheckDirection(actual, x, y, z, .0008f);
            EXPECT_NEAR(1.0f, actual.SquareMagnitude(), .000004f);
        }
    }
}

TEST(MorphPackedAngleTest, LegacyNormalEncodingUsesTheSameFourteenBitAngularUnits) {
    VxCompressedVectorOld legacy(1.0f, 0.0f, 0.0f);
    EXPECT_EQ(0, legacy.xa);
    EXPECT_EQ(4096, legacy.ya);
    VxCompressedVector current;
    current = legacy;
    EXPECT_EQ(0, current.xa);
    EXPECT_EQ(4096, current.ya);
    VxVector actual;
    actual = current;
    CheckDirection(actual, 1.0, 0.0, 0.0);
}

TEST(MorphPackedAngleTest, MorphFixedPointNormalSampleDecodesToTheGeometricMidpoint) {
    VxCompressedVector first, last, sampled;
    first.xa = last.xa = 0;
    first.ya = 0;
    last.ya = 4096;
    sampled.Slerp(.5f, first, last);
    EXPECT_EQ(0, sampled.xa);
    EXPECT_EQ(2048, sampled.ya);
    VxVector actual;
    actual = sampled;
    CheckDirection(actual, std::sqrt(.5), 0.0, std::sqrt(.5));
}

} // namespace
