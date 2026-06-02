#include "CKFFRenderPacketQueue.h"
#include "CKRenderPipeline.h"
#include "TestTriangleMultiset.h"

#include <cstring>
#include <stdio.h>

static void InitPayload(CKFFRenderPacketUniformPayload *payload,
                        CKDWORD hash,
                        float value)
{
    memset(payload, 0, sizeof(*payload));
    payload->Hash = hash;
    payload->EntryCount = 1;
    payload->Vec4Count = 1;
    payload->Entries[0].Uniform = 100;
    payload->Entries[0].Offset = 0;
    payload->Entries[0].Count = 1;
    payload->Entries[0].Vec4Count = 1;
    payload->Values[0][0] = value;
}

static CKDWORD InternPayload(CKFFRenderPacketQueue *queue,
                             CKDWORD hash,
                             float value,
                             CKBOOL *interned)
{
    CKFFRenderPacketUniformPayload payload;
    InitPayload(&payload, hash, value);
    return queue->InternStaticUniformPayload(payload, interned);
}

static CKDWORD InternDefaultPayload(CKFFRenderPacketQueue *queue)
{
    CKBOOL interned = FALSE;
    return InternPayload(queue, 7, 1.0f, &interned);
}

static void InitPacket(CKRenderPacket *packet,
                       CKDWORD serial,
                       CKDWORD vertexBuffer,
                       CKDWORD indexBuffer,
                       CKDWORD staticUniformIndex)
{
    memset(packet, 0, sizeof(*packet));
    packet->Serial = serial;
    packet->View = CKRP_VIEW_OPAQUE3D;
    packet->Type = VX_TRIANGLELIST;
    packet->Program = 10;
    packet->DrawState.Lo = 1;
    packet->DrawState.Mid = 2;
    packet->DrawState.Hi = 3;
    packet->VertexLayout = 77;
    packet->VertexBuffer = vertexBuffer;
    packet->BaseVertex = 0;
    packet->VertexCount = 3;
    packet->IndexBuffer = indexBuffer;
    packet->StartIndex = 0;
    packet->IndexCount = 3;
    packet->StaticUniformIndex = staticUniformIndex;
    packet->InstancedProgram = 20;
    packet->CanInstance = TRUE;
    packet->ViewProjection.SetIdentity();
    packet->ViewProjectionHash = CKFFHashBytes(&packet->ViewProjection,
                                               sizeof(packet->ViewProjection),
                                               2166136261u);
    packet->World.SetIdentity();
}

static void AddPacket(CKFFRenderPacketQueue *queue,
                      CKDWORD serial,
                      CKDWORD vertexBuffer,
                      CKDWORD indexBuffer,
                      CKDWORD staticUniformIndex)
{
    CKRenderPacket packet;
    InitPacket(&packet, serial, vertexBuffer, indexBuffer, staticUniformIndex);
    queue->BuildSortKey(&packet);
    queue->AddPacket(packet);
}

static CKBOOL SameSortKeyExceptSerial(const CKRenderPacket &a,
                                      const CKRenderPacket &b)
{
    return CKFFRenderPacketSortKeyEquals(a.SortKey, b.SortKey);
}

static void StaticUniformInterningUsesExactCompare()
{
    CKFFRenderPacketQueue queue;
    CKBOOL interned = FALSE;

    CKDWORD first = InternPayload(&queue, 123, 1.0f, &interned);
    TestCheck(interned == TRUE, "first payload should be inserted");

    CKDWORD duplicate = InternPayload(&queue, 123, 1.0f, &interned);
    TestCheck(interned == FALSE, "duplicate payload should be reused");
    TestCheck(first == duplicate, "duplicate payload should return the first index");

    CKDWORD collision = InternPayload(&queue, 123, 2.0f, &interned);
    TestCheck(interned == TRUE, "hash collision with different data should be inserted");
    TestCheck(collision != first, "hash collision must not reuse a different payload");
}

static void QueueSortsByPrecomputedKeyAndKeepsStableSerial()
{
    CKFFRenderPacketQueue queue;
    CKDWORD staticUniformIndex = InternDefaultPayload(&queue);

    for (CKDWORD i = 0; i < 80; ++i) {
        CKDWORD vertexBuffer = (i & 1) ? 300 : 100;
        CKDWORD indexBuffer = (i & 1) ? 400 : 200;
        AddPacket(&queue, i + 1, vertexBuffer, indexBuffer, staticUniformIndex);
    }

    TestCheck(queue.IsDirectReplay(FALSE) == FALSE,
              "mixed large queue should request sorted replay");

    XArray<CKDWORD> indices;
    queue.SortPackets(indices);
    TestCheck(indices.Size() == 80, "sort should return every packet index");

    CKDWORD lastSerialForKey = 0;
    for (int i = 1; i < indices.Size(); ++i) {
        const CKRenderPacket &previous = queue.GetPacket((int)indices[i - 1]);
        const CKRenderPacket &current = queue.GetPacket((int)indices[i]);
        TestCheck(CKFFCompareRenderPacket(previous, current) <= 0,
                  "packets should be sorted by key and serial");
        if (SameSortKeyExceptSerial(previous, current)) {
            TestCheck(previous.Serial < current.Serial,
                      "equal keys should preserve serial order");
            lastSerialForKey = current.Serial;
        } else {
            lastSerialForKey = current.Serial;
        }
    }
    TestCheck(lastSerialForKey != 0, "stable sort should visit sorted packets");
}

static void RunPlansMergeInstanceCompatiblePackets()
{
    CKFFRenderPacketQueue queue;
    CKDWORD staticUniformIndex = InternDefaultPayload(&queue);

    for (CKDWORD i = 0; i < 8; ++i)
        AddPacket(&queue, i + 1, 100, 200, staticUniformIndex);

    XArray<CKFFRenderPacketRunPlan> plans;
    queue.BuildRunPlans(NULL, TRUE, TRUE, plans);
    TestCheck(plans.Size() == 1, "compatible packets should form one run");
    TestCheck(plans[0].Instanced == TRUE, "run should be marked instanced");
    TestCheck(plans[0].Count == 8, "run should include all packets");

    queue.BuildRunPlans(NULL, TRUE, FALSE, plans);
    TestCheck(plans.Size() == 8, "disabled instancing should produce packet plans");
    for (int i = 0; i < plans.Size(); ++i) {
        TestCheck(plans[i].Instanced == FALSE, "packet plan should not be instanced");
        TestCheck(plans[i].Count == 1, "packet plan should replay one packet");
    }
}

static void RunPlansSplitViewProjectionChanges()
{
    CKFFRenderPacketQueue queue;
    CKDWORD staticUniformIndex = InternDefaultPayload(&queue);

    for (CKDWORD i = 0; i < 8; ++i) {
        CKRenderPacket packet;
        InitPacket(&packet, i + 1, 100, 200, staticUniformIndex);
        if (i >= 4) {
            packet.ViewProjection[3][0] = 5.0f;
            packet.ViewProjectionHash = CKFFHashBytes(&packet.ViewProjection,
                                                      sizeof(packet.ViewProjection),
                                                      2166136261u);
        }
        queue.BuildSortKey(&packet);
        queue.AddPacket(packet);
    }

    XArray<CKFFRenderPacketRunPlan> plans;
    queue.BuildRunPlans(NULL, TRUE, TRUE, plans);
    TestCheck(plans.Size() == 2, "viewProjection changes should split instance runs");
    TestCheck(plans[0].Instanced == TRUE && plans[0].Count == 4,
              "first viewProjection run should instance four packets");
    TestCheck(plans[1].Instanced == TRUE && plans[1].Count == 4,
              "second viewProjection run should instance four packets");
}

static void InstanceRunCompatibilityRequiresExactViewProjection()
{
    CKFFRenderPacketQueue queue;
    CKDWORD staticUniformIndex = InternDefaultPayload(&queue);
    CKRenderPacket first;
    CKRenderPacket second;

    InitPacket(&first, 1, 100, 200, staticUniformIndex);
    InitPacket(&second, 2, 100, 200, staticUniformIndex);
    queue.BuildSortKey(&first);
    queue.BuildSortKey(&second);

    second.ViewProjection[3][0] = 9.0f;
    second.ViewProjectionHash = first.ViewProjectionHash;

    TestCheck(CKFFRenderPacketCanInstanceRun(first, second) == FALSE,
              "same viewProjection hash must still exact-compare matrix data");
}

int main()
{
    TestFramework tests;
    tests.Run("StaticUniformInterningUsesExactCompare", &StaticUniformInterningUsesExactCompare);
    tests.Run("QueueSortsByPrecomputedKeyAndKeepsStableSerial", &QueueSortsByPrecomputedKeyAndKeepsStableSerial);
    tests.Run("RunPlansMergeInstanceCompatiblePackets", &RunPlansMergeInstanceCompatiblePackets);
    tests.Run("RunPlansSplitViewProjectionChanges", &RunPlansSplitViewProjectionChanges);
    tests.Run("InstanceRunCompatibilityRequiresExactViewProjection", &InstanceRunCompatibilityRequiresExactViewProjection);
    return tests.ExitCode();
}
