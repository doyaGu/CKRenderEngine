// Synthetic TCB reference samples captured by executing the original Win32 DLL.
// CK2_3D.dll SHA-256: 5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590
// VxMath.dll SHA-256: bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2
// See docs/serialization-alignment.md for capture details and comparison scope.
#pragma once

namespace TCBReference {
struct Sample { float time; float value[4]; };
struct Fixture { const char *name; bool rotation; int keyCount; const float *keys; int sampleCount; const Sample *samples; };

static const float VectorTwoKeysKeys[] = {
    2.0f, 1.0f, -2.0f, 3.0f, 0.25f, 0.7f, -0.6f, 0.0f, 0.0f,
    10.0f, 9.0f, 4.0f, -1.0f, -0.4f, -0.3f, 0.8f, 0.0f, 0.0f,
};
static const Sample VectorTwoKeysSamples[] = {
    {2.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {3.0f, {1.7648437f, -1.42636716f, 2.61757803f, 0.0f}},
    {4.0f, {2.5687499f, -0.823437452f, 2.21562505f, 0.0f}},
    {6.0f, {4.3499999f, 0.512500048f, 1.32500005f, 0.0f}},
    {8.0f, {6.45625019f, 2.0921874f, 0.271874964f, 0.0f}},
    {9.0f, {7.66640615f, 2.99980474f, -0.333203137f, 0.0f}},
    {10.0f, {9.0f, 4.0f, -1.0f, 0.0f}},
};

static const float VectorZeroTCBKeys[] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    4.0f, 6.0f, -3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    10.0f, 10.0f, 8.0f, -4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};
static const Sample VectorZeroTCBSamples[] = {
    {1.0f, {0.0f, 0.0f, 0.0f, 0.0f}},
    {1.75f, {1.8125f, -1.4140625f, 0.890625f, 0.0f}},
    {2.5f, {3.49999976f, -2.5625f, 1.62499988f, 0.0f}},
    {3.25f, {4.9375f, -3.1796875f, 2.046875f, 0.0f}},
    {4.0f, {6.0f, -3.0f, 2.0f, 0.0f}},
    {5.5f, {7.203125f, -1.3671875f, 1.140625f, 0.0f}},
    {7.0f, {7.87499952f, 0.9375f, -0.124999881f, 0.0f}},
    {8.5f, {8.609375f, 4.0234375f, -1.828125f, 0.0f}},
    {10.0f, {10.0f, 8.0f, -4.0f, 0.0f}},
};

static const float VectorUnevenTCBKeys[] = {
    0.0f, 1.0f, -2.0f, 3.0f, 0.25f, 0.5f, -0.25f, 0.4f, 0.2f,
    2.0f, -4.0f, 3.0f, 2.0f, -0.35f, -0.6f, 0.4f, 0.3f, 0.1f,
    9.0f, 6.0f, -1.0f, -2.0f, 0.5f, 0.7f, -0.5f, 0.2f, 0.4f,
    20.0f, -2.0f, 5.0f, 7.0f, -0.2f, -0.3f, 0.6f, 0.3f, 0.2f,
};
static const Sample VectorUnevenTCBSamples[] = {
    {0.0f, {1.0f, -2.0f, 3.0f, 0.0f}},
    {0.5f, {0.129599959f, -1.19007993f, 2.88639998f, 0.0f}},
    {1.0f, {-1.58847415f, 0.455194235f, 2.61558533f, 0.0f}},
    {1.5f, {-3.32629442f, 2.24372649f, 2.21730924f, 0.0f}},
    {2.0f, {-4.0f, 3.0f, 2.0f, 0.0f}},
    {3.75f, {-1.64102554f, 2.17658281f, 0.719600916f, 0.0f}},
    {5.5f, {2.68004107f, 0.270741105f, -1.2896806f, 0.0f}},
    {7.25f, {5.847332f, -1.1027782f, -2.38750315f, 0.0f}},
    {9.0f, {6.0f, -1.0f, -2.0f, 0.0f}},
    {11.75f, {6.01929235f, -0.94159472f, -1.87074947f, 0.0f}},
    {14.5f, {4.590693f, 0.154712558f, -0.212083817f, 0.0f}},
    {17.25f, {0.505031586f, 3.13002968f, 4.20007467f, 0.0f}},
    {20.0f, {-2.0f, 5.0f, 7.0f, 0.0f}},
};

static const float QuaternionTwoKeysKeys[] = {
    2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    10.0f, 0.0f, 0.0f, 0.6f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};
static const Sample QuaternionTwoKeysSamples[] = {
    {2.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {3.0f, {0.0f, 0.0f, 0.0803509206f, 0.996766567f}},
    {4.0f, {0.0f, 0.0f, 0.160182238f, 0.987087429f}},
    {6.0f, {0.0f, 0.0f, 0.316227764f, 0.948683262f}},
    {8.0f, {0.0f, 0.0f, 0.464106679f, 0.885779262f}},
    {9.0f, {0.0f, 0.0f, 0.533779204f, 0.845623851f}},
    {10.0f, {0.0f, 0.0f, 0.600000024f, 0.800000012f}},
};

static const float QuaternionUnevenTCBKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.5f, -0.25f, 0.4f, 0.2f,
    2.0f, 0.6f, 0.0f, 0.0f, 0.8f, -0.35f, -0.6f, 0.4f, 0.3f, 0.1f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f, 0.5f, 0.7f, -0.5f, 0.2f, 0.4f,
    20.0f, 0.0f, 0.0f, -0.6f, 0.8f, -0.2f, -0.3f, 0.6f, 0.3f, 0.2f,
};
static const Sample QuaternionUnevenTCBSamples[] = {
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.5f, {0.0982256532f, -0.00428579934f, 0.0f, 0.994159818f}},
    {1.0f, {0.304449081f, -0.0173970032f, 0.0f, 0.951612592f}},
    {1.5f, {0.517681897f, -0.0129548907f, 0.0f, 0.854988217f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {3.75f, {0.620028913f, 0.0107297152f, -0.0233365521f, 0.784158409f}},
    {5.5f, {0.623291731f, -0.104257494f, -0.0822085962f, 0.770635664f}},
    {7.25f, {-0.0613461584f, 0.861655414f, 0.0634925589f, 0.499755353f}},
    {9.0f, {0.0f, 0.799999952f, 0.0f, 0.599999964f}},
    {11.75f, {-0.0166088194f, 0.794306993f, -0.0248954184f, 0.606779039f}},
    {14.5f, {-0.0276239533f, 0.642623723f, -0.216890365f, 0.734322965f}},
    {17.25f, {-0.00481365109f, 0.222380117f, -0.505136967f, 0.833882809f}},
    {20.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
};

static const float QuaternionOppositeSignsKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.5f, -0.25f, 0.0f, 0.0f,
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f, -0.35f, -0.6f, 0.4f, 0.0f, 0.0f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f, 0.5f, 0.7f, -0.5f, 0.0f, 0.0f,
    20.0f, 0.0f, 0.0f, 0.6f, -0.8f, -0.2f, -0.3f, 0.6f, 0.0f, 0.0f,
};
static const Sample QuaternionOppositeSignsSamples[] = {
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.5f, {0.126505479f, -0.00626556389f, 0.0f, 0.990947306f}},
    {1.0f, {0.282519698f, -0.0164327938f, 0.0f, 0.958338678f}},
    {1.5f, {0.447208166f, -0.0179773644f, 0.0f, 0.893624306f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {3.75f, {-0.622096896f, -0.00395567715f, 0.0259499084f, -0.782500088f}},
    {5.5f, {-0.628091156f, 0.103040963f, 0.0764686167f, -0.767487109f}},
    {7.25f, {0.110712335f, -0.884530663f, -0.0810278207f, -0.445850611f}},
    {9.0f, {0.0f, -0.799999952f, 0.0f, -0.599999964f}},
    {11.75f, {-0.026770249f, 0.761697471f, -0.0801176131f, 0.642402947f}},
    {14.5f, {-0.0261132084f, 0.611430764f, -0.245927155f, 0.75165832f}},
    {17.25f, {-0.0103552332f, 0.341284633f, -0.440927923f, 0.830060363f}},
    {20.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
};

static const Fixture Fixtures[] = {
    {"VectorTwoKeys", false, 2, VectorTwoKeysKeys, 7, VectorTwoKeysSamples},
    {"VectorZeroTCB", false, 3, VectorZeroTCBKeys, 9, VectorZeroTCBSamples},
    {"VectorUnevenTCB", false, 4, VectorUnevenTCBKeys, 13, VectorUnevenTCBSamples},
    {"QuaternionTwoKeys", true, 2, QuaternionTwoKeysKeys, 7, QuaternionTwoKeysSamples},
    {"QuaternionUnevenTCB", true, 4, QuaternionUnevenTCBKeys, 13, QuaternionUnevenTCBSamples},
    {"QuaternionOppositeSigns", true, 4, QuaternionOppositeSignsKeys, 13, QuaternionOppositeSignsSamples},
};
} // namespace TCBReference
