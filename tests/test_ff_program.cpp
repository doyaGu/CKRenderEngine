#include "CKFFProgramDesc.h"
#include "CKFFProgramLayout.h"
#include "CKError.h"
#include "TestTriangleMultiset.h"

#include <cstring>
#include <new>

namespace {

struct Program {
    CKFFProgramDesc Desc;
    CKShaderDesc Vertex;
    CKShaderDesc Pixel;

    Program()
    {
        Desc.VertexShader = 1;
        Desc.PixelShader = 2;
        Vertex.Stage = CKRST_SHADER_VERTEX;
        Pixel.Stage = CKRST_SHADER_PIXEL;
        SetTarget(CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX12);
    }

    void SetTarget(CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile)
    {
        Vertex.Format = Pixel.Format = format;
        Vertex.Profile = Pixel.Profile = profile;
    }

    CKERROR Validate() const { return CKFFValidateProgram(Desc, Vertex, Pixel); }
};

CKFFUniformBufferBinding Buffer(CK_SHADER_STAGE stage, CKDWORD size, CKDWORD slot = 0)
{
    CKFFUniformBufferBinding result;
    result.Stage = stage;
    result.Size = size;
    result.Slot = slot;
    return result;
}

CKFFUniformBinding Uniform(CK_SHADER_STAGE stage, CKDWORD slot, CKDWORD offset,
                               CKDWORD count = 1, CKFFUniformType type = CKFF_UNIFORM_VEC4)
{
    CKFFUniformBinding result;
    result.Stage = stage;
    result.Slot = slot;
    result.Offset = offset;
    result.Count = count;
    result.Type = type;
    result.Name.Format("u_data%u", (unsigned)slot);
    return result;
}

CKFFSamplerBinding Sampler(CK_SHADER_STAGE stage, CKDWORD logicalSlot, CKDWORD nativeSlot = 0)
{
    CKFFSamplerBinding result;
    result.Stage = stage;
    result.Slot = logicalSlot;
    result.NativeSlot = nativeSlot;
    result.Name.Format("s_image%u", (unsigned)logicalSlot);
    return result;
}

Program OneSampler()
{
    Program p;
    p.Desc.UniformBuffers.PushBack(Buffer(CKRST_SHADER_PIXEL, 64));
    p.Desc.Uniforms.PushBack(Uniform(CKRST_SHADER_PIXEL, 31, 0, 2));
    p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_PIXEL, 23));
    p.Pixel.UniformBufferCount = p.Pixel.SamplerCount = 1;
    return p;
}

Program WithMetadata()
{
    Program p = OneSampler();
    p.Desc.Samplers[0].MetadataBufferSlot = 0;
    p.Desc.Samplers[0].BorderColorOffset = 32;
    p.Desc.Samplers[0].SamplerStateOffset = 48;
    return p;
}

void ExpectInvalid(const Program &p, const char *message)
{
    TestCheck(p.Validate() == CKERR_INVALIDPARAMETER, message);
}

void TestProgramsHaveNoFixedFunctionRoles()
{
    Program procedural;
    TestCheck(procedural.Validate() == CK_OK, "a procedural program needs no vertex attributes or resources");
    Program one = OneSampler();
    CKFFVertexInput input;
    input.Attribute = CKRST_ATTRIB_POSITION;
    input.Location = 6;
    one.Desc.VertexInputs.PushBack(input);
    input.Attribute = CKRST_ATTRIB_INDICES;
    input.Location = 2;
    input.Integer = TRUE;
    one.Desc.VertexInputs.PushBack(input);
    TestCheck(one.Validate() == CK_OK, "one sampler at an arbitrary logical slot is an ordinary program");
    one.Desc.VertexInputs.Clear();
    TestCheck(one.Validate() == CK_OK, "procedural programs can have resources");
    one.Desc.VertexShader = ~0u;
    TestCheck(one.Validate() == CK_OK, "nonzero generation handles are resolved by the backend");

    // Shader model 5.1 DXBC runs on D3D12 as well as D3D11.
    const CK_SHADER_FORMAT formats[] = {CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_FORMAT_DXBC,
        CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_FORMAT_MSL, CKRST_SHADER_FORMAT_METALLIB};
    const CK_SHADER_PROFILE profiles[] = {CKRST_SHADER_PROFILE_DX11, CKRST_SHADER_PROFILE_DX12,
        CKRST_SHADER_PROFILE_SPIRV, CKRST_SHADER_PROFILE_MSL, CKRST_SHADER_PROFILE_MSL};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        one.SetTarget(formats[i], profiles[i]);
        TestCheck(one.Validate() == CK_OK, "native program layouts are not tied to D3D12");
    }
}

void TestFixedFunctionShapedProgram()
{
    Program p;
    for (int stage = 0; stage < 2; ++stage) {
        auto buffer = Buffer(static_cast<CK_SHADER_STAGE>(stage), 768);
        buffer.SharedData = 9;
        p.Desc.UniformBuffers.PushBack(buffer);
        p.Desc.Uniforms.PushBack(Uniform(buffer.Stage, 0, 0, 4, CKFF_UNIFORM_MAT4));
        p.Desc.Uniforms.PushBack(Uniform(buffer.Stage, 1, 256, 8));
    }
    for (CKDWORD i = 0; i < 12; ++i) {
        auto sampler = Sampler(CKRST_SHADER_PIXEL, i, i);
        sampler.Dimension = static_cast<CKFFTextureDimension>(i / 4);
        sampler.MetadataBufferSlot = 0;
        sampler.BorderColorOffset = 384 + i * 32;
        sampler.SamplerStateOffset = sampler.BorderColorOffset + 16;
        p.Desc.Samplers.PushBack(sampler);
    }
    p.Vertex.UniformBufferCount = p.Pixel.UniformBufferCount = 1;
    p.Pixel.SamplerCount = 12;
    TestCheck(p.Validate() == CK_OK, "shared matrices, stage data and twelve texture bindings fit the backend limits");
}

void TestShaderIdentity()
{
    Program p;
    p.Desc.VertexShader = 0;
    ExpectInvalid(p, "zero vertex handle rejected");
    p = Program(); p.Desc.PixelShader = 0;
    ExpectInvalid(p, "zero pixel handle rejected");
    p = Program(); p.Desc.PixelShader = p.Desc.VertexShader;
    ExpectInvalid(p, "one shader cannot supply both stages");
    p = Program(); p.Vertex.Stage = CKRST_SHADER_PIXEL;
    ExpectInvalid(p, "vertex descriptor stage checked");
    p = Program(); p.Pixel.Stage = static_cast<CK_SHADER_STAGE>(3);
    ExpectInvalid(p, "unknown descriptor stage rejected");
    p = Program(); p.Pixel.Format = CKRST_SHADER_FORMAT_SPIRV;
    ExpectInvalid(p, "mixed payload formats rejected");
    p = Program(); p.Pixel.Profile = CKRST_SHADER_PROFILE_DX11;
    ExpectInvalid(p, "mixed profiles rejected");
    p.SetTarget(CKRST_SHADER_FORMAT_UNKNOWN, CKRST_SHADER_PROFILE_UNKNOWN);
    ExpectInvalid(p, "unknown shader target rejected");
    p.SetTarget(CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX11);
    ExpectInvalid(p, "a matching but incompatible format/profile pair is rejected");
    p = Program(); p.Vertex.StorageBufferCount = 1;
    ExpectInvalid(p, "storage buffers need an interface declaration");
    p = Program(); p.Pixel.StorageTextureCount = 1;
    ExpectInvalid(p, "storage textures need an interface declaration");
}

void TestUniformBuffers()
{
    Program p = OneSampler();
    p.Desc.UniformBuffers[0].Size = 0;
    ExpectInvalid(p, "empty uniform buffer rejected");
    p = OneSampler(); p.Desc.UniformBuffers[0].Size = 65;
    ExpectInvalid(p, "buffer size must be 16-byte aligned");
    p = OneSampler(); p.Desc.UniformBuffers[0].Size = CKFF_MAX_CONSTANT_BYTES + 16;
    ExpectInvalid(p, "oversized uniform buffer rejected");
    p = OneSampler(); p.Desc.UniformBuffers[0].Slot = CKFF_UNIFORM_BUFFER_COUNT;
    ExpectInvalid(p, "native buffer slot limit enforced");
    p = OneSampler(); p.Desc.UniformBuffers[0].Stage = static_cast<CK_SHADER_STAGE>(-1);
    ExpectInvalid(p, "invalid buffer stage rejected");
    p = OneSampler(); p.Desc.UniformBuffers.PushBack(p.Desc.UniformBuffers[0]);
    ExpectInvalid(p, "duplicate native buffer slot in one stage rejected");
    p = OneSampler(); p.Desc.UniformBuffers[0].Slot = 1; p.Desc.Uniforms[0].BufferSlot = 1;
    ExpectInvalid(p, "native buffer slots start at zero without holes");
    p = OneSampler(); p.Desc.UniformBuffers[0].Size = CKFF_MAX_CONSTANT_BYTES;
    p.Desc.Uniforms[0].Count = CKFF_MAX_CONSTANT_BYTES / 16;
    TestCheck(p.Validate() == CK_OK, "the exact uniform buffer capacity is accepted");
}

void TestUniformRanges()
{
    Program p = OneSampler();
    p.Desc.Uniforms[0].Slot = CKFF_CONSTANT_SLOT_COUNT;
    ExpectInvalid(p, "logical constant slot limit enforced");
    p = OneSampler(); p.Desc.Uniforms[0].Type = static_cast<CKFFUniformType>(2);
    ExpectInvalid(p, "unknown uniform type rejected");
    p = OneSampler(); p.Desc.Uniforms[0].Count = 0;
    ExpectInvalid(p, "empty uniform rejected");
    p = OneSampler(); p.Desc.Uniforms[0].Count = ~0u;
    ExpectInvalid(p, "uniform byte-size overflow rejected before multiplication");
    p = OneSampler(); p.Desc.Uniforms[0].Type = CKFF_UNIFORM_MAT4;
    p.Desc.Uniforms[0].Count = CKFF_MAX_CONSTANT_BYTES / 64 + 1;
    ExpectInvalid(p, "matrix count uses the matrix byte size");
    p = OneSampler(); p.Desc.Uniforms[0].Offset = 1;
    ExpectInvalid(p, "uniform offset must be 16-byte aligned");
    p = OneSampler(); p.Desc.Uniforms[0].Offset = 48;
    ExpectInvalid(p, "uniform end must fit inside its buffer");
    p = OneSampler(); p.Desc.Uniforms[0].Offset = ~15u;
    ExpectInvalid(p, "uniform range arithmetic cannot wrap");
    p = OneSampler(); p.Desc.Uniforms[0].BufferSlot = 1;
    ExpectInvalid(p, "uniform must reference a declared buffer");
    p = OneSampler(); p.Desc.Uniforms[0].BufferSlot = CKFF_UNIFORM_BUFFER_COUNT;
    ExpectInvalid(p, "uniform buffer slot is bounded");
    p = OneSampler(); p.Desc.Uniforms[0].Stage = static_cast<CK_SHADER_STAGE>(2);
    ExpectInvalid(p, "uniform stage is bounded");
    p = OneSampler(); p.Desc.Uniforms.PushBack(p.Desc.Uniforms[0]);
    ExpectInvalid(p, "duplicate logical uniform in one stage rejected");
    p = OneSampler(); p.Desc.Uniforms.PushBack(Uniform(CKRST_SHADER_PIXEL, 0, 16));
    ExpectInvalid(p, "overlapping uniforms rejected");
    p.Desc.Uniforms[1].Offset = 32;
    TestCheck(p.Validate() == CK_OK, "adjacent ranges are not overlapping");
}

void TestUniformsAcrossStages()
{
    Program p = OneSampler();
    p.Desc.UniformBuffers.PushBack(Buffer(CKRST_SHADER_VERTEX, 64));
    p.Desc.Uniforms.PushBack(Uniform(CKRST_SHADER_VERTEX, 31, 16, 2));
    p.Vertex.UniformBufferCount = 1;
    TestCheck(p.Validate() == CK_OK, "one logical uniform may occupy independent ranges in both stages");
    p.Desc.Uniforms[1].Count = 1;
    ExpectInvalid(p, "cross-stage logical uniform capacity must agree");
    p.Desc.Uniforms[1].Type = CKFF_UNIFORM_MAT4;
    p.Desc.Uniforms[1].Offset = 0;
    ExpectInvalid(p, "cross-stage logical uniform type must agree");
}

void TestSamplers()
{
    Program p = OneSampler();
    p.Desc.Samplers[0].Slot = CKFF_TEXTURE_SLOT_COUNT;
    ExpectInvalid(p, "logical texture slot limit enforced");
    p = OneSampler(); p.Desc.Samplers[0].NativeSlot = CKFF_TEXTURE_SLOT_COUNT;
    ExpectInvalid(p, "native sampler slot limit enforced");
    p = OneSampler(); p.Desc.Samplers[0].NativeSlot = 1;
    ExpectInvalid(p, "native sampler slots must be contiguous from zero");
    p = OneSampler(); p.Desc.Samplers[0].Stage = static_cast<CK_SHADER_STAGE>(2);
    ExpectInvalid(p, "sampler stage is bounded");
    p = OneSampler(); p.Desc.Samplers[0].Dimension = static_cast<CKFFTextureDimension>(-1);
    ExpectInvalid(p, "unknown sampler dimension rejected");
    p = OneSampler(); p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_PIXEL, 22));
    p.Pixel.SamplerCount = 2;
    ExpectInvalid(p, "native sampler slot cannot be reused within one stage");
    p.Desc.Samplers[1] = Sampler(CKRST_SHADER_PIXEL, 23, 1);
    ExpectInvalid(p, "logical sampler slot cannot be reused within one stage");
    p = OneSampler(); p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_VERTEX, 23));
    p.Vertex.SamplerCount = 1;
    TestCheck(p.Validate() == CK_OK, "one logical texture may be sampled from both stages");
}

void TestNativeResourceCounts()
{
    Program p = OneSampler();
    p.Pixel.SamplerCount = 0;
    ExpectInvalid(p, "an undeclared native sampler cannot be added by the program");
    p.Pixel.SamplerCount = 2;
    ExpectInvalid(p, "every shader sampler must be bound");
    p = OneSampler(); p.Vertex.SamplerCount = 1;
    ExpectInvalid(p, "sampler counts are checked by stage");
    p = OneSampler(); p.Pixel.UniformBufferCount = 0;
    ExpectInvalid(p, "extra uniform buffers rejected");
    p.Pixel.UniformBufferCount = 2;
    ExpectInvalid(p, "missing uniform buffers rejected");
    p = OneSampler(); p.Vertex.UniformBufferCount = 1;
    ExpectInvalid(p, "uniform buffer counts are checked by stage");
}

void TestSamplerDimensionsAcrossStages()
{
    Program p;
    p.Vertex.SamplerCount = 1;
    p.Pixel.SamplerCount = 2;
    p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_VERTEX, 23));
    p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_PIXEL, 4));
    p.Desc.Samplers.PushBack(Sampler(CKRST_SHADER_PIXEL, 23, 1));
    p.Desc.Samplers[0].DefaultColor = 0xff112233;
    p.Desc.Samplers[2].DefaultColor = 0xff445566;
    p.Desc.Samplers[2].Name = "s_fragmentImage";
    TestCheck(p.Validate() == CK_OK,
              "one logical texture permits different native slots, names and zero-binding defaults");
    p.Desc.Samplers[2].Dimension = CKFF_TEXTURE_CUBE;
    ExpectInvalid(p, "one logical nonzero binding cannot be both 2D and cube across stages");
    const CKFFSamplerBinding first = p.Desc.Samplers[0];
    p.Desc.Samplers[0] = p.Desc.Samplers[2];
    p.Desc.Samplers[2] = first;
    ExpectInvalid(p, "cross-stage dimension conflicts are rejected in either declaration order");
    p.Desc.Samplers[2].Dimension = CKFF_TEXTURE_CUBE;
    TestCheck(p.Validate() == CK_OK, "a shared logical cube remains valid with independent native slots");
    p.Desc.Samplers[2].Dimension = CKFF_TEXTURE_3D;
    ExpectInvalid(p, "one logical texture cannot be both cube and volume across stages");
}

void TestSamplerMetadata()
{
    Program p = WithMetadata();
    TestCheck(p.Validate() == CK_OK, "border and state metadata may follow explicit uniform ranges");
    p.Desc.Samplers[0].MetadataBufferSlot = 1;
    ExpectInvalid(p, "metadata requires a buffer in its own stage");
    p = WithMetadata(); p.Desc.Samplers[0].MetadataBufferSlot = CKFF_UNIFORM_BUFFER_COUNT;
    ExpectInvalid(p, "metadata buffer slot is bounded");
    p = WithMetadata(); p.Desc.Samplers[0].BorderColorOffset = 33;
    ExpectInvalid(p, "border color metadata is aligned");
    p = WithMetadata(); p.Desc.Samplers[0].SamplerStateOffset = 49;
    ExpectInvalid(p, "sampler state metadata is aligned");
    p = WithMetadata(); p.Desc.Samplers[0].SamplerStateOffset = 64;
    ExpectInvalid(p, "metadata end must fit its buffer");
    p = WithMetadata(); p.Desc.Samplers[0].BorderColorOffset = 16;
    ExpectInvalid(p, "metadata cannot overwrite a uniform");
    p = WithMetadata(); p.Desc.Samplers[0].SamplerStateOffset = 32;
    ExpectInvalid(p, "border and state metadata cannot overlap");
    p = WithMetadata(); p.Desc.Samplers.PushBack(p.Desc.Samplers[0]);
    p.Desc.Samplers[1].Slot = 22; p.Desc.Samplers[1].NativeSlot = 1; p.Pixel.SamplerCount = 2;
    ExpectInvalid(p, "metadata of different samplers cannot overlap");
}

void TestSharedUniformPacking()
{
    Program p = WithMetadata();
    p.Desc.UniformBuffers[0].SharedData = 7;
    auto buffer = Buffer(CKRST_SHADER_VERTEX, 64);
    buffer.SharedData = 7;
    p.Desc.UniformBuffers.PushBack(buffer);
    p.Desc.Uniforms.PushBack(Uniform(CKRST_SHADER_VERTEX, 31, 0, 2));
    p.Vertex.UniformBufferCount = 1;
    TestCheck(p.Validate() == CK_OK, "identical cross-stage writes share one packed snapshot");
    Program bad = p;
    bad.Desc.UniformBuffers[1].Size = 80;
    ExpectInvalid(bad, "shared packed buffers must have equal capacity");
    bad = p; bad.Desc.Uniforms[1].Offset = 16;
    ExpectInvalid(bad, "partially overlapping cross-stage writes cannot share bytes");
    bad = p; bad.Desc.Uniforms[1].Slot = 30;
    ExpectInvalid(bad, "equal ranges from different logical uniforms cannot share bytes");
    bad = p; bad.Desc.Uniforms[1].Slot = 30; bad.Desc.Uniforms[1].Count = 1;
    ExpectInvalid(bad, "unequal capacities cannot alias in a shared buffer");
    bad = p; bad.Desc.Uniforms[1].Slot = 30; bad.Desc.Uniforms[1].Count = 1;
    bad.Desc.Uniforms[1].Type = CKFF_UNIFORM_MAT4;
    ExpectInvalid(bad, "unequal uniform types cannot alias in a shared buffer");
    bad = p; bad.Desc.Uniforms[1].Offset = 32;
    ExpectInvalid(bad, "one stage's uniforms cannot overlap another stage's metadata");
    bad = p; bad.Desc.UniformBuffers[1].SharedData = 8;
    bad.Desc.Uniforms[1].Offset = 32;
    TestCheck(bad.Validate() == CK_OK, "separate packing groups have independent byte ranges");
}

void TestSharedSamplerMetadata()
{
    Program p = WithMetadata();
    p.Desc.UniformBuffers[0].SharedData = 0;
    auto buffer = Buffer(CKRST_SHADER_VERTEX, 64);
    buffer.SharedData = 0;
    p.Desc.UniformBuffers.PushBack(buffer);
    auto sampler = p.Desc.Samplers[0];
    sampler.Stage = CKRST_SHADER_VERTEX;
    p.Desc.Samplers.PushBack(sampler);
    p.Vertex.UniformBufferCount = p.Vertex.SamplerCount = 1;
    TestCheck(p.Validate() == CK_OK, "identical sampler metadata can share bytes across stages");
    Program bad = p; bad.Desc.Samplers[1].Slot = 22;
    ExpectInvalid(bad, "different logical sampler metadata cannot alias");
    bad = p; bad.Desc.Samplers[1].BorderColorOffset = 48;
    bad.Desc.Samplers[1].SamplerStateOffset = 32;
    ExpectInvalid(bad, "border color and sampler state have distinct write identities");
    bad = p; bad.Desc.Samplers[1].BorderColorOffset = 0;
    ExpectInvalid(bad, "shared metadata cannot overwrite another stage's uniform");
}

void TestVertexInputs()
{
    Program p;
    CKFFVertexInput input;
    p.Desc.VertexInputs.PushBack(input);
    TestCheck(p.Validate() == CK_OK, "a position-only program is valid");
    p.Desc.VertexInputs[0].Location = 16;
    ExpectInvalid(p, "native attribute locations are bounded");
    p.Desc.VertexInputs[0] = input;
    p.Desc.VertexInputs[0].Attribute = static_cast<CK_VERTEX_ATTRIB>(CKRST_ATTRIB_COUNT);
    ExpectInvalid(p, "logical vertex attributes are bounded");
    p.Desc.VertexInputs[0] = input; p.Desc.VertexInputs[0].Integer = 2;
    ExpectInvalid(p, "integer input flag must be boolean");
    p.Desc.VertexInputs[0] = input; p.Desc.VertexInputs.PushBack(input);
    p.Desc.VertexInputs[1].Location = 1;
    ExpectInvalid(p, "one logical attribute cannot occupy two locations");
    p.Desc.VertexInputs[1].Attribute = CKRST_ATTRIB_NORMAL;
    p.Desc.VertexInputs[1].Location = 0;
    ExpectInvalid(p, "two attributes cannot occupy one location");
}

void TestSharedSnapshotPacking()
{
    Program p = WithMetadata();
    p.Desc.UniformBuffers[0].SharedData = 0;
    auto shared = Buffer(CKRST_SHADER_VERTEX, 64);
    shared.SharedData = 0;
    p.Desc.UniformBuffers.PushBack(shared);
    p.Desc.UniformBuffers.PushBack(Buffer(CKRST_SHADER_VERTEX, 16, 1));
    p.Desc.Uniforms.PushBack(Uniform(CKRST_SHADER_VERTEX, 31, 0, 2));
    auto independent = Uniform(CKRST_SHADER_VERTEX, 4, 0);
    independent.BufferSlot = 1;
    p.Desc.Uniforms.PushBack(independent);
    p.Vertex.UniformBufferCount = 2;
    TestCheck(p.Validate() == CK_OK, "shared and independent buffers can coexist");

    CKFFProgramLayout layout;
    layout.Init(p.Desc);
    TestCheck(layout.Buffers.Size() == 3 && layout.Data.Size() == 80,
              "two shared 64-byte bindings allocate one snapshot plus the independent 16 bytes");
    TestCheck(layout.BufferOffset(CKRST_SHADER_VERTEX, 0) == layout.BufferOffset(CKRST_SHADER_PIXEL, 0) &&
                  layout.BufferOffset(CKRST_SHADER_VERTEX, 1) == 64,
              "stage bindings reference the shared snapshot's physical offset");
    TestCheck(layout.Buffers[0].Change == 0 && layout.Buffers[1].Change == 0 &&
                  layout.Buffers[2].Change == 0,
              "new physical uniform buffers start unchanged");

    CKFFConstantSet values;
    CKBYTE primary[32], independentBytes[16];
    for (CKDWORD i = 0; i < 32; ++i) primary[i] = static_cast<CKBYTE>(i + 1);
    for (CKDWORD i = 0; i < 16; ++i) independentBytes[i] = static_cast<CKBYTE>(0x80 + i);
    TestCheck(values.Set(31, primary, sizeof(primary)) == CK_OK &&
                  values.Set(4, independentBytes, sizeof(independentBytes)) == CK_OK, "prepare logical data");
    layout.Update(values);
    TestCheck(memcmp(layout.Data.Begin(), primary, 32) == 0 &&
                  memcmp(layout.Data.Begin() + 64, independentBytes, 16) == 0,
              "logical slots populate only their declared ranges");
    const CKQWORD sharedChange = layout.Buffers[0].Change;
    const CKQWORD independentChange = layout.Buffers[2].Change;
    TestCheck(sharedChange != 0 && layout.Buffers[1].Change == sharedChange &&
                  independentChange != 0,
              "logical writes advance every binding of the affected physical buffer");
    for (CKDWORD i = 32; i < 64; ++i)
        TestCheck(layout.Data[i] == 0, "uniform packing leaves metadata bytes untouched");

    const auto previous = layout.Data;
    const CKQWORD change = values[31].Change;
    values.Set(31, primary, sizeof(primary));
    TestCheck(values[31].Change == change, "equal bytes preserve the change marker");
    layout.Update(values);
    TestCheck(layout.Data.Size() == previous.Size() &&
                  memcmp(layout.Data.Begin(), previous.Begin(), previous.Size()) == 0 &&
                  layout.Buffers[0].Change == sharedChange &&
                  layout.Buffers[2].Change == independentChange,
              "unchanged values preserve data and physical buffer change markers");
    primary[0] = 0xee;
    values.Set(31, primary, sizeof(primary));
    TestCheck(values[31].Change != change, "changed bytes advance the change marker");
    layout.Update(values);
    TestCheck(layout.Data[0] == 0xee &&
                  memcmp(layout.Data.Begin() + 1, previous.Begin() + 1, previous.Size() - 1) == 0 &&
                  layout.Buffers[0].Change != sharedChange &&
                  layout.Buffers[1].Change == layout.Buffers[0].Change &&
                  layout.Buffers[2].Change == independentChange,
              "changed data refreshes only its declared slot");

    const auto fullSnapshot = layout.Data;
    const CKBYTE prefix[] = {0x51, 0x52, 0x53};
    values.Set(31, prefix, sizeof(prefix));
    layout.Update(values);
    TestCheck(memcmp(layout.Data.Begin(), prefix, sizeof(prefix)) == 0 &&
                  memcmp(layout.Data.Begin() + 3, fullSnapshot.Begin() + 3, fullSnapshot.Size() - 3) == 0,
              "prefix mutation preserves its producer's tail and other buffers");

    CKFFConstantSet other;
    const CKBYTE first = 0x91;
    other.Set(31, &first, 1);
    layout.Update(other);
    TestCheck(layout.Data[0] == first, "switching producers selects the new source");
    for (CKDWORD i = 1; i < 32; ++i)
        TestCheck(layout.Data[i] == 0, "another producer never inherits a previous uniform tail");
    for (CKDWORD i = 64; i < 80; ++i)
        TestCheck(layout.Data[i] == 0, "omitted slots select zero data");
    CKFFConstantSet sameChange;
    const CKBYTE second = 0x71;
    sameChange.Set(31, &second, 1);
    layout.Update(sameChange);
    TestCheck(layout.Data[0] == second && other[31].Change == sameChange[31].Change,
              "equal change markers from distinct producers cannot alias");
    layout.Update(values);
    TestCheck(memcmp(layout.Data.Begin(), prefix, sizeof(prefix)) == 0 &&
                  memcmp(layout.Data.Begin() + 3, fullSnapshot.Begin() + 3, fullSnapshot.Size() - 3) == 0,
              "switching back restores the original producer's complete values");
}

void TestReusedConstantSetAddress()
{
    CKFFProgramDesc desc;
    desc.UniformBuffers.PushBack(Buffer(CKRST_SHADER_VERTEX, 16));
    desc.Uniforms.PushBack(Uniform(CKRST_SHADER_VERTEX, 3, 0));
    CKFFProgramLayout layout;
    layout.Init(desc);

    alignas(CKFFConstantSet) unsigned char storage[sizeof(CKFFConstantSet)];
    const CKBYTE first[16] = {1};
    auto *source = new (storage) CKFFConstantSet();
    TestCheck(source->Set(3, first, sizeof(first)) == CK_OK, "first constant source is writable");
    layout.Update(*source);
    source->~CKFFConstantSet();

    const CKBYTE second[16] = {2};
    source = new (storage) CKFFConstantSet();
    TestCheck(source->Set(3, second, sizeof(second)) == CK_OK, "replacement constant source is writable");
    layout.Update(*source);
    TestCheck(std::memcmp(layout.Data.Begin(), second, sizeof(second)) == 0,
              "a new source at the same address refreshes equal slot revisions");
    source->~CKFFConstantSet();
}

} // namespace

int main()
{
    TestFramework framework;
    framework.Run("programs have no fixed-function roles", TestProgramsHaveNoFixedFunctionRoles);
    framework.Run("fixed-function-shaped generic program", TestFixedFunctionShapedProgram);
    framework.Run("shader identity and target", TestShaderIdentity);
    framework.Run("uniform buffers", TestUniformBuffers);
    framework.Run("uniform ranges", TestUniformRanges);
    framework.Run("uniforms across stages", TestUniformsAcrossStages);
    framework.Run("sampler bindings", TestSamplers);
    framework.Run("sampler dimensions across stages", TestSamplerDimensionsAcrossStages);
    framework.Run("native resource counts", TestNativeResourceCounts);
    framework.Run("sampler metadata", TestSamplerMetadata);
    framework.Run("shared uniform packing", TestSharedUniformPacking);
    framework.Run("shared sampler metadata", TestSharedSamplerMetadata);
    framework.Run("vertex inputs", TestVertexInputs);
    framework.Run("shared snapshot packing and revisions", TestSharedSnapshotPacking);
    framework.Run("reused constant source address", TestReusedConstantSetAddress);
    return framework.ExitCode();
}
