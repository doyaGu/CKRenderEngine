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
const CKDWORD kVersion = 2;
const CKDWORD kRecordDwords = sizeof(CKSdlGpuFFJitRecord) / sizeof(CKDWORD);
static_assert(sizeof(CKSdlGpuFFJitRecord) == kRecordDwords * sizeof(CKDWORD),
              "manifest records are plain DWORDs");
// The switches of switch word 0, of every stage for the per-stage ones.
const CKDWORD kSwitchMask = CKFF_NATIVE_FRAGMENT_AFFINE | CKFF_NATIVE_FRAGMENT_LINE |
    0xffu * (CKFF_NATIVE_FRAGMENT_TEXTURE | CKFF_NATIVE_FRAGMENT_BUMP_UNORM |
             CKFF_NATIVE_FRAGMENT_LOD_BIAS);

struct Header {
    CKDWORD Magic;
    CKDWORD Version;
    CKDWORD Identity[2];
    CKDWORD RecordDwords;
    CKDWORD Count;
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

bool ValidRecord(const CKSdlGpuFFJitRecord &record)
{
    for (CKDWORD lane : record.Lanes) {
        if (lane > CKFFFragmentProgram::LaneMask)
            return false;
    }
    return (record.Switches[0] & ~kSwitchMask) == 0 &&
           record.SamplerLayout < CKFF_SAMPLER_LAYOUT_COUNT &&
           record.Variant < CKFF_PROGRAM_VARIANT_COUNT &&
           record.DepthClipEnabled <= 1;
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
    const CKDWORD revision[] = {kVersion, kRecordDwords, (CKDWORD)Format,
                                CKFF_SHADER_ABI_VERSION,
                                CKFF_SHADER_NATIVE_INTERFACE_HASH};
    uint64_t hash = HashBytes64(14695981039346656037ull, revision, sizeof(revision));
    // Terminators keep the strings from running together.
    if (!Driver) Driver = "";
    if (!Device) Device = "";
    hash = HashBytes64(hash, Driver, SDL_strlen(Driver) + 1);
    return HashBytes64(hash, Device, SDL_strlen(Device) + 1);
}

void CKSdlGpuEncodeFFJitManifest(uint64_t Identity,
                                 const XArray<CKSdlGpuFFJitRecord> &Records,
                                 XArray<CKBYTE> &Data)
{
    const int count = Records.Size() < CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS
        ? Records.Size() : CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS;
    const size_t size = (size_t)count * sizeof(CKSdlGpuFFJitRecord);
    Header header = {kMagic, kVersion, {(CKDWORD)Identity, (CKDWORD)(Identity >> 32)},
                     kRecordDwords, (CKDWORD)count, 0};
    header.Checksum = Checksum(header, Records.Begin(), size);
    Data.Resize((int)(sizeof(header) + size));
    std::memcpy(Data.Begin(), &header, sizeof(header));
    if (size)
        std::memcpy(Data.Begin() + sizeof(header), Records.Begin(), size);
}

bool CKSdlGpuDecodeFFJitManifest(uint64_t Identity, const void *Data, size_t Size,
                                 XArray<CKSdlGpuFFJitRecord> &Records)
{
    Records.Clear();
    Header header;
    if (!Data || Size < sizeof(header))
        return false;
    std::memcpy(&header, Data, sizeof(header));
    if (header.Magic != kMagic || header.Version != kVersion ||
        header.Identity[0] != (CKDWORD)Identity ||
        header.Identity[1] != (CKDWORD)(Identity >> 32) ||
        header.RecordDwords != kRecordDwords ||
        header.Count > CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS)
        return false;
    const size_t size = (size_t)header.Count * sizeof(CKSdlGpuFFJitRecord);
    const CKBYTE *records = static_cast<const CKBYTE *>(Data) + sizeof(header);
    if (Size != sizeof(header) + size || Checksum(header, records, size) != header.Checksum)
        return false;
    Records.Resize((int)header.Count);
    if (size)
        std::memcpy(Records.Begin(), records, size);
    for (int i = 0; i < Records.Size(); ++i) {
        if (!ValidRecord(Records[i])) {
            Records.Clear();
            return false;
        }
    }
    return true;
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
                               XArray<CKSdlGpuFFJitRecord> &Records)
{
    Records.Clear();
    SDL_PathInfo info;
    if (!Path || !Path[0] || !SDL_GetPathInfo(Path, &info) ||
        info.type != SDL_PATHTYPE_FILE ||
        info.size > sizeof(Header) +
            CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS * sizeof(CKSdlGpuFFJitRecord))
        return false;
    size_t size = 0;
    void *data = SDL_LoadFile(Path, &size);
    if (!data)
        return false;
    const bool loaded = CKSdlGpuDecodeFFJitManifest(Identity, data, size, Records);
    SDL_free(data);
    return loaded;
}

bool CKSdlGpuSaveFFJitManifest(const char *Path, uint64_t Identity,
                               const XArray<CKSdlGpuFFJitRecord> &Records)
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
    CKSdlGpuEncodeFFJitManifest(Identity, Records, data);
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
