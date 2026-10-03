// Synthetic linear-controller samples captured from the original Win32 DLL.
// See tests/tcb_reference/capture_linear.py for inputs and binary hashes.
#pragma once

namespace LinearReference {
struct Sample { float time; float value[4]; };
struct Fixture { const char *name; bool rotation; int keyCount; const float *keys; int sampleCount; const Sample *samples; };

static const float VectorSingleKeys[] = {
    2.0f, 1.0f, -2.0f, 3.0f,
};
static const Sample VectorSingleSamples[] = {
    {-1.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {0.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {1.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {2.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {2.00001001f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {3.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {5.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {9.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {9.00000954f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {15.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {20.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {21.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
};

static const float VectorUnevenKeys[] = {
    0.0f, 1.0f, -2.0f, 3.0f,
    2.0f, -4.0f, 5.0f, -6.0f,
    9.0f, 7.0f, -8.0f, 9.0f,
    20.0f, 10.0f, 2.0f, -1.0f,
};
static const Sample VectorUnevenSamples[] = {
    {-1.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {0.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {1.0f, {-1.5f, 1.5f, -1.5f, 0.0f}},
    {2.0f, {-4.0f, 5.0f, -6.0f, 0.0f}},
    {2.00001001f, {-3.99998426f, 4.9999814f, -5.99997854f, 0.0f}},
    {3.0f, {-2.42857122f, 3.14285707f, -3.85714269f, 0.0f}},
    {5.0f, {0.714285851f, -0.571428776f, 0.428571701f, 0.0f}},
    {9.0f, {7.0f, -8.0f, 9.0f, 0.0f}},
    {9.00000954f, {7.00000238f, -7.99999142f, 8.99999142f, 0.0f}},
    {15.0f, {8.63636398f, -2.5454545f, 3.5454545f, 0.0f}},
    {20.0f, {10.0f, 2.0f, -1.0f, 0.0f}},
    {21.0f, {10.0f, 2.0f, -1.0f, 0.0f}},
};

static const float VectorRepeatedTimesKeys[] = {
    0.0f, 1.0f, -2.0f, 3.0f,
    2.0f, -4.0f, 5.0f, -6.0f,
    2.0f, 7.0f, -8.0f, 9.0f,
    20.0f, 10.0f, 2.0f, -1.0f,
};
static const Sample VectorRepeatedTimesSamples[] = {
    {-1.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {0.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {1.0f, {-1.5f, 1.5f, -1.5f, 0.0f}},
    {2.0f, {-4.0f, 5.0f, -6.0f, 0.0f}},
    {2.00001001f, {7.00000191f, -7.99999428f, 8.99999428f, 0.0f}},
    {3.0f, {7.16666651f, -7.44444466f, 8.44444466f, 0.0f}},
    {5.0f, {7.5f, -6.33333302f, 7.33333302f, 0.0f}},
    {9.0f, {8.16666698f, -4.11111116f, 5.11111116f, 0.0f}},
    {9.00000954f, {8.16666794f, -4.11110592f, 5.11110592f, 0.0f}},
    {15.0f, {9.16666603f, -0.777777672f, 1.77777767f, 0.0f}},
    {20.0f, {10.0f, 2.0f, -1.0f, 0.0f}},
    {21.0f, {10.0f, 2.0f, -1.0f, 0.0f}},
};

static const float QuaternionSingleKeys[] = {
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f,
};
static const Sample QuaternionSingleSamples[] = {
    {-1.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {0.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {1.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {2.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {2.00001001f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {3.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {5.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {9.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {9.00000954f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {15.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {20.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {21.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
};

static const float QuaternionOppositeSignsKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f,
    20.0f, 0.0f, 0.0f, 0.6f, -0.8f,
};
static const Sample QuaternionOppositeSignsSamples[] = {
    {-1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {1.0f, {0.316227764f, 0.0f, 0.0f, 0.948683262f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {2.00001001f, {-0.599999487f, -1.3960148e-06f, 0.0f, -0.80000037f}},
    {3.0f, {-0.543009102f, -0.138869748f, 0.0f, -0.828164399f}},
    {5.0f, {-0.392653346f, -0.40372774f, 0.0f, -0.826333642f}},
    {9.0f, {0.0f, -0.799999952f, 0.0f, -0.599999964f}},
    {9.00000954f, {0.0f, 0.799999595f, -6.34552237e-07f, 0.60000056f}},
    {15.0f, {0.0f, 0.426297009f, -0.376938045f, 0.822306812f}},
    {20.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
    {21.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
};

static const float QuaternionRepeatedTimesKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    2.0f, 0.6f, 0.0f, 0.0f, 0.8f,
    2.0f, 0.0f, 0.8f, 0.0f, 0.6f,
    20.0f, 0.0f, 0.0f, -0.6f, 0.8f,
};
static const Sample QuaternionRepeatedTimesSamples[] = {
    {-1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {1.0f, {0.316227764f, 0.0f, 0.0f, 0.948683262f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {2.00001001f, {0.0f, 0.799999714f, -4.07170972e-07f, 0.600000322f}},
    {3.0f, {0.0f, 0.772578299f, -0.0406379327f, 0.63361764f}},
    {5.0f, {0.0f, 0.709651649f, -0.121339925f, 0.694025278f}},
    {9.0f, {0.0f, 0.554764152f, -0.276487887f, 0.784723639f}},
    {9.00000954f, {0.0f, 0.554763734f, -0.276488245f, 0.784723759f}},
    {15.0f, {0.0f, 0.267104536f, -0.477527678f, 0.83703196f}},
    {20.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
    {21.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
};

static const Fixture Fixtures[] = {
    {"VectorSingle", false, 1, VectorSingleKeys, 12, VectorSingleSamples},
    {"VectorUneven", false, 4, VectorUnevenKeys, 12, VectorUnevenSamples},
    {"VectorRepeatedTimes", false, 4, VectorRepeatedTimesKeys, 12, VectorRepeatedTimesSamples},
    {"QuaternionSingle", true, 1, QuaternionSingleKeys, 12, QuaternionSingleSamples},
    {"QuaternionOppositeSigns", true, 4, QuaternionOppositeSignsKeys, 12, QuaternionOppositeSignsSamples},
    {"QuaternionRepeatedTimes", true, 4, QuaternionRepeatedTimesKeys, 12, QuaternionRepeatedTimesSamples},
};
} // namespace LinearReference
