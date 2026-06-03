#include "CKFFRenderPacketQueue.h"

#include <cstring>

CKFFRenderPacketQueue::CKFFRenderPacketQueue()
    : m_PacketSerial(0),
      m_StaticUniformDirtySerial(1),
      m_StaticUniformCachedSerial(0),
      m_StaticUniformCachedIndex(0),
      m_StaticUniformCacheValid(FALSE),
      m_AlreadySorted(TRUE),
      m_SingleKey(TRUE),
      m_HasLastKey(FALSE),
      m_AdaptiveBypass(FALSE),
      m_AdaptiveBypassReason(CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE),
      m_AdaptivePendingBypassReason(CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE),
      m_AdaptiveSamples(0),
      m_AdaptiveBypasses(0),
      m_AdaptiveSavedBindEstimate(0),
      m_AdaptiveRepeatBindEstimate(0),
      m_AdaptiveRunBypasses(0),
      m_AdaptiveSampleRunsEvaluated(FALSE),
      m_AdaptiveSampleRuns(0),
      m_AdaptiveSampleMaxRun(0),
      m_AdaptiveSubmitSavedEstimate(0),
      m_AdaptiveCooldownBypasses(0),
      m_AdaptiveCooldownFrames(0),
      m_AdaptiveCooldownFresh(FALSE),
      m_AdaptiveFrameEndEvaluations(0),
      m_AdaptiveFrameEndRunBypasses(0)
{
    memset(&m_FirstPacketSortKey, 0, sizeof(m_FirstPacketSortKey));
    memset(&m_LastPacketSortKey, 0, sizeof(m_LastPacketSortKey));
}

void CKFFRenderPacketQueue::Clear()
{
    m_Packets.Resize(0);
    m_StaticUniformPayloads.Resize(0);
    m_StaticUniformCacheValid = FALSE;
    m_AlreadySorted = TRUE;
    m_SingleKey = TRUE;
    m_HasLastKey = FALSE;
    memset(&m_FirstPacketSortKey, 0, sizeof(m_FirstPacketSortKey));
    memset(&m_LastPacketSortKey, 0, sizeof(m_LastPacketSortKey));
}

void CKFFRenderPacketQueue::ResetFrameState()
{
    if (m_AdaptiveCooldownFresh) {
        m_AdaptiveCooldownFresh = FALSE;
    } else if (m_AdaptiveCooldownFrames > 0) {
        --m_AdaptiveCooldownFrames;
    }

    m_AdaptiveBypass = FALSE;
    m_AdaptiveBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE;
    m_AdaptivePendingBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE;
    m_AdaptiveSamples = 0;
    m_AdaptiveBypasses = 0;
    m_AdaptiveSavedBindEstimate = 0;
    m_AdaptiveRepeatBindEstimate = 0;
    m_AdaptiveRunBypasses = 0;
    m_AdaptiveSampleRunsEvaluated = FALSE;
    m_AdaptiveSampleRuns = 0;
    m_AdaptiveSampleMaxRun = 0;
    m_AdaptiveSubmitSavedEstimate = 0;
    m_AdaptiveCooldownBypasses = 0;
    m_AdaptiveFrameEndEvaluations = 0;
    m_AdaptiveFrameEndRunBypasses = 0;
}

CKBOOL CKFFRenderPacketQueue::HasPackets() const
{
    return m_Packets.Size() > 0 ? TRUE : FALSE;
}

int CKFFRenderPacketQueue::GetPacketCount() const
{
    return m_Packets.Size();
}

const CKRenderPacket &CKFFRenderPacketQueue::GetPacket(int index) const
{
    return m_Packets[index];
}

CKRenderPacket &CKFFRenderPacketQueue::GetPacket(int index)
{
    return m_Packets[index];
}

const CKFFRenderPacketUniformPayload &CKFFRenderPacketQueue::GetStaticUniformPayload(CKDWORD index) const
{
    return m_StaticUniformPayloads[(int)index];
}

CKDWORD CKFFRenderPacketQueue::NextSerial()
{
    ++m_PacketSerial;
    if (m_PacketSerial == 0)
        ++m_PacketSerial;
    return m_PacketSerial;
}

void CKFFRenderPacketQueue::MarkStaticUniformsDirty()
{
    ++m_StaticUniformDirtySerial;
    if (m_StaticUniformDirtySerial == 0)
        ++m_StaticUniformDirtySerial;
}

CKBOOL CKFFRenderPacketQueue::TryUseCachedStaticUniform(CKDWORD *index) const
{
    if (!index)
        return FALSE;
    if (!m_StaticUniformCacheValid)
        return FALSE;
    if (m_StaticUniformCachedSerial != m_StaticUniformDirtySerial)
        return FALSE;
    *index = m_StaticUniformCachedIndex;
    return TRUE;
}

void CKFFRenderPacketQueue::CacheStaticUniform(CKDWORD index)
{
    m_StaticUniformCachedSerial = m_StaticUniformDirtySerial;
    m_StaticUniformCachedIndex = index;
    m_StaticUniformCacheValid = TRUE;
}

CKDWORD CKFFRenderPacketQueue::InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload,
                                                         CKBOOL *interned)
{
    const int count = m_StaticUniformPayloads.Size();
    const int scanCount = count < CKFF_RENDER_PACKET_STATIC_INTERN_SCAN_LIMIT
        ? count
        : CKFF_RENDER_PACKET_STATIC_INTERN_SCAN_LIMIT;
    if (interned)
        *interned = FALSE;

    for (int i = 0; i < scanCount; ++i) {
        if (CKFFRenderPacketUniformPayloadEquals(m_StaticUniformPayloads[i], payload))
            return (CKDWORD)i;
    }

    m_StaticUniformPayloads.PushBack(payload);
    if (interned)
        *interned = TRUE;
    return (CKDWORD)count;
}

void CKFFRenderPacketQueue::BuildSortKey(CKRenderPacket *packet) const
{
    if (!packet)
        return;

    memset(&packet->SortKey, 0, sizeof(packet->SortKey));
    packet->SortKey.Program = packet->Program;
    packet->SortKey.DrawStateLo = packet->DrawState.Lo;
    packet->SortKey.DrawStateMid = packet->DrawState.Mid;
    packet->SortKey.DrawStateHi = packet->DrawState.Hi;
    packet->SortKey.StencilRef = packet->StencilRef;
    packet->SortKey.StencilReadMask = packet->StencilReadMask;
    packet->SortKey.StencilWriteMask = packet->StencilWriteMask;
    packet->SortKey.StaticUniformHash =
        GetStaticUniformPayload(packet->StaticUniformIndex).Hash;
    packet->SortKey.TextureSetHash = packet->TextureSetHash;
    packet->SortKey.ActiveTextureCount = packet->ActiveTextureCount;
    packet->SortKey.VertexLayout = packet->VertexLayout;
    packet->SortKey.VertexBuffer = packet->VertexBuffer;
    packet->SortKey.BaseVertex = packet->BaseVertex;
    packet->SortKey.VertexCount = packet->VertexCount;
    packet->SortKey.IndexBuffer = packet->IndexBuffer;
    packet->SortKey.StartIndex = packet->StartIndex;
    packet->SortKey.IndexCount = packet->IndexCount;
}

void CKFFRenderPacketQueue::AddPacket(const CKRenderPacket &packet)
{
    TrackPacket(packet);
    m_Packets.PushBack(packet);
}

CKBOOL CKFFRenderPacketQueue::IsAdaptiveBypassed() const
{
    if (m_AdaptiveBypass)
        return TRUE;
    return IsAdaptiveCooldownActive();
}

CKBOOL CKFFRenderPacketQueue::IsAdaptiveCooldownActive() const
{
    if (m_AdaptiveCooldownFresh)
        return FALSE;
    return m_AdaptiveCooldownFrames > 0 ? TRUE : FALSE;
}

CKBOOL CKFFRenderPacketQueue::ShouldAdaptiveBypass(CKBOOL instancingEnabled)
{
    if (m_AdaptiveBypass)
        return TRUE;
    m_AdaptivePendingBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE;
    if (m_AdaptiveSamples < CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT)
        return FALSE;
    if (m_AdaptiveSamples > CKFF_RENDER_PACKET_ADAPTIVE_SAMPLE_COUNT)
        return FALSE;
    if (instancingEnabled) {
        if (!m_AdaptiveSampleRunsEvaluated)
            EvaluateAdaptiveSampleRuns();
        if (m_AdaptiveSampleMaxRun < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT) {
            m_AdaptivePendingBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_RUN;
            if (ShouldStartNoRunCooldown())
                StartAdaptiveCooldown();
            return TRUE;
        }
        ClearAdaptiveCooldown();
        return FALSE;
    }
    if (m_AdaptiveRepeatBindEstimate == 0) {
        m_AdaptivePendingBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_BINDING;
        return TRUE;
    }
    if (m_AdaptiveSavedBindEstimate >= (m_AdaptiveSamples / 2))
        return FALSE;
    m_AdaptivePendingBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_GENERAL;
    return TRUE;
}

void CKFFRenderPacketQueue::MarkAdaptiveBypass()
{
    if (!m_AdaptiveBypass)
        ++m_AdaptiveBypasses;
    m_AdaptiveBypass = TRUE;
    m_AdaptiveBypassReason = m_AdaptivePendingBypassReason;
    if (m_AdaptiveBypassReason == CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE)
        m_AdaptiveBypassReason = CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_GENERAL;
}

void CKFFRenderPacketQueue::MarkAdaptiveCooldownBypass()
{
    ++m_AdaptiveCooldownBypasses;
}

CKBOOL CKFFRenderPacketQueue::EvaluateAdaptiveFrameEnd(CKBOOL instancingEnabled)
{
    if (!instancingEnabled)
        return FALSE;
    if (m_AdaptiveBypass)
        return FALSE;
    if (m_AdaptiveSamples < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
        return FALSE;

    ++m_AdaptiveFrameEndEvaluations;
    if (!m_AdaptiveSampleRunsEvaluated)
        EvaluateAdaptiveSampleRuns();
    if (ShouldStartNoRunCooldown()) {
        ++m_AdaptiveFrameEndRunBypasses;
        StartAdaptiveCooldown();
        return TRUE;
    }

    ClearAdaptiveCooldown();
    return FALSE;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveRunBypasses() const
{
    return m_AdaptiveRunBypasses;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveSampleRuns() const
{
    return m_AdaptiveSampleRuns;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveSampleMaxRun() const
{
    return m_AdaptiveSampleMaxRun;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveSubmitSavedEstimate() const
{
    return m_AdaptiveSubmitSavedEstimate;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveCooldownBypasses() const
{
    return m_AdaptiveCooldownBypasses;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveCooldownFrames() const
{
    return m_AdaptiveCooldownFrames;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveFrameEndEvaluations() const
{
    return m_AdaptiveFrameEndEvaluations;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveFrameEndRunBypasses() const
{
    return m_AdaptiveFrameEndRunBypasses;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveSamples() const
{
    return m_AdaptiveSamples;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveBypasses() const
{
    return m_AdaptiveBypasses;
}

CKDWORD CKFFRenderPacketQueue::GetAdaptiveSavedBindEstimate() const
{
    return m_AdaptiveSavedBindEstimate;
}

CKBOOL CKFFRenderPacketQueue::IsDirectReplay(CKBOOL forceDirectReplay) const
{
    const int packetCount = m_Packets.Size();
    if (forceDirectReplay)
        return TRUE;
    if (packetCount < 2)
        return TRUE;
    if (packetCount < CKFF_RENDER_PACKET_MIN_SORT_COUNT)
        return TRUE;
    if (m_SingleKey)
        return TRUE;
    if (m_AlreadySorted)
        return TRUE;
    return FALSE;
}

void CKFFRenderPacketQueue::SortPackets(XArray<CKDWORD> &indices) const
{
    SortPacketIndices(indices, TRUE);
}

void CKFFRenderPacketQueue::SortPacketIndices(XArray<CKDWORD> &indices,
                                              CKBOOL allowDirectReplaySkip) const
{
    const int count = m_Packets.Size();
    indices.Resize(count);
    for (int i = 0; i < count; ++i)
        indices[i] = (CKDWORD)i;
    if (count < 2)
        return;
    if (allowDirectReplaySkip && IsDirectReplay(FALSE))
        return;

    XArray<CKDWORD> scratch;
    scratch.Resize(count);
    for (int width = 1; width < count; width <<= 1) {
        for (int left = 0; left < count; left += width << 1) {
            int mid = left + width;
            int right = left + (width << 1);
            if (mid > count)
                mid = count;
            if (right > count)
                right = count;

            int a = left;
            int b = mid;
            int out = left;
            while (a < mid && b < right) {
                const CKRenderPacket &pa = m_Packets[(int)indices[a]];
                const CKRenderPacket &pb = m_Packets[(int)indices[b]];
                if (CKFFCompareRenderPacket(pa, pb) <= 0)
                    scratch[out++] = indices[a++];
                else
                    scratch[out++] = indices[b++];
            }
            while (a < mid)
                scratch[out++] = indices[a++];
            while (b < right)
                scratch[out++] = indices[b++];
        }
        for (int i = 0; i < count; ++i)
            indices[i] = scratch[i];
    }
}

int CKFFRenderPacketQueue::GetReplayPacketIndex(const XArray<CKDWORD> *indices,
                                                CKBOOL directReplay,
                                                int position) const
{
    return directReplay || !indices ? position : (int)(*indices)[position];
}

void CKFFRenderPacketQueue::GetRunStats(const XArray<CKDWORD> *indices,
                                        CKBOOL directReplay,
                                        CKDWORD *runCount,
                                        CKDWORD *maxRun) const
{
    CKDWORD runs = 0;
    CKDWORD currentRun = 0;
    CKDWORD longestRun = 0;
    const int count = directReplay || !indices ? m_Packets.Size() : indices->Size();

    for (int i = 0; i < count; ++i) {
        const int packetIndex = GetReplayPacketIndex(indices, directReplay, i);
        const CKRenderPacket &packet = m_Packets[packetIndex];
        if (i == 0) {
            runs = 1;
            currentRun = 1;
        } else {
            const int previousIndex = GetReplayPacketIndex(indices, directReplay, i - 1);
            const CKRenderPacket &previous = m_Packets[previousIndex];
            if (CKFFRenderPacketSameRunKey(previous, packet)) {
                ++currentRun;
            } else {
                if (currentRun > longestRun)
                    longestRun = currentRun;
                ++runs;
                currentRun = 1;
            }
        }
    }

    if (currentRun > longestRun)
        longestRun = currentRun;
    if (runCount)
        *runCount = runs;
    if (maxRun)
        *maxRun = longestRun;
}

void CKFFRenderPacketQueue::BuildRunPlans(const XArray<CKDWORD> *indices,
                                          CKBOOL directReplay,
                                          CKBOOL instancingEnabled,
                                          XArray<CKFFRenderPacketRunPlan> &plans) const
{
    const int count = directReplay || !indices ? m_Packets.Size() : indices->Size();
    plans.Resize(0);

    for (int i = 0; i < count;) {
        const int packetIndex = GetReplayPacketIndex(indices, directReplay, i);
        const CKRenderPacket &packet = m_Packets[packetIndex];
        int runLength = 1;
        if (instancingEnabled && packet.CanInstance) {
            while (i + runLength < count) {
                const int nextIndex = GetReplayPacketIndex(indices, directReplay, i + runLength);
                const CKRenderPacket &nextPacket = m_Packets[nextIndex];
                if (!CKFFRenderPacketCanInstanceRun(packet, nextPacket))
                    break;
                ++runLength;
            }
        }

        CKFFRenderPacketRunPlan plan;
        plan.Start = i;
        plan.Count = runLength;
        plan.Instanced = (instancingEnabled &&
                          runLength >= CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT &&
                          packet.CanInstance) ? TRUE : FALSE;
        plan.FallbackReason = plan.Instanced
            ? CKFF_RENDER_PACKET_RUN_FALLBACK_NONE
            : CKFF_RENDER_PACKET_RUN_FALLBACK_NOT_INSTANCEABLE;
        plans.PushBack(plan);
        i += runLength;
    }
}

void CKFFRenderPacketQueue::TrackPacket(const CKRenderPacket &packet)
{
    ++m_AdaptiveSamples;
    m_AdaptiveSavedBindEstimate += EstimateSavedBinds(packet);
    m_AdaptiveRepeatBindEstimate += EstimateRepeatBinds(packet);

    if (!m_HasLastKey) {
        m_HasLastKey = TRUE;
        m_AlreadySorted = TRUE;
        m_SingleKey = TRUE;
        m_FirstPacketSortKey = packet.SortKey;
        m_LastPacketSortKey = packet.SortKey;
        return;
    }

    if (CKFFCompareRenderPacketSortKey(m_LastPacketSortKey, packet.SortKey) > 0)
        m_AlreadySorted = FALSE;
    if (!CKFFRenderPacketSortKeyEquals(m_FirstPacketSortKey, packet.SortKey))
        m_SingleKey = FALSE;
    m_LastPacketSortKey = packet.SortKey;
}

void CKFFRenderPacketQueue::EvaluateAdaptiveSampleRuns()
{
    m_AdaptiveSampleRunsEvaluated = TRUE;
    m_AdaptiveSampleRuns = 0;
    m_AdaptiveSampleMaxRun = 0;
    m_AdaptiveSubmitSavedEstimate = 0;

    const int count = m_Packets.Size();
    if (count <= 0)
        return;

    XArray<CKDWORD> indices;
    SortPacketIndices(indices, FALSE);

    CKDWORD currentRun = 1;
    m_AdaptiveSampleRuns = 1;
    for (int i = 1; i < count; ++i) {
        const CKRenderPacket &previous = m_Packets[(int)indices[i - 1]];
        const CKRenderPacket &packet = m_Packets[(int)indices[i]];
        if (CKFFRenderPacketCanInstanceRun(previous, packet)) {
            ++currentRun;
        } else {
            if (currentRun > m_AdaptiveSampleMaxRun)
                m_AdaptiveSampleMaxRun = currentRun;
            if (currentRun >= CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
                m_AdaptiveSubmitSavedEstimate += currentRun - 1;
            ++m_AdaptiveSampleRuns;
            currentRun = 1;
        }
    }

    if (currentRun > m_AdaptiveSampleMaxRun)
        m_AdaptiveSampleMaxRun = currentRun;
    if (currentRun >= CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
        m_AdaptiveSubmitSavedEstimate += currentRun - 1;
    if (m_AdaptiveSampleMaxRun < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
        ++m_AdaptiveRunBypasses;
}

void CKFFRenderPacketQueue::StartAdaptiveCooldown()
{
    m_AdaptiveCooldownFrames = CKFF_RENDER_PACKET_ADAPTIVE_REPROBE_INTERVAL;
    m_AdaptiveCooldownFresh = TRUE;
}

void CKFFRenderPacketQueue::ClearAdaptiveCooldown()
{
    m_AdaptiveCooldownFrames = 0;
    m_AdaptiveCooldownFresh = FALSE;
}

CKBOOL CKFFRenderPacketQueue::ShouldStartNoRunCooldown() const
{
    if (m_AdaptiveSampleMaxRun >= CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
        return FALSE;
    return m_AdaptiveSubmitSavedEstimate == 0 ? TRUE : FALSE;
}

CKDWORD CKFFRenderPacketQueue::EstimateSavedBinds(const CKRenderPacket &packet) const
{
    if (m_Packets.Size() <= 0)
        return 0;

    const CKRenderPacket &prev = m_Packets[m_Packets.Size() - 1];
    CKDWORD saved = 0;
    if (CKFFDrawStateEquals(prev.DrawState, packet.DrawState) &&
        prev.StencilRef == packet.StencilRef &&
        prev.StencilReadMask == packet.StencilReadMask &&
        prev.StencilWriteMask == packet.StencilWriteMask)
        ++saved;
    if (prev.StaticUniformIndex == packet.StaticUniformIndex)
        ++saved;
    if (prev.SortKey.TextureSetHash == packet.SortKey.TextureSetHash &&
        prev.ActiveTextureCount == packet.ActiveTextureCount)
        ++saved;
    if (prev.VertexLayout == packet.VertexLayout &&
        prev.VertexBuffer == packet.VertexBuffer &&
        prev.BaseVertex == packet.BaseVertex &&
        prev.VertexCount == packet.VertexCount)
        ++saved;
    if (prev.IndexBuffer == packet.IndexBuffer &&
        prev.StartIndex == packet.StartIndex &&
        prev.IndexCount == packet.IndexCount)
        ++saved;
    return saved;
}

CKDWORD CKFFRenderPacketQueue::EstimateRepeatBinds(const CKRenderPacket &packet) const
{
    if (m_Packets.Size() <= 0)
        return 0;

    const CKRenderPacket &prev = m_Packets[m_Packets.Size() - 1];
    CKDWORD saved = 0;
    if (prev.SortKey.TextureSetHash == packet.SortKey.TextureSetHash &&
        prev.ActiveTextureCount == packet.ActiveTextureCount)
        ++saved;
    if (prev.VertexLayout == packet.VertexLayout &&
        prev.VertexBuffer == packet.VertexBuffer &&
        prev.BaseVertex == packet.BaseVertex &&
        prev.VertexCount == packet.VertexCount)
        ++saved;
    if (prev.IndexBuffer == packet.IndexBuffer &&
        prev.StartIndex == packet.StartIndex &&
        prev.IndexCount == packet.IndexCount)
        ++saved;
    return saved;
}
