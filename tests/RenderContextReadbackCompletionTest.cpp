#include "TextureTestObserver.h"
#include <new>

namespace {
class RefusingReadbackObserver : public TextureTestObserver {
public:
    CKBOOL BeginShutdown() override { return FALSE; }
};

struct PlacementContext {
    alignas(RCKRenderContext) unsigned char Storage[sizeof(RCKRenderContext)];
    RCKRenderContext *Live;
    explicit PlacementContext(CKContext *context) : Live(new (Storage) RCKRenderContext(context)) {}
    ~PlacementContext() {
        if (Live) {
            Live->m_RasterizerContext = nullptr;
            Live->m_RasterizerDriver = nullptr;
            Live->~RCKRenderContext();
        }
    }
    void Retire() { Live->~RCKRenderContext(); Live = nullptr; }
};

class RenderContextReadbackCompletionTest : public TextureTestWorld {};

TEST_F(RenderContextReadbackCompletionTest, LateSuccessCannotWriteThroughAReplacementAtTheDestroyedOwnersAddress) {
    RefusingReadbackObserver target;
    ASSERT_TRUE(Manager->RegisterRasterizerContext(&target, 0));
    PlacementContext slot(Context);
    RCKRenderContext *retired = slot.Live;
    retired->m_RasterizerContext = &target;
    retired->m_RasterizerDriver = &Driver;
    TextureTestProbe texture(Context);
    CKDWORD original = 0xff112233u;
    Pixels(texture, &original, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(retired, FALSE));
    ASSERT_TRUE(texture.CopyContext(retired, nullptr, nullptr, 0));
    ASSERT_NE(nullptr, target.Pending);
    slot.Retire();

    // Deterministic allocator reuse: a late request belongs to the retired
    // owner, even when a live Context now occupies exactly the same address.
    RCKRenderContext *replacement = new (slot.Storage) RCKRenderContext(Context);
    slot.Live = replacement;
    replacement->m_RasterizerContext = &target;
    replacement->m_RasterizerDriver = &Driver;
    ASSERT_TRUE(Manager->RegisterRasterizerContext(&target, 0));
    ASSERT_TRUE(texture.SystemToVideoMemory(replacement, FALSE));
    const int uploadsBeforeCompletion = target.Loads;
    CKDWORD latePixel = 0xffabcdefu;
    VxImageDescEx image = Image(&latePixel, 1, 1);
    target.Complete(&image);
    EXPECT_EQ(uploadsBeforeCompletion, target.Loads);
    EXPECT_EQ(original, texture.GetPixel(0, 0));
    texture.FreeVideoMemory();
    replacement->m_RasterizerContext = nullptr;
    replacement->m_RasterizerDriver = nullptr;
    slot.Retire();
    Manager->ForgetRasterizerContext(&target);
}

TEST_F(RenderContextReadbackCompletionTest, OutOfOrderSuccessfulCompletionsRemainAttachedToTheLiveOwner) {
    TextureTestProbe texture(Context);
    CKDWORD original = 0xff010203u;
    Pixels(texture, &original, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    ASSERT_TRUE(texture.CopyContext(Device, nullptr, nullptr, 0));
    CKReadbackCallback first = State.Pending;
    void *firstUser = State.PendingUser;
    State.Pending = nullptr;
    State.PendingUser = nullptr;
    ASSERT_TRUE(texture.CopyContext(Device, nullptr, nullptr, 0));
    CKDWORD newer = 0xff334455u, older = 0xff667788u;
    VxImageDescEx newImage = Image(&newer, 1, 1), oldImage = Image(&older, 1, 1);
    State.Complete(&newImage);
    first(firstUser, nullptr, VXBUFFER_BACKBUFFER, &oldImage, TRUE);
    ASSERT_EQ(3, State.Loads);
    EXPECT_EQ(newer, Pixel(State.Uploads[1], 0));
    EXPECT_EQ(older, Pixel(State.Uploads[2], 0));
}

TEST_F(RenderContextReadbackCompletionTest, RejectedRequestDoesNotPreventLaterSuccessOrOwnerDestruction) {
    TextureTestProbe texture(Context);
    CKDWORD original = 0xff010203u;
    Pixels(texture, &original, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    State.ReadbackResult = FALSE;
    EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, 0));
    State.ReadbackResult = TRUE;
    ASSERT_TRUE(texture.CopyContext(Device, nullptr, nullptr, 0));
    CKDWORD pixel = 0xffaabbccu;
    VxImageDescEx image = Image(&pixel, 1, 1);
    State.Complete(&image);
    ASSERT_EQ(2, State.Loads);
    EXPECT_EQ(pixel, Pixel(State.Uploads[1], 0));
}
} // namespace
