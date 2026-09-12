// CKBgfxBackend runtime diagnostics and draw-marker tracing.

#include "CKBgfxBackend.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"
#include "CKBgfxDrawMapTrace.h"
#include "CKRasterizerDrawMarker.h"

#include <string.h>

static const char *CKBgfxTextureKindName(const CKBgfxTextureRecord *Record)
{
    if (!Record)
        return "none";
    if (Record->IsDepth)
        return "depth";
    if (Record->Flags & CKRST_TEXTURE_CUBEMAP)
        return "cube";
    if ((Record->Flags & CKRST_TEXTURE_VOLUMEMAP) && Record->Depth > 1)
        return "volume";
    return "2d";
}

static CKDWORD CKBgfxHashDword(CKDWORD Hash, CKDWORD Value)
{
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
}

static CKDWORD CKBgfxHashDrawState(CKDrawState State)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    hash = CKBgfxHashDword(hash, State.Lo);
    hash = CKBgfxHashDword(hash, State.Mid);
    hash = CKBgfxHashDword(hash, State.Hi);
    return hash;
}

static CKDWORD CKBgfxHashStencil(CKDWORD Ref, CKDWORD ReadMask, CKDWORD WriteMask)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    hash = CKBgfxHashDword(hash, Ref & 0xFF);
    hash = CKBgfxHashDword(hash, ReadMask & 0xFF);
    hash = CKBgfxHashDword(hash, WriteMask & 0xFF);
    return hash;
}

static CKDWORD CKBgfxHashProgram(const CKBgfxProgramRecord *Record)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    if (!Record)
        return 0;
    hash = CKBgfxHashDword(hash, Record->Handle.idx);
    hash = CKBgfxHashDword(hash, Record->VertexShader);
    hash = CKBgfxHashDword(hash, Record->PixelShader);
    return hash;
}

void CKBgfxBackend::ConfigureDebug()
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    m_DebugOverlay = debug.Overlay ? TRUE : FALSE;
    m_DebugLogPresentSync = debug.Log.PresentSync ? TRUE : FALSE;
    m_DebugLogTextureBindings = debug.Log.TextureBindings ? TRUE : FALSE;
    m_DebugLogTextures = debug.Log.Textures ? TRUE : FALSE;
    m_DebugLogUniforms = debug.Log.Uniforms ? TRUE : FALSE;
    SetDebugFlags(m_DebugFlags);

    if (debug.Log.Config ||
        m_DebugBgfxFlags != BGFX_DEBUG_NONE ||
        m_DebugOverlay) {
        CKBgfxLogf("Debug", "configured bgfxFlags=0x%X overlay=%d",
                 m_DebugBgfxFlags, m_DebugOverlay ? 1 : 0);
    }
}
void CKBgfxBackend::DrawDebugOverlay()
{
    if (!m_DebugOverlay)
        return;

    const bgfx::Stats *s = bgfx::getStats();
    bgfx::dbgTextClear(0, false);
    bgfx::dbgTextPrintf(0, 0, 0x4f, "CKBgfx frame=%u renderer=%s size=%ux%u",
                        m_DebugFrameId, m_RendererName,
                        (unsigned)m_Width, (unsigned)m_Height);
    if (s) {
        bgfx::dbgTextPrintf(0, 1, 0x2f, "bgfx gpuFrame=%u draws=%u blits=%u computes=%u views=%u",
                            s->gpuFrameNum, s->numDraw, s->numBlit, s->numCompute, s->numViews);
        bgfx::dbgTextPrintf(0, 2, 0x2f, "transient vb=%u ib=%u waitSubmit=%lld waitRender=%lld",
                            s->transientVbUsed, s->transientIbUsed,
                            (long long)s->waitSubmit, (long long)s->waitRender);
    }
    bgfx::dbgTextPrintf(0, 3, 0x1f, "%s", CKBgfxDebugViewLine0());
    bgfx::dbgTextPrintf(0, 4, 0x1f, "%s", CKBgfxDebugViewLine1());
}
void CKBgfxBackend::SetDebugFlags(CKDWORD Flags)
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    uint32_t bgfxFlags = debug.BgfxFlags;
    m_DebugFlags = Flags;
    m_DrawMapFlags = ((Flags & CKRST_DEBUG_DRAWMAP) != 0) ? Flags : 0;
    m_DrawMapActive = (m_DrawMapFlags & CKRST_DEBUG_DRAWMAP) != 0 ? TRUE : FALSE;
    m_DrawMapSubmitActive =
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUBMITS);
    m_DrawMapMarkerCaptureActive =
        (m_DrawMapSubmitActive ||
         CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
            ? TRUE : FALSE;
    if (!m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    if (Flags & CKRST_DEBUG_WIREFRAME) bgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (Flags & CKRST_DEBUG_IFH)       bgfxFlags |= BGFX_DEBUG_IFH;
    if (Flags & CKRST_DEBUG_STATS)     bgfxFlags |= BGFX_DEBUG_STATS;
    if (Flags & CKRST_DEBUG_TEXT)      bgfxFlags |= BGFX_DEBUG_TEXT;
    if (Flags & CKRST_DEBUG_PROFILER)  bgfxFlags |= BGFX_DEBUG_PROFILER;
    m_DebugBgfxFlags = bgfxFlags;
    if (m_BgfxInitialized)
        bgfx::setDebug(bgfxFlags);
}

void CKBgfxBackend::TraceTextureMap(CKSTRING Event, CKDWORD Texture,
                                              const CKBgfxTextureRecord *Record)
{
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapTextureTrace trace;
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Texture = Texture;
    trace.Bgfx = Record && bgfx::isValid(Record->Handle) ? Record->Handle.idx : 0xffff;
    trace.SamplerBase = Record && bgfx::isValid(Record->SamplerBaseHandle) ? Record->SamplerBaseHandle.idx : 0xffff;
    trace.Kind = (CKSTRING)CKBgfxTextureKindName(Record);
    trace.Width = Record ? Record->Width : 0u;
    trace.Height = Record ? Record->Height : 0u;
    trace.Depth = Record ? Record->Depth : 0u;
    trace.Format = Record ? (int)Record->Format : -1;
    trace.Mips = Record ? Record->MipCount : 0u;
    trace.Flags = Record ? Record->Flags : 0u;
    trace.AutoMip = Record && Record->RequestedAutoMips ? 1u : 0u;
    trace.BaseSampler = Record && Record->SamplerBaseValid ? 1u : 0u;
    trace.IsDepth = Record && Record->IsDepth ? 1u : 0u;
    trace.BitsPerPixel = Record ? Record->BitsPerPixel : 0u;
    CKBgfxDrawMapTraceTexture(&trace);
}
void CKBgfxBackend::TraceProgramMap(CKSTRING Event, CKDWORD Program,
                                              const CKBgfxProgramRecord *Record)
{
    char spec[160];
    const CK_SHADER_PROFILE profile = m_Caps.ShaderProfile;
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapProgramTrace trace;
    spec[0] = '\0';
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Program = Program;
    trace.Bgfx = Record && bgfx::isValid(Record->Handle) ? Record->Handle.idx : 0xffff;
    trace.VertexShader = Record ? Record->VertexShader : 0u;
    trace.PixelShader = Record ? Record->PixelShader : 0u;
    trace.Profile = (CKSTRING)CKBgfxShaderProfileName(profile);
    trace.SpecCount = 0;
    trace.SpecHash = 0;
    trace.Spec = spec;
    CKBgfxDrawMapTraceProgram(&trace);
}
void CKBgfxBackend::TraceBufferMap(CKSTRING Event, CKSTRING Kind,
                                             CKDWORD Buffer,
                                             CKDWORD BgfxHandle,
                                             CKDWORD Layout,
                                             CKDWORD Stride,
                                             CKDWORD Count,
                                             CKDWORD Index32,
                                             CKDWORD Flags)
{
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapBufferTrace trace;
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Kind = Kind;
    trace.Buffer = Buffer;
    trace.Bgfx = BgfxHandle;
    trace.Layout = Layout;
    trace.Stride = Stride;
    trace.Count = Count;
    trace.Index32 = Index32;
    trace.Flags = Flags;
    CKBgfxDrawMapTraceBuffer(&trace);
}
void CKBgfxBackend::RecordInvalidSubmit(CKSTRING Kind, bgfx::ViewId View,
                                                  CKDWORD Program, CKSTRING Reason)
{
    m_DebugInvalidSubmitCount.fetch_add(1, std::memory_order_relaxed);
    if (m_DrawMapSubmitActive) {
        CKBgfxDrawMapTraceSubmitMiss(Reason,
                                      m_DebugFrameId,
                                      0,
                                      (unsigned)View,
                                      Program,
                                      Kind,
                                      NULL);
    }
}
void CKBgfxBackend::RecordTransientAllocMiss(const char *Kind,
                                                       CKDWORD Requested,
                                                       CKDWORD Available)
{
    const CKDWORD miss = m_DebugTransientAllocMissCount.fetch_add(1, std::memory_order_relaxed);
    if (miss < 16 || CKBgfxLogEnabled("Config", false)) {
        CKBgfxLogf("Transient",
                   "%s allocation rejected requested=%u available=%u",
                   Kind ? Kind : "unknown",
                   (unsigned)Requested,
                   (unsigned)Available);
    }
}
void CKBgfxBackend::ResetDebugBindings()
{
    if (!m_DrawMapSubmitActive)
        return;
    memset(m_DebugVertexBindings, 0, sizeof(m_DebugVertexBindings));
    m_DebugVertexBindingMask = 0;
    m_DebugIndexBuffer = 0;
    m_DebugIndexStart = 0;
    m_DebugIndexCount = 0;
    m_DebugIndexHandle = 0;
    memset(m_DebugTextureBindings, 0, sizeof(m_DebugTextureBindings));
    m_DebugTextureBindingMask = 0;
}

void CKBgfxBackend::TraceSubmit(CKDWORD Program, bgfx::ProgramHandle ProgramHandle, CKDWORD Depth, const CKBackendPipelineState &state)
{
    if (!m_DrawMapSubmitActive)
        return;
    const bgfx::ViewId View = m_CurrentView;
    const CKDWORD submitSerial = m_DebugSubmitSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    CKDWORD viewSubmitSerial = 0;
    CKDrawAnnotationParsed parsed;
    CKBOOL parsedLabel = FALSE;
    CKDWORD sourceIndex = CKDRAW_SOURCE_NONE;
    CKBgfxProgramRecord *programRecord = GetProgram(Program);
    CKDWORD stateHash = CKBgfxHashDrawState(m_CachedDrawState);
    CKDWORD stencilHash = CKBgfxHashStencil(state.StencilRef, state.StencilReadMask, state.StencilWriteMask);
    CKDWORD programHash = CKBgfxHashProgram(programRecord);
    uint64_t finalState = m_CachedBgfxState;
    char texFields[CKBACKEND_MAX_TEXTURE_SLOTS * 64];
    CKDWORD texOffset = 0;
    char vbFields[256];
    CKDWORD vbOffset = 0;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);

    texFields[0] = '\0';
    for (CKDWORD i = 0; i < CKBACKEND_MAX_TEXTURE_SLOTS; ++i) {
        if ((m_DebugTextureBindingMask & (1u << i)) == 0)
            continue;
        if (!CKBgfxDrawMapAppendTextureBinding(texFields, sizeof(texFields),
                                               &texOffset,
                                               i,
                                               m_DebugTextureBindings[i].Texture,
                                               m_DebugTextureBindings[i].Uniform,
                                               m_DebugTextureBindings[i].BgfxHandle,
                                               m_DebugTextureBindings[i].SamplerFlags))
            break;
    }
    texFields[sizeof(texFields) - 1] = '\0';

    vbFields[0] = '\0';
    for (CKDWORD i = 0; i < CKRST_MAX_VERTEX_STREAMS; ++i) {
        if ((m_DebugVertexBindingMask & (1u << i)) == 0)
            continue;
        if (!CKBgfxDrawMapAppendVertexBinding(vbFields, sizeof(vbFields),
                                              &vbOffset,
                                              i,
                                              m_DebugVertexBindings[i].Buffer,
                                              m_DebugVertexBindings[i].Start,
                                              m_DebugVertexBindings[i].Count,
                                              m_DebugVertexBindings[i].BgfxHandle,
                                              m_DebugVertexBindings[i].LayoutHandle))
            break;
    }
    vbFields[sizeof(vbFields) - 1] = '\0';

    if (View < CKRST_MAX_PASSES)
        viewSubmitSerial = m_DebugViewSubmitSerial[View].fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_LastMarker[0] == '\0') {
        m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
        CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"no_annotation",
                                      m_DebugFrameId,
                                      submitSerial,
                                      (unsigned)View,
                                      Program,
                                      (CKSTRING)"Submit",
                                      NULL);
    } else {
        parsedLabel = CKDrawAnnotationParseLabel(m_LastMarker, &parsed);
        if (parsedLabel) {
            m_DebugParsedAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            sourceIndex = (CKDWORD)parsed.Source;
            if (sourceIndex < CKDRAW_SOURCE_COUNT)
                m_DebugSourceSubmitCount[sourceIndex].fetch_add(1, std::memory_order_relaxed);
            if (parsed.Source == CKDRAW_SOURCE_RAW_PRIMITIVE)
                m_DebugRawPrimitiveCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"malformed_annotation",
                                          m_DebugFrameId,
                                          submitSerial,
                                          (unsigned)View,
                                          Program,
                                          (CKSTRING)"Submit",
                                          m_LastMarker);
        }
    }

    {
        CKBgfxDrawMapStateTrace stateTrace;
        stateTrace.Frame = m_DebugFrameId;
        stateTrace.Submit = submitSerial;
        stateTrace.StateHash = stateHash;
        stateTrace.Low = m_CachedDrawState.Lo;
        stateTrace.Mid = m_CachedDrawState.Mid;
        stateTrace.High = m_CachedDrawState.Hi;
        stateTrace.BgfxStateLo = (CKDWORD)(finalState & 0xffffffffu);
        stateTrace.BgfxStateHi = (CKDWORD)(finalState >> 32);
        stateTrace.StencilHash = stencilHash;
        stateTrace.StencilRef = state.StencilRef;
        stateTrace.StencilReadMask = state.StencilReadMask;
        stateTrace.StencilWriteMask = state.StencilWriteMask;
        stateTrace.PointSize = m_PointSize;
        CKBgfxDrawMapTraceState(&stateTrace);
    }

    {
        CKBgfxDrawMapSubmitTrace submitTrace;
        submitTrace.Frame = m_DebugFrameId;
        submitTrace.Submit = submitSerial;
        submitTrace.View = (unsigned)View;
        submitTrace.ViewSubmit = viewSubmitSerial;
        submitTrace.Kind = (CKSTRING)"Submit";
        submitTrace.ViewMode = (CKSTRING)"Sequential";
        submitTrace.OrderGeneration = 0;
        submitTrace.OrderSequential = 1u;
        submitTrace.Depth = Depth;
        submitTrace.Program = Program;
        submitTrace.BgfxProgram = bgfx::isValid(ProgramHandle) ? ProgramHandle.idx : 0xffff;
        submitTrace.ProgramHash = programHash;
        submitTrace.ShaderProfile = (CKSTRING)CKBgfxShaderProfileName(m_Caps.ShaderProfile);
        submitTrace.StateHash = stateHash;
        submitTrace.SpecHash = 0;
        submitTrace.BgfxStateLo = (CKDWORD)(finalState & 0xffffffffu);
        submitTrace.BgfxStateHi = (CKDWORD)(finalState >> 32);
        submitTrace.StencilHash = stencilHash;
        submitTrace.Layout = m_CurrentLayout;
        submitTrace.VertexBufferMask = m_DebugVertexBindingMask;
        submitTrace.IndexBuffer = m_DebugIndexBuffer;
        submitTrace.IndexStart = m_DebugIndexStart;
        submitTrace.IndexCount = m_DebugIndexCount;
        submitTrace.IndexBgfxHandle = m_DebugIndexHandle;
        submitTrace.TextureMask = m_DebugTextureBindingMask;
        submitTrace.Discard = (unsigned)BGFX_DISCARD_ALL;
        submitTrace.Extra0 = 0;
        submitTrace.Extra1 = 0;
        submitTrace.Extra2 = 0;
        submitTrace.Parse = parsedLabel ? 1u : 0u;
        submitTrace.Token = parsedLabel ? parsed.Token : 0u;
        submitTrace.Source = parsedLabel ? parsed.SourceName : (CKSTRING)"";
        submitTrace.ObjectId = parsedLabel ? parsed.Object.Id : 0u;
        submitTrace.ObjectName = parsedLabel ? parsed.Object.Name : (CKSTRING)"";
        submitTrace.EntityId = parsedLabel ? parsed.Entity.Id : 0u;
        submitTrace.EntityName = parsedLabel ? parsed.Entity.Name : (CKSTRING)"";
        submitTrace.MeshId = parsedLabel ? parsed.Mesh.Id : 0u;
        submitTrace.MeshName = parsedLabel ? parsed.Mesh.Name : (CKSTRING)"";
        submitTrace.MaterialId = parsedLabel ? parsed.Material.Id : 0u;
        submitTrace.MaterialName = parsedLabel ? parsed.Material.Name : (CKSTRING)"";
        submitTrace.Path = parsedLabel ? parsed.Path : (CKSTRING)"";
        submitTrace.GroupIndex = parsedLabel ? parsed.GroupIndex : -1;
        submitTrace.PrimitiveIndex = parsedLabel ? parsed.PrimitiveIndex : -1;
        submitTrace.PrimitiveType = parsedLabel ? (int)parsed.PrimitiveType : 0;
        submitTrace.IndexTotal = parsedLabel ? parsed.IndexCount : 0u;
        submitTrace.VertexTotal = parsedLabel ? parsed.VertexCount : 0u;
        submitTrace.VertexFields = vbFields;
        submitTrace.TextureFields = texFields;
        submitTrace.Label = m_LastMarker;
        CKBgfxDrawMapTraceSubmit(&submitTrace);
    }
}
