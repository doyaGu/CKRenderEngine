#ifndef CKSDLGPU_FFJIT_MANIFEST_H
#define CKSDLGPU_FFJIT_MANIFEST_H

#include "CKFFNativeFragmentJit.h"
#include "XArray.h"
#include "XString.h"

#include <SDL3/SDL.h>
#include <cstdint>

// The compiled fragment programs of earlier runs on one device, with the
// pipelines their draws used, precompiled ones too. A later run creates them
// on the worker before its draws ask for them. The manifest keeps draw state, never code: the
// compiler is fast, and the driver caches what pipeline creation built. A
// prewarmed pipeline has the key of the draw it came from, so a record that
// no draw needs again costs background work only.

// The canonical CKFFNativeFragmentKey of a program.
struct CKSdlGpuFFJitProgramRecord {
    CKDWORD Lanes[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
    CKDWORD Switches[CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT];
    CKDWORD SamplerLayout;
};

enum CKSdlGpuFFJitPipelineFlags {
    CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP = 1,
    // The precompiled program, or the one the draw replaced, pads position-T
    // depth.
    CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD = 2,
    // A pipeline of a precompiled program, which draws used before their
    // programs were compiled. Program indexes the precompiled artifacts.
    CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED = 4,
    // Uses the unlit 3D vertex companion for the recorded ordinary or clipped variant.
    CKSDL_GPU_FF_JIT_PIPELINE_UNLIT = 8,
    // Uses the lit 3D vertex companion for the recorded ordinary or clipped variant; exclusive with UNLIT.
    CKSDL_GPU_FF_JIT_PIPELINE_LIT = 16,
};

// A pipeline a program was drawn with.
struct CKSdlGpuFFJitPipelineRecord {
    // Indexes the manifest's programs, or the precompiled artifacts.
    CKBYTE Program;
    CKBYTE Variant;
    CKBYTE Flags;
    CKBYTE ColorFormat, DepthFormat, SampleCount;
    CKBYTE StencilReadMask, StencilWriteMask;
    // Vertex format flags, which name the draw's native vertex layout.
    CKDWORD VertexFormat;
    CKDWORD StateLo, StateMid, StateHi;
};

struct CKSdlGpuFFJitManifest {
    XArray<CKSdlGpuFFJitProgramRecord> Programs;
    XArray<CKSdlGpuFFJitPipelineRecord> Pipelines;

    void Clear()
    {
        Programs.Clear();
        Pipelines.Clear();
    }
};

enum {
    // Bound the background work one manifest starts.
    CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS = 256,
    CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES = 1024,
};

// Changes with everything that gives the records their meaning, so another
// device, driver, shader format or fixed-function ABI never loads them.
uint64_t CKSdlGpuFFJitManifestIdentity(const char *Driver, const char *Device,
                                       SDL_GPUShaderFormat Format);
// Encodes the first programs up to the limit, and up to the limit the first
// pipelines of those and of precompiled programs.
void CKSdlGpuEncodeFFJitManifest(uint64_t Identity, const CKSdlGpuFFJitManifest &Manifest,
                                 XArray<CKBYTE> &Data);
// Accepts a manifest only as a whole. Another identity or format revision,
// a size or checksum mismatch, or any malformed record leaves it empty.
bool CKSdlGpuDecodeFFJitManifest(uint64_t Identity, const void *Data, size_t Size,
                                 CKSdlGpuFFJitManifest &Manifest);

// The manifest file of a device, empty when disabled. CKRE_SDL_GPU_FF_JIT_CACHE
// names its directory, or disables it when "0"; it defaults to CKSdlGpuCache
// next to the rasterizer.
XString CKSdlGpuFFJitManifestPath(uint64_t Identity);
bool CKSdlGpuLoadFFJitManifest(const char *Path, uint64_t Identity,
                               CKSdlGpuFFJitManifest &Manifest);
// Replaces the file only with a complete manifest.
bool CKSdlGpuSaveFFJitManifest(const char *Path, uint64_t Identity,
                               const CKSdlGpuFFJitManifest &Manifest);

#endif
