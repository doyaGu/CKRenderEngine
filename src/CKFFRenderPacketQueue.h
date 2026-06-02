#ifndef CKFFRENDERPACKETQUEUE_H
#define CKFFRENDERPACKETQUEUE_H

#include "CKFFRenderPacket.h"
#include "XArray.h"

#define CKFF_RENDER_PACKET_MIN_SORT_COUNT 64
#define CKFF_RENDER_PACKET_STATIC_INTERN_SCAN_LIMIT 128
#define CKFF_RENDER_PACKET_ADAPTIVE_SAMPLE_COUNT 128

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
    CKBOOL ShouldAdaptiveBypass() const;
    void MarkAdaptiveBypass();
    CKDWORD GetAdaptiveSamples() const;
    CKDWORD GetAdaptiveBypasses() const;
    CKDWORD GetAdaptiveSavedBindEstimate() const;

    CKBOOL IsDirectReplay(CKBOOL forceDirectReplay) const;
    void SortPackets(XArray<CKDWORD> &indices) const;

private:
    void TrackPacket(const CKRenderPacket &packet);
    CKDWORD EstimateSavedBinds(const CKRenderPacket &packet) const;

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
    CKDWORD m_AdaptiveSamples;
    CKDWORD m_AdaptiveBypasses;
    CKDWORD m_AdaptiveSavedBindEstimate;
};

#endif
