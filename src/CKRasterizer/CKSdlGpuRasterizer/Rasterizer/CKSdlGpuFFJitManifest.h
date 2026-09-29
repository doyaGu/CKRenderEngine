#ifndef CKSDLGPU_FFJIT_MANIFEST_H
#define CKSDLGPU_FFJIT_MANIFEST_H

#include "CKFFFragmentProgram.h"
#include "XArray.h"
#include "XString.h"

#include <SDL3/SDL.h>
#include <cstdint>

// The compiled fragment programs of earlier runs on one device, with the
// pipelines their draws used. A later run creates both on the worker before
// its draws ask for them. The manifest keeps draw state, never code: the
// compiler is fast, and the driver caches what pipeline creation built. A
// prewarmed pipeline has the key of the draw it came from, so a record that
// no draw needs again costs background work only.
struct CKSdlGpuFFJitRecord {
    CKDWORD Lanes[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
    CKDWORD SamplerLayout;
    CKDWORD Variant;
    // Vertex format flags, which name the draw's native vertex layout.
    CKDWORD VertexFormat;
    CKDWORD ColorFormat, DepthFormat, SampleCount;
    CKDWORD StateLo, StateMid, StateHi;
    CKDWORD StencilReadMask, StencilWriteMask, DepthClipEnabled;
};

enum {
    // Bounds the background work one manifest starts.
    CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS = 1024,
};

// Changes with everything that gives the records their meaning, so another
// device, driver, shader format or fixed-function ABI never loads them.
uint64_t CKSdlGpuFFJitManifestIdentity(const char *Driver, const char *Device,
                                       SDL_GPUShaderFormat Format);
// Encodes at most CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS records.
void CKSdlGpuEncodeFFJitManifest(uint64_t Identity,
                                 const XArray<CKSdlGpuFFJitRecord> &Records,
                                 XArray<CKBYTE> &Data);
// Accepts a manifest only as a whole. Another identity or format revision,
// a size or checksum mismatch, or any malformed record leaves Records empty.
bool CKSdlGpuDecodeFFJitManifest(uint64_t Identity, const void *Data, size_t Size,
                                 XArray<CKSdlGpuFFJitRecord> &Records);

// The manifest file of a device, empty when disabled. CKRE_SDL_GPU_FF_JIT_CACHE
// names its directory, or disables it when "0"; it defaults to CKSdlGpuCache
// next to the rasterizer.
XString CKSdlGpuFFJitManifestPath(uint64_t Identity);
bool CKSdlGpuLoadFFJitManifest(const char *Path, uint64_t Identity,
                               XArray<CKSdlGpuFFJitRecord> &Records);
// Replaces the file only with a complete manifest.
bool CKSdlGpuSaveFFJitManifest(const char *Path, uint64_t Identity,
                               const XArray<CKSdlGpuFFJitRecord> &Records);

#endif
