#include "CKSdlGpuFFJitManifest.h"
#include "CKBuiltinShaderIdentity.h"
#include "CKFFDrawTypes.h"
#include "CKFFProgram.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstddef>
#include <cstring>

namespace {
const CKDWORD kMagic = 0x4A464B43u; // "CKFJ"
// Revision of the file layout and of the record fields.
const CKDWORD kVersion = 4;
const size_t kProgramSize = sizeof(CKSdlGpuFFJitProgramRecord);
const size_t kPipelineSize = sizeof(CKSdlGpuFFJitPipelineRecord);
static_assert(kProgramSize == sizeof(CKDWORD) * (CKFF_FRAGMENT_PROGRAM_LANE_COUNT +
                                                 CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT + 1) &&
              kPipelineSize == 8 + 4 * sizeof(CKDWORD),
              "manifest records have no padding");
static_assert(CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS <= 256, "programs are indexed by bytes");
static_assert(SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT < 256 && SDL_GPU_SAMPLECOUNT_8 < 256,
              "texture formats and sample counts are stored as bytes");
const CKDWORD kPipelineFlags =
    CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP | CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD;
// The switches of switch word 0, of every stage for the per-stage ones.
const CKDWORD kSwitchMask = CKFF_NATIVE_FRAGMENT_AFFINE | CKFF_NATIVE_FRAGMENT_LINE |
    CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING | CKFF_NATIVE_FRAGMENT_COMPARISONS |
    0xffu * (CKFF_NATIVE_FRAGMENT_TEXTURE | CKFF_NATIVE_FRAGMENT_BUMP_UNORM |
             CKFF_NATIVE_FRAGMENT_LOD_BIAS);
// The sampling flags of switch words 3 and 4, of every stage.
const CKDWORD kSamplingMask = 0x01010101u *
    (CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_V | CKFF_NATIVE_FRAGMENT_MIRROR_W |
     CKFF_NATIVE_FRAGMENT_BORDER | CKFF_NATIVE_FRAGMENT_GRADIENT | CKFF_NATIVE_FRAGMENT_ANISOTROPY |
     CKFF_NATIVE_FRAGMENT_MIN_MIP);

// Followed by the programs, then the pipelines.
struct Header {
    CKDWORD Magic;
    CKDWORD Version;
    CKDWORD Identity[2];
    CKDWORD ProgramCount;
    CKDWORD PipelineCount;
    // Covers the fields above and the records.
    CKDWORD Checksum;
};

uint64_t HashBytes64(uint64_t hash, const void *data, size_t size)
{
    const CKBYTE *bytes = static_cast<const CKBYTE *>(data);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

CKDWORD Checksum(const Header &header, const void *records, size_t size)
{
    const CKDWORD hash = CKFFHashBytes(&header, (CKDWORD)offsetof(Header, Checksum),
                                       2166136261u);
    return CKFFHashBytes(records, (CKDWORD)size, hash);
}

bool ValidProgram(const CKSdlGpuFFJitProgramRecord &record)
{
    for (CKDWORD lane : record.Lanes) {
        if (lane > CKFFFragmentProgram::LaneMask)
            return false;
    }
    return (record.Switches[0] & ~kSwitchMask) == 0 &&
           (record.Switches[3] & ~kSamplingMask) == 0 &&
           (record.Switches[4] & ~kSamplingMask) == 0 &&
           record.SamplerLayout < CKFF_SAMPLER_LAYOUT_COUNT;
}

// The draw state is checked against the device when it is used.
bool ValidPipeline(const CKSdlGpuFFJitPipelineRecord &record, CKDWORD programs)
{
    return record.Program < programs && record.Variant < CKFF_PROGRAM_VARIANT_COUNT &&
           (record.Flags & ~kPipelineFlags) == 0;
}

// The directory of the module holding this code, with its separator.
XString ModuleDirectory()
{
    XString path;
#ifdef _WIN32
    HMODULE module = NULL;
    wchar_t wide[4096];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&CKSdlGpuFFJitManifestPath, &module))
        return path;
    const DWORD length = GetModuleFileNameW(module, wide, 4096);
    if (length == 0 || length >= 4096)
        return path;
    // SDL takes UTF-8 paths.
    char utf8[4096 * 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL))
        return path;
    path = utf8;
#else
    Dl_info info;
    if (!dladdr((const void *)&CKSdlGpuFFJitManifestPath, &info) || !info.dli_fname)
        return path;
    path = info.dli_fname;
#endif
    const char *file = path.CStr();
    const char *separator = SDL_strrchr(file, '/');
    const char *backslash = SDL_strrchr(file, '\\');
    if (backslash > separator)
        separator = backslash;
    return separator ? XString(file, (int)(separator - file + 1)) : XString();
}
}

uint64_t CKSdlGpuFFJitManifestIdentity(const char *Driver, const char *Device,
                                       SDL_GPUShaderFormat Format)
{
    const CKDWORD revision[] = {kVersion, (CKDWORD)kProgramSize, (CKDWORD)kPipelineSize,
                                (CKDWORD)Format, CKFF_SHADER_ABI_VERSION,
                                CKFF_SHADER_NATIVE_INTERFACE_HASH};
    uint64_t hash = HashBytes64(14695981039346656037ull, revision, sizeof(revision));
    // Terminators keep the strings from running together.
    if (!Driver) Driver = "";
    if (!Device) Device = "";
    hash = HashBytes64(hash, Driver, SDL_strlen(Driver) + 1);
    return HashBytes64(hash, Device, SDL_strlen(Device) + 1);
}

void CKSdlGpuEncodeFFJitManifest(uint64_t Identity, const CKSdlGpuFFJitManifest &Manifest,
                                 XArray<CKBYTE> &Data)
{
    const int programs = Manifest.Programs.Size() < CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS
        ? Manifest.Programs.Size() : CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS;
    int pipelines = 0;
    for (int i = 0; i < Manifest.Pipelines.Size() &&
                    pipelines < CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES; ++i)
        pipelines += Manifest.Pipelines[i].Program < programs ? 1 : 0;
    const size_t size = (size_t)programs * kProgramSize + (size_t)pipelines * kPipelineSize;
    Data.Resize((int)(sizeof(Header) + size));
    CKBYTE *records = Data.Begin() + sizeof(Header);
    if (programs)
        std::memcpy(records, Manifest.Programs.Begin(), (size_t)programs * kProgramSize);
    CKBYTE *pipeline = records + (size_t)programs * kProgramSize;
    for (int i = 0, written = 0; written < pipelines; ++i) {
        if (Manifest.Pipelines[i].Program >= programs)
            continue;
        std::memcpy(pipeline, &Manifest.Pipelines[i], kPipelineSize);
        pipeline += kPipelineSize;
        ++written;
    }
    Header header = {kMagic, kVersion, {(CKDWORD)Identity, (CKDWORD)(Identity >> 32)},
                     (CKDWORD)programs, (CKDWORD)pipelines, 0};
    header.Checksum = Checksum(header, records, size);
    std::memcpy(Data.Begin(), &header, sizeof(header));
}

bool CKSdlGpuDecodeFFJitManifest(uint64_t Identity, const void *Data, size_t Size,
                                 CKSdlGpuFFJitManifest &Manifest)
{
    Manifest.Clear();
    Header header;
    if (!Data || Size < sizeof(header))
        return false;
    std::memcpy(&header, Data, sizeof(header));
    if (header.Magic != kMagic || header.Version != kVersion ||
        header.Identity[0] != (CKDWORD)Identity ||
        header.Identity[1] != (CKDWORD)(Identity >> 32) ||
        header.ProgramCount > CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS ||
        header.PipelineCount > CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES)
        return false;
    const size_t programSize = (size_t)header.ProgramCount * kProgramSize;
    const size_t pipelineSize = (size_t)header.PipelineCount * kPipelineSize;
    const CKBYTE *records = static_cast<const CKBYTE *>(Data) + sizeof(header);
    if (Size != sizeof(header) + programSize + pipelineSize ||
        Checksum(header, records, programSize + pipelineSize) != header.Checksum)
        return false;
    Manifest.Programs.Resize((int)header.ProgramCount);
    Manifest.Pipelines.Resize((int)header.PipelineCount);
    if (programSize)
        std::memcpy(Manifest.Programs.Begin(), records, programSize);
    if (pipelineSize)
        std::memcpy(Manifest.Pipelines.Begin(), records + programSize, pipelineSize);
    bool valid = true;
    for (int i = 0; valid && i < Manifest.Programs.Size(); ++i)
        valid = ValidProgram(Manifest.Programs[i]);
    for (int i = 0; valid && i < Manifest.Pipelines.Size(); ++i)
        valid = ValidPipeline(Manifest.Pipelines[i], header.ProgramCount);
    if (!valid)
        Manifest.Clear();
    return valid;
}

XString CKSdlGpuFFJitManifestPath(uint64_t Identity)
{
    const char *setting = SDL_getenv("CKRE_SDL_GPU_FF_JIT_CACHE");
    XString path;
    if (setting && setting[0]) {
        if (SDL_strcmp(setting, "0") == 0)
            return path;
        path = setting;
        path << "/";
    } else {
        path = ModuleDirectory();
        path << "CKSdlGpuCache/";
    }
    char name[32];
    SDL_snprintf(name, sizeof(name), "ffjit-%016llX.bin", (unsigned long long)Identity);
    path << name;
    return path;
}

bool CKSdlGpuLoadFFJitManifest(const char *Path, uint64_t Identity,
                               CKSdlGpuFFJitManifest &Manifest)
{
    Manifest.Clear();
    SDL_PathInfo info;
    if (!Path || !Path[0] || !SDL_GetPathInfo(Path, &info) ||
        info.type != SDL_PATHTYPE_FILE ||
        info.size > sizeof(Header) + CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS * kProgramSize +
                        CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES * kPipelineSize)
        return false;
    size_t size = 0;
    void *data = SDL_LoadFile(Path, &size);
    if (!data)
        return false;
    const bool loaded = CKSdlGpuDecodeFFJitManifest(Identity, data, size, Manifest);
    SDL_free(data);
    return loaded;
}

bool CKSdlGpuSaveFFJitManifest(const char *Path, uint64_t Identity,
                               const CKSdlGpuFFJitManifest &Manifest)
{
    if (!Path || !Path[0])
        return false;
    const char *separator = SDL_strrchr(Path, '/');
    const char *backslash = SDL_strrchr(Path, '\\');
    if (backslash > separator)
        separator = backslash;
    if (separator && separator != Path) {
        const XString directory(Path, (int)(separator - Path));
        if (!SDL_CreateDirectory(directory.CStr()))
            return false;
    }
    XArray<CKBYTE> data;
    CKSdlGpuEncodeFFJitManifest(Identity, Manifest, data);
    // A per-thread name keeps concurrent writers off each other's file.
    char suffix[32];
    SDL_snprintf(suffix, sizeof(suffix), ".%llx.tmp",
                 (unsigned long long)SDL_GetCurrentThreadID());
    XString temporary(Path);
    temporary << suffix;
    if (!SDL_SaveFile(temporary.CStr(), data.Begin(), (size_t)data.Size()) ||
        !SDL_RenamePath(temporary.CStr(), Path)) {
        SDL_RemovePath(temporary.CStr());
        return false;
    }
    return true;
}
