// CKBgfxRasterizerContext frame submission and draw-marker diagnostics.

#include "CKBgfxRasterizerContext.h"
#include "CKBgfxInternal.h"
#include "CKRasterizerDrawMarker.h"

#include <stdint.h>

CKERROR CKBgfxRasterizerContext::Submit(CKPresentSync Mode, CKBOOL PresentWindow,
                                        CKDWORD *FrameNumber)
{
    (void)PresentWindow;
    if (FrameNumber)
        *FrameNumber = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKERROR fatalError = GetDeviceStatus();
    if (fatalError != CK_OK)
        return fatalError;
    if (Mode != CKRST_PRESENT_IMMEDIATE && Mode != CKRST_PRESENT_VSYNC &&
        Mode != CKRST_PRESENT_UNCHANGED)
        return CKERR_INVALIDPARAMETER;
    if (m_LastSubmitId == (CKQWORD)-1)
        return CKERR_INVALIDOPERATION;

    // A present-sync change needs bgfx::reset. Applying it to the frame being
    // submitted loses that frame's rendering (the reset recreates the swap
    // chain and the frame buffers while the frame renders), so the frame is
    // rendered with the old sync mode and the reset gets an empty frame of its
    // own right after it.
    const CKBOOL updatePresentSync = Mode != CKRST_PRESENT_UNCHANGED;
    const CKBOOL vsync = Mode == CKRST_PRESENT_VSYNC;
    const CKBOOL resetAfterFrame = updatePresentSync && vsync != m_VSync;

    static int s_PresentSyncLogCount = 0;
    if (m_DebugLogPresentSync && s_PresentSyncLogCount < 64) {
        CKBgfxLogf("PresentSync",
                 "frame=%u mode=%d currentVSync=%d resetFlags=0x%X",
                 m_DebugFrameId, (int)Mode,
                 m_VSync ? 1 : 0, m_ResetFlags);
        ++s_PresentSyncLogCount;
    }

    DrawDebugOverlay();

    if (m_DrawMapActive &&
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME)) {
        CKBgfxLogf("FrameMap",
                   "End frame=%u passes=%u submits=%u parsed=%u missingAnnotations=%u rawPrimitive=%u markerOverwrite=%u markerStale=%u invalidSubmit=%u",
                   m_DebugFrameId,
                   m_BgfxFramePasses,
                   m_DebugSubmitSerial.load(std::memory_order_relaxed),
                   m_DebugParsedAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugMissingAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugRawPrimitiveCount.load(std::memory_order_relaxed),
                   m_DebugMarkerOverwriteCount.load(std::memory_order_relaxed),
                   m_DebugMarkerStaleCount.load(std::memory_order_relaxed),
                   m_DebugInvalidSubmitCount.load(std::memory_order_relaxed));
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUMMARY)) {
            CKBgfxLogf("FrameMap",
                       "Sources frame=%u Mesh=%u 2D=%u Sprite=%u Callback=%u RawPrimitive=%u",
                       m_DebugFrameId,
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_MESH].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_2D_ENTITY].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_SPRITE].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_CALLBACK].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_RAW_PRIMITIVE].load(std::memory_order_relaxed));
            for (int i = 0; i < CKRST_MAX_PASSES; ++i) {
                CKDWORD viewSubmits = m_DebugViewSubmitSerial[i].load(std::memory_order_relaxed);
                if (viewSubmits != 0) {
                    CKBgfxLogf("ViewMap",
                               "frame=%u view=%u submits=%u name=%s",
                               m_DebugFrameId,
                               (unsigned)i,
                               viewSubmits,
                               m_DebugViewName[i]);
                }
            }
        }
    }
    if (m_DrawMapMarkerCaptureActive && m_LastMarker[0] != '\0') {
        m_DebugMarkerStaleCount.fetch_add(1, std::memory_order_relaxed);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
            CKBgfxLogf("MarkerStale", "frame=%u reason=present label=\"%s\"", m_DebugFrameId, m_LastMarker);
        m_LastMarker[0] = '\0';
    }

    CKDWORD submittedFrame = bgfx::frame();
    m_BorderPalette.Reset();
    if (resetAfterFrame)
    {
        m_VSync = vsync;
        m_ResetFlags = CKBgfxBuildResetFlags(vsync, 0);
        bgfx::reset((uint32_t)m_DrawableWidth, (uint32_t)m_DrawableHeight, m_ResetFlags);
        submittedFrame = bgfx::frame();
    }
    for (int i = 0; i < m_BgfxReadbacks.Size();) {
        std::shared_ptr<NativeReadback> &ticket = m_BgfxReadbacks[i];
        if ((int32_t)(submittedFrame - ticket->AvailableFrame) >= 0) {
            ticket->Complete = TRUE;
            bgfx::destroy(ticket->Snapshot);
            ticket->Snapshot = BGFX_INVALID_HANDLE;
            m_BgfxReadbacks.RemoveAt(i);
        } else {
            ++i;
        }
    }
    if (FrameNumber)
        *FrameNumber = submittedFrame;

    ++m_LastSubmitId;
    m_SubmittedFrames.PushBack(
        CKBgfxSubmittedFrame(submittedFrame, m_LastSubmitId));
    while (m_SubmittedFrames.Size() != 0 &&
           (int32_t)(submittedFrame - m_SubmittedFrames[0].FrameNumber) >
               (int32_t)CKBGFX_MAX_FRAME_LATENCY) {
        m_CompletedSubmitId = m_SubmittedFrames[0].SubmitId;
        m_SubmittedFrames.RemoveAt(0);
    }

    // Views the previous frame used and this one did not keep their
    // configuration in bgfx; reset them so a later frame starts clean.
    for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view)
        bgfx::resetView((bgfx::ViewId)view);
    {
        VxMutexLock lock(m_ResourceStateMutex);
        for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view) {
            m_ViewFrameBuffer[view] = 0;
            m_ViewRect[view].left = m_ViewRect[view].top = m_ViewRect[view].right = m_ViewRect[view].bottom = 0;
            m_ViewClearFlags[view] = 0;
        }
        for (int i = 0; i < CKRST_MAX_PASSES; ++i)
            m_ViewClearRecorded[i] = FALSE;
    }
    m_LastFrameViewCount = m_NextView;
    m_NextView = 0;
    m_PassOpen = FALSE;
    m_FrameInProgress = FALSE;
    m_TransientVBCount = 0;
    m_TransientIBCount = 0;
    m_DrawErrorLogCount = 0;

    m_BgfxFramePasses = m_FrameDraws = m_FrameBlits = m_BgfxFrameTextureUploads = m_BgfxFrameBufferUploads = 0;
    if (const bgfx::Stats *s = bgfx::getStats()) {
        m_BgfxCpuTimeFrame = s->cpuTimeFrame;
        m_BgfxCpuTimerFreq = s->cpuTimerFreq;
        m_BgfxGpuTimeFrame = s->gpuTimeEnd - s->gpuTimeBegin;
        m_BgfxGpuTimerFreq = s->gpuTimerFreq;
        m_BgfxGpuMemoryMax = (CKDWORD)(s->gpuMemoryMax >> 10);
        m_BgfxGpuMemoryUsed = (CKDWORD)(s->gpuMemoryUsed >> 10);
    }

    ++m_DebugFrameId;
    if (m_DrawMapActive) {
        m_DebugSubmitSerial.store(0, std::memory_order_relaxed);
        m_DebugMissingAnnotationCount.store(0, std::memory_order_relaxed);
        m_DebugMarkerOverwriteCount.store(0, std::memory_order_relaxed);
        m_DebugMarkerStaleCount.store(0, std::memory_order_relaxed);
        m_DebugInvalidSubmitCount.store(0, std::memory_order_relaxed);
        m_DebugParsedAnnotationCount.store(0, std::memory_order_relaxed);
        m_DebugRawPrimitiveCount.store(0, std::memory_order_relaxed);
        for (int i = 0; i < CKDRAW_SOURCE_COUNT; ++i)
            m_DebugSourceSubmitCount[i].store(0, std::memory_order_relaxed);
        for (int i = 0; i < CKRST_MAX_PASSES; ++i)
            m_DebugViewSubmitSerial[i].store(0, std::memory_order_relaxed);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME))
            CKBgfxLogf("FrameMap", "Begin frame=%u", m_DebugFrameId);
    }
    return CK_OK;
}

CKQWORD CKBgfxRasterizerContext::GetCompletedSubmitId()
{
    return m_CompletedSubmitId;
}
