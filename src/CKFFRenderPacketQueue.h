#ifndef CKFFRENDERPACKETQUEUE_H
#define CKFFRENDERPACKETQUEUE_H

#include "CKFFRenderPacket.h"
#include "XArray.h"

#define CKFF_RENDER_PACKET_MIN_SORT_COUNT 64
#define CKFF_RENDER_PACKET_STATIC_INTERN_SCAN_LIMIT 128
#define CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT 32
#define CKFF_RENDER_PACKET_ADAPTIVE_SAMPLE_COUNT 128
#define CKFF_RENDER_PACKET_ADAPTIVE_REPROBE_INTERVAL 16
#define CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_NONE 0
#define CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_BINDING 1
#define CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_RUN 2
#define CKFF_RENDER_PACKET_ADAPTIVE_BYPASS_REASON_GENERAL 3

class CKFFRenderPacketQueue {
public:
    CKFFRenderPacketQueue();

    void Clear();
    void ResetFrameState();

    CKBOOL HasPackets() const;
    int GetPacketCount() const;
    const CKRenderPacket &GetPacket(int index) const;
    CKRenderPacket &GetPacket(int index);
    const CKFFRenderPacketUniformPayload &GetStaticUniformPayload(CKDWORD index) const;

    CKDWORD NextSerial();
    void MarkStaticUniformsDirty();
    CKBOOL TryUseCachedStaticUniform(CKDWORD *index) const;
    void CacheStaticUniform(CKDWORD index);
    CKDWORD InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload,
                                       CKBOOL *interned);
    void BuildSortKey(CKRenderPacket *packet) const;

    void AddPacket(const CKRenderPacket &packet);
    CKBOOL IsAdaptiveBypassed() const;
    CKBOOL IsAdaptiveCooldownActive() const;
    CKBOOL ShouldAdaptiveBypass(CKBOOL instancingEnabled);
    void MarkAdaptiveBypass();
    void MarkAdaptiveCooldownBypass();
    CKBOOL EvaluateAdaptiveFrameEnd(CKBOOL instancingEnabled);
    CKDWORD GetAdaptiveSamples() const;
    CKDWORD GetAdaptiveBypasses() const;
    CKDWORD GetAdaptiveSavedBindEstimate() const;
    CKDWORD GetAdaptiveRunBypasses() const;
    CKDWORD GetAdaptiveSampleRuns() const;
    CKDWORD GetAdaptiveSampleMaxRun() const;
    CKDWORD GetAdaptiveSubmitSavedEstimate() const;
    CKDWORD GetAdaptiveCooldownBypasses() const;
    CKDWORD GetAdaptiveCooldownFrames() const;
    CKDWORD GetAdaptiveFrameEndEvaluations() const;
    CKDWORD GetAdaptiveFrameEndRunBypasses() const;

    CKBOOL IsDirectReplay(CKBOOL forceDirectReplay) const;
    void SortPackets(XArray<CKDWORD> &indices) const;
    void GetRunStats(const XArray<CKDWORD> *indices,
                     CKBOOL directReplay,
                     CKDWORD *runCount,
                     CKDWORD *maxRun) const;
    void BuildRunPlans(const XArray<CKDWORD> *indices,
                       CKBOOL directReplay,
                       CKBOOL instancingEnabled,
                       XArray<CKFFRenderPacketRunPlan> &plans) const;

private:
    void SortPacketIndices(XArray<CKDWORD> &indices,
                           CKBOOL allowDirectReplaySkip) const;
    int GetReplayPacketIndex(const XArray<CKDWORD> *indices,
                             CKBOOL directReplay,
                             int position) const;
    void TrackPacket(const CKRenderPacket &packet);
    void EvaluateAdaptiveSampleRuns();
    void StartAdaptiveCooldown();
    void ClearAdaptiveCooldown();
    CKDWORD EstimateSavedBinds(const CKRenderPacket &packet) const;
    CKDWORD EstimateRepeatBinds(const CKRenderPacket &packet) const;
    CKBOOL ShouldStartNoRunCooldown() const;

    XArray<CKRenderPacket> m_Packets;
    XArray<CKFFRenderPacketUniformPayload> m_StaticUniformPayloads;
    CKDWORD m_PacketSerial;
    CKDWORD m_StaticUniformDirtySerial;
    CKDWORD m_StaticUniformCachedSerial;
    CKDWORD m_StaticUniformCachedIndex;
    CKBOOL m_StaticUniformCacheValid;
    CKBOOL m_AlreadySorted;
    CKBOOL m_SingleKey;
    CKBOOL m_HasLastKey;
    CKRenderPacketSortKey m_FirstPacketSortKey;
    CKRenderPacketSortKey m_LastPacketSortKey;
    CKBOOL m_AdaptiveBypass;
    CKDWORD m_AdaptiveBypassReason;
    CKDWORD m_AdaptivePendingBypassReason;
    CKDWORD m_AdaptiveSamples;
    CKDWORD m_AdaptiveBypasses;
    CKDWORD m_AdaptiveSavedBindEstimate;
    CKDWORD m_AdaptiveRepeatBindEstimate;
    CKDWORD m_AdaptiveRunBypasses;
    CKBOOL m_AdaptiveSampleRunsEvaluated;
    CKDWORD m_AdaptiveSampleRuns;
    CKDWORD m_AdaptiveSampleMaxRun;
    CKDWORD m_AdaptiveSubmitSavedEstimate;
    CKDWORD m_AdaptiveCooldownBypasses;
    CKDWORD m_AdaptiveCooldownFrames;
    CKBOOL m_AdaptiveCooldownFresh;
    CKDWORD m_AdaptiveFrameEndEvaluations;
    CKDWORD m_AdaptiveFrameEndRunBypasses;
};

#endif
