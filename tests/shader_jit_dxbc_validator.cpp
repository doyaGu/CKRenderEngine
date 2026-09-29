#include "CKJitDxbc.h"

#include <cstdio>
#include <cstring>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

// Creates a D3D12 pipeline on WARP for every DXBC container given on the
// command line, with the debug layer when it is installed. The runtime checks
// the digest, the input signature against the vertex outputs and the resource
// declarations against the root signature; WARP compiles the program. Each
// pixel shader is paired with a generated vertex shader writing its input
// signature register by register, and with the root signature of the SDL_gpu
// fragment ABI: textures and samplers in space 2, uniforms at b0 of space 3.
//
// Controls show that each check is active: the first container with a wrong
// digest, every container with resources against a root signature in another
// space, and every container with varyings against vertex outputs one
// register off must be refused.

using Microsoft::WRL::ComPtr;

namespace {

const uint32_t kOpDclResource = 88;
const uint32_t kOpDclConstantBuffer = 89;
const uint32_t kOpDclSampler = 90;

bool ReadContainer(const char *path, XArray<uint32_t> &words) {
    FILE *file = std::fopen(path, "rb");
    if (!file)
        return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bool read = size > 0 && size % 4 == 0;
    if (read) {
        words.Resize((int)(size / 4));
        read = std::fread(words.Begin(), 4, (size_t)words.Size(), file) == (size_t)words.Size();
    }
    std::fclose(file);
    return read;
}

// The body of the chunk with the tag, or null; size receives its dwords.
const uint32_t *FindChunk(const XArray<uint32_t> &words, const char *tag, uint32_t &size) {
    if (words.Size() < 8)
        return nullptr;
    for (uint32_t i = 0; i < words[7] && 8 + (int)i < words.Size(); ++i) {
        const uint32_t offset = words[8 + (int)i] / 4;
        if (offset + 2 <= (uint32_t)words.Size() && std::memcmp(&words[(int)offset], tag, 4) == 0) {
            size = words[(int)offset + 1] / 4;
            return offset + 2 + size <= (uint32_t)words.Size() ? words.Begin() + offset + 2 : nullptr;
        }
    }
    return nullptr;
}

bool DeclaresResources(const XArray<uint32_t> &words) {
    uint32_t size = 0;
    const uint32_t *program = FindChunk(words, "SHEX", size);
    for (uint32_t at = 2; program && at < size;) {
        const uint32_t opcode = program[at] & 0x7ff;
        const uint32_t length = program[at] >> 24 & 0x7f;
        if (opcode == kOpDclResource || opcode == kOpDclConstantBuffer || opcode == kOpDclSampler)
            return true;
        if (length == 0)
            break;
        at += length;
    }
    return false;
}

// Input signature registers: the semantic of each, and the highest one.
struct Varyings {
    const char *Names[32];
    uint32_t Indices[32];
    uint32_t Last;
};

bool ReadVaryings(const XArray<uint32_t> &pixelShader, Varyings &varyings) {
    std::memset(&varyings, 0, sizeof(varyings));
    uint32_t size = 0;
    const uint32_t *signature = FindChunk(pixelShader, "ISGN", size);
    if (!signature || size < 2 || 2 + signature[0] * 6 > size)
        return false;
    for (uint32_t i = 0; i < signature[0]; ++i) {
        const uint32_t *element = signature + 2 + i * 6;
        if (element[4] >= 32 || element[0] >= size * 4)
            return false;
        const char *name = (const char *)signature + element[0];
        if (element[4] == 0 ? std::strcmp(name, "SV_Position") != 0 : element[2] != 0)
            return false; // only the position is a system value, at register 0
        varyings.Names[element[4]] = name;
        varyings.Indices[element[4]] = element[1];
        varyings.Last = element[4] > varyings.Last ? element[4] : varyings.Last;
    }
    return true;
}

// A vertex shader writing SV_Position to o0 and a float4 under the pixel
// shader's semantic to every further register, shifted up by shift.
bool BuildVertexShader(const Varyings &varyings, uint32_t shift, char *source, size_t capacity) {
    int length = std::snprintf(source, capacity, "struct Output {\n    float4 r0 : SV_Position;\n");
    for (uint32_t r = 1; r <= varyings.Last + shift && length > 0 && (size_t)length < capacity; ++r) {
        const uint32_t input = r - shift;
        if (r > shift && varyings.Names[input]) {
            length += std::snprintf(source + length, capacity - length, "    float4 r%u : %s%u;\n", r,
                                    varyings.Names[input], varyings.Indices[input]);
        } else {
            length += std::snprintf(source + length, capacity - length, "    float4 r%u : FILLER%u;\n", r, r);
        }
    }
    if (length > 0 && (size_t)length < capacity) {
        length += std::snprintf(source + length, capacity - length,
                                "};\nOutput main(uint id : SV_VertexID) {\n    Output o = (Output)0;\n"
                                "    o.r0 = float4(id & 1, id >> 1, 0.5, 1);\n    return o;\n}\n");
    }
    return length > 0 && (size_t)length < capacity;
}

class Validator {
public:
    enum Resources { ABI, MISPLACED };

    bool Initialize();
    bool HasDebugLayer() const { return m_InfoQueue != nullptr; }

    // Whether a pipeline with the pixel shader is created without debug
    // layer errors; the result and the messages are printed.
    bool Accepts(const char *name, const XArray<uint32_t> &pixelShader, Resources resources, uint32_t shift);

private:
    bool CreateRootSignature(UINT samplerSpace, UINT uniformSpace, ComPtr<ID3D12RootSignature> &signature);
    bool DrainMessages(); // false after an error message

    ComPtr<ID3D12Device> m_Device;
    ComPtr<ID3D12InfoQueue> m_InfoQueue;
    ComPtr<ID3D12RootSignature> m_RootSignatures[2];
};

bool Validator::Initialize() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
        std::printf("no WARP adapter\n");
        return false;
    }
    const HRESULT hr = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device));
    if (FAILED(hr)) {
        std::printf("D3D12CreateDevice failed: 0x%08lx\n", (unsigned long)hr);
        return false;
    }
    if (debug)
        m_Device.As(&m_InfoQueue);
    return CreateRootSignature(2, 3, m_RootSignatures[ABI]) && CreateRootSignature(0, 0, m_RootSignatures[MISPLACED]);
}

bool Validator::CreateRootSignature(UINT samplerSpace, UINT uniformSpace, ComPtr<ID3D12RootSignature> &signature) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = CKJIT_MAX_SAMPLERS;
    ranges[0].RegisterSpace = samplerSpace;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = CKJIT_MAX_SAMPLERS;
    ranges[1].RegisterSpace = samplerSpace;

    D3D12_ROOT_PARAMETER parameters[3] = {};
    for (int i = 0; i < 2; ++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 0;
    parameters[2].Descriptor.RegisterSpace = uniformSpace;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(m_Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&signature)))) {
        std::printf("the root signature was refused\n");
        return false;
    }
    return true;
}

bool Validator::DrainMessages() {
    if (!m_InfoQueue)
        return true;
    bool clean = true;
    const UINT64 count = m_InfoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T length = 0;
        m_InfoQueue->GetMessage(i, nullptr, &length);
        XArray<char> storage;
        storage.Resize((int)length);
        D3D12_MESSAGE *message = (D3D12_MESSAGE *)storage.Begin();
        if (FAILED(m_InfoQueue->GetMessage(i, message, &length)))
            continue;
        const bool error = message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR;
        clean = clean && !error;
        std::printf("    %s: %s\n", error ? "error" : "message", message->pDescription);
    }
    m_InfoQueue->ClearStoredMessages();
    return clean;
}

bool Validator::Accepts(const char *name, const XArray<uint32_t> &pixelShader, Resources resources, uint32_t shift) {
    Varyings varyings;
    char source[4096];
    if (!ReadVaryings(pixelShader, varyings) || !BuildVertexShader(varyings, shift, source, sizeof(source))) {
        std::printf("%s: no vertex shader matches the input signature\n", name);
        return false;
    }
    ComPtr<ID3DBlob> vertexShader, errors;
    if (FAILED(D3DCompile(source, std::strlen(source), "vertex", nullptr, nullptr, "main", "vs_5_1", 0, 0,
                          &vertexShader, &errors))) {
        std::printf("%s: the vertex shader does not compile:\n%s\n", name,
                    errors ? (const char *)errors->GetBufferPointer() : "");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_RootSignatures[resources].Get();
    desc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    desc.VS.BytecodeLength = vertexShader->GetBufferSize();
    desc.PS.pShaderBytecode = pixelShader.Begin();
    desc.PS.BytecodeLength = (SIZE_T)pixelShader.Size() * 4;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = 0xffffffffu;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    DrainMessages();
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    std::printf("%s: %s (0x%08lx)\n", name, SUCCEEDED(hr) ? "created" : "refused", (unsigned long)hr);
    const bool clean = DrainMessages();
    return SUCCEEDED(hr) && clean;
}

// A control run: the pipeline must be refused.
bool Refuses(Validator &validator, const char *path, const char *control, const XArray<uint32_t> &pixelShader,
             Validator::Resources resources, uint32_t shift) {
    char name[512];
    std::snprintf(name, sizeof(name), "%s [control: %s]", path, control);
    if (!validator.Accepts(name, pixelShader, resources, shift))
        return true;
    std::printf("    the control was accepted\n");
    return false;
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the log of a crashing driver
    if (argc < 2) {
        std::printf("usage: shader_jit_dxbc_validator <container.dxbc>...\n");
        return 2;
    }
    Validator validator;
    if (!validator.Initialize())
        return 1;
    std::printf("debug layer: %s\n", validator.HasDebugLayer() ? "on" : "not installed");

    int failures = 0, digestControls = 0, resourceControls = 0, linkageControls = 0;
    for (int i = 1; i < argc; ++i) {
        XArray<uint32_t> words;
        if (!ReadContainer(argv[i], words)) {
            std::printf("%s: unreadable\n", argv[i]);
            ++failures;
            continue;
        }
        failures += validator.Accepts(argv[i], words, Validator::ABI, 0) ? 0 : 1;

        if (digestControls == 0) {
            XArray<uint32_t> corrupted = words;
            corrupted[1] ^= 1;
            failures += Refuses(validator, argv[i], "wrong digest", corrupted, Validator::ABI, 0) ? 0 : 1;
            ++digestControls;
        }
        if (DeclaresResources(words)) {
            failures += Refuses(validator, argv[i], "resources in space 0", words, Validator::MISPLACED, 0) ? 0 : 1;
            ++resourceControls;
        }
        Varyings varyings;
        if (ReadVaryings(words, varyings) && varyings.Last > 0) {
            failures += Refuses(validator, argv[i], "vertex outputs one register off", words, Validator::ABI, 1) ? 0 : 1;
            ++linkageControls;
        }
    }
    if (digestControls == 0 || resourceControls == 0 || linkageControls == 0) {
        std::printf("the containers do not exercise every control\n");
        ++failures;
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
