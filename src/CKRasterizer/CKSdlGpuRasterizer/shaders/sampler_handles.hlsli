// Sampler handles. A handle names one resource of a sampler family by its
// index; each of its methods applies the resource method of the same name to
// the resource the index selects (CKFF_SELECT_*). A sampling function of
// handles is thus instantiated once for a family rather than once for each of
// its resources. The generated CKFF_DISPATCH_* pass handles; the sampler
// layout must precede this file.

// The sampler argument of a handle method. A method samples with the sampler
// of its own texture's slot, which it selects with the texture.
struct CKFFSampler
{
    uint Index;
};

// Applies _operation to the handle of type _type of the resource _index
// selects, with its sampler and slot, if _index is within the family's
// [_first, _end); _result is left unchanged otherwise.
#define CKFF_DISPATCH_HANDLE(_type, _base, _first, _end, _index, _result, _operation) \
    if (uint(_index) - (_first) < uint((_end) - (_first))) { \
        _type ck_resource = { uint(_index) }; \
        CKFFSampler ck_resourceSampler = { uint(_index) }; \
        uint ck_resourceSlot = (_base) + uint(_index); \
        (_result) = _operation(ck_resource); \
    }

struct CKFFTexture2D
{
    uint Index;

    float4 Load(int3 location)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.Load(location)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    void GetDimensions(uint mip, out uint width, out uint height, out uint levels)
    {
        width = 0; height = 0; levels = 0;
#define CKFF_METHOD(_texture, _sampler) _texture.GetDimensions(mip, width, height, levels)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
    }

    float CalculateLevelOfDetailUnclamped(CKFFSampler state, float2 uv)
    {
        float result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.CalculateLevelOfDetailUnclamped(_sampler, uv)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    float4 SampleBias(CKFFSampler state, float2 uv, float bias)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleBias(_sampler, uv, bias)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    float4 SampleGrad(CKFFSampler state, float2 uv, float2 dx, float2 dy)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleGrad(_sampler, uv, dx, dy)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    float4 SampleLevel(CKFFSampler state, float2 uv, float lod)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleLevel(_sampler, uv, lod)
        CKFF_SELECT_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }
};

// A 2D texture a comparison sampler samples.
struct CKFFDepthTexture2D
{
    uint Index;

    float Load(int3 location)
    {
        float result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.Load(location)
        CKFF_SELECT_DEPTH_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    void GetDimensions(uint mip, out uint width, out uint height, out uint levels)
    {
        width = 0; height = 0; levels = 0;
#define CKFF_METHOD(_texture, _sampler) _texture.GetDimensions(mip, width, height, levels)
        CKFF_SELECT_DEPTH_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
    }

    float SampleCmp(CKFFSampler state, float2 uv, float reference)
    {
        float result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleCmp(_sampler, uv, reference)
        CKFF_SELECT_DEPTH_2D(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }
};

struct CKFFTextureCube
{
    uint Index;

    void GetDimensions(uint mip, out uint width, out uint height, out uint levels)
    {
        width = 0; height = 0; levels = 0;
#define CKFF_METHOD(_texture, _sampler) _texture.GetDimensions(mip, width, height, levels)
        CKFF_SELECT_CUBE(Index, CKFF_METHOD)
#undef CKFF_METHOD
    }

    float4 SampleBias(CKFFSampler state, float3 uv, float bias)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleBias(_sampler, uv, bias)
        CKFF_SELECT_CUBE(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }
};

struct CKFFTexture3D
{
    uint Index;

    float4 Load(int4 location)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.Load(location)
        CKFF_SELECT_VOLUME(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    void GetDimensions(uint mip, out uint width, out uint height, out uint depth,
                       out uint levels)
    {
        width = 0; height = 0; depth = 0; levels = 0;
#define CKFF_METHOD(_texture, _sampler) _texture.GetDimensions(mip, width, height, depth, levels)
        CKFF_SELECT_VOLUME(Index, CKFF_METHOD)
#undef CKFF_METHOD
    }

    float CalculateLevelOfDetailUnclamped(CKFFSampler state, float3 uv)
    {
        float result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.CalculateLevelOfDetailUnclamped(_sampler, uv)
        CKFF_SELECT_VOLUME(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    float4 SampleBias(CKFFSampler state, float3 uv, float bias)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleBias(_sampler, uv, bias)
        CKFF_SELECT_VOLUME(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }

    float4 SampleLevel(CKFFSampler state, float3 uv, float lod)
    {
        float4 result = 0.0;
#define CKFF_METHOD(_texture, _sampler) result = _texture.SampleLevel(_sampler, uv, lod)
        CKFF_SELECT_VOLUME(Index, CKFF_METHOD)
#undef CKFF_METHOD
        return result;
    }
};
