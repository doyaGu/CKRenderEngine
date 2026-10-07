// NULL rasterizer: the engine's fallback when no rasterizer
// plugin loads and the base of the recording test harness. Checks the driver
// description (baseline caps, display modes), the frame protocol and the
// handle table.

#include "CKRecordingRasterizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Fail(const char *what)
{
    fprintf(stderr, "test_recording_backend: %s\n", what);
    return EXIT_FAILURE;
}

static bool HasDisplayMode(CKRasterizerDriver *driver, int width, int height, int bpp, int refreshRate)
{
    for (int i = 0; i < driver->GetDisplayModeCount(); ++i) {
        VxDisplayMode mode;
        if (!driver->GetDisplayMode(i, &mode))
            return false;
        if (mode.Width == width &&
            mode.Height == height &&
            mode.Bpp == bpp &&
            mode.RefreshRate == refreshRate) {
            return true;
        }
    }

    return false;
}

static int TestBackendShaderTargets()
{
    const CKFFShaderTarget targets[] = {
        {CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_PROFILE_DX11},
        {CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX12},
        {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_PROFILE_SPIRV},
    };
    for (const auto &selected : targets) {
        CKRasterizerDeviceCaps conventions;
        conventions.ShaderFormat = selected.Format;
        conventions.ShaderProfile = selected.Profile;
        CKRecordingBackend backend(conventions);
        CKRasterizerInitParameters init;
        init.Width = init.Height = 16;
        if (backend.Init(&init) != CK_OK || backend.GetCaps().ShaderFormat != selected.Format ||
            backend.GetCaps().ShaderProfile != selected.Profile)
            return Fail("an empty shader target list accepts the backend's own target");
        backend.Shutdown();

        const CKFFShaderTarget rejected[] = {
            {selected.Format == CKRST_SHADER_FORMAT_DXIL ? CKRST_SHADER_FORMAT_SPIRV : CKRST_SHADER_FORMAT_DXIL,
             selected.Profile},
            {selected.Format, selected.Profile == CKRST_SHADER_PROFILE_DX11
                ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_DX11},
            {CKRST_SHADER_FORMAT_UNKNOWN, CKRST_SHADER_PROFILE_UNKNOWN},
        };
        for (const auto &target : rejected) {
            init.ShaderTargets.Clear();
            init.ShaderTargets.PushBack(target);
            if (backend.Init(&init) != CKERR_NOTIMPLEMENTED || backend.GetDeviceStatus() == CK_OK ||
                backend.GetObjectCount(CKRST_OBJ_ALL) != 0)
                return Fail("shader targets must match both format and profile without partially initializing");
            CKDWORD object = 123;
            CKTextureDesc texture;
            texture.Format.Width = texture.Format.Height = 1;
            if (backend.CreateTexture(&texture, NULL, &object) != CKERR_INVALIDOPERATION || object != 0)
                return Fail("shader target rejection leaves resource creation disabled");
        }

        init.ShaderTargets.PushBack(selected);
        if (backend.Init(&init) != CK_OK || backend.GetCaps().ShaderFormat != selected.Format ||
            backend.GetCaps().ShaderProfile != selected.Profile)
            return Fail("initialization can retry with a matching entry in a mixed target list");
        backend.Shutdown();
        init.ShaderTargets.Clear();
        init.ShaderTargets.PushBack(selected);
        if (backend.Init(&init) != CK_OK)
            return Fail("one exact shader target permits reinitialization");
    }
    return EXIT_SUCCESS;
}

static int TestGenericBackend()
{
    CKRecordingBackend backend;
    CKRasterizerInitParameters init;
    init.Width = init.Height = 16;
    if (backend.Init(&init) != CK_OK)
        return Fail("backend construction needs no rasterizer driver");

    CKFFConstantSet constants;
    const CKBYTE complete[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const CKBYTE prefix[] = {9, 10, 11};
    if (constants.Set(31, complete, sizeof(complete)) != CK_OK ||
        constants.Set(31, prefix, sizeof(prefix)) != CK_OK)
        return Fail("arbitrary byte prefixes are accepted");
    const auto &bytes = constants[31].Bytes;
    if (bytes.Size() != sizeof(complete) || memcmp(bytes.Begin(), prefix, sizeof(prefix)) != 0 ||
        memcmp(bytes.Begin() + sizeof(prefix), complete + sizeof(prefix), sizeof(complete) - sizeof(prefix)) != 0)
        return Fail("constant prefix update preserves the remaining bytes");
    if (constants.Set(CKFF_CONSTANT_SLOT_COUNT, complete, sizeof(complete)) != CKERR_INVALIDPARAMETER ||
        constants.Set(0, complete, CKFF_MAX_CONSTANT_BYTES + 1) != CKERR_INVALIDPARAMETER ||
        constants.Set(0, NULL, 1) != CKERR_INVALIDPARAMETER ||
        constants.Set(0, complete, 0) != CKERR_INVALIDPARAMETER)
        return Fail("constant byte input bounds");

    static const CKBYTE token[] = {'t', 'e', 's', 't'};
    CKShaderDesc shader;
    shader.Format = backend.GetCaps().ShaderFormat;
    shader.Profile = backend.GetCaps().ShaderProfile;
    shader.Code = token;
    shader.CodeSize = sizeof(token);
    CKFFProgramDesc programDesc;
    if (backend.CreateShader(&shader, &programDesc.VertexShader) != CK_OK)
        return Fail("generic vertex shader");
    CKDWORD wrongTarget = 123;
    shader.Format = CKRST_SHADER_FORMAT_SPIRV;
    if (backend.CreateShader(&shader, &wrongTarget) != CKERR_INVALIDPARAMETER || wrongTarget != 0)
        return Fail("shader payload target must match the device conventions");
    shader.Format = backend.GetCaps().ShaderFormat;
    shader.Stage = CKRST_SHADER_PIXEL;
    shader.UniformBufferCount = shader.SamplerCount = 1;
    if (backend.CreateShader(&shader, &programDesc.PixelShader) != CK_OK)
        return Fail("generic fragment shader");
    CKFFUniformBufferBinding buffer;
    buffer.Stage = CKRST_SHADER_PIXEL;
    buffer.Size = 16;
    programDesc.UniformBuffers.PushBack(buffer);
    CKFFUniformBinding uniform;
    uniform.Slot = 31;
    uniform.Name = "u_customData";
    uniform.Stage = CKRST_SHADER_PIXEL;
    programDesc.Uniforms.PushBack(uniform);
    CKFFSamplerBinding sampler;
    sampler.Slot = 31;
    sampler.Name = "s_customImage";
    programDesc.Samplers.PushBack(sampler);
    CKDWORD program = 0;
    if (backend.CreateProgram(&programDesc, &program) != CK_OK || !program)
        return Fail("custom one-sampler program at logical slot 31");
    CKDWORD rejected = 123;
    programDesc.Samplers[0].NativeSlot = 1;
    if (backend.CreateProgram(&programDesc, &rejected) != CKERR_INVALIDPARAMETER || rejected != 0)
        return Fail("recording backend validates program resource declarations");
    programDesc.Uniforms.Clear();
    programDesc.Samplers.Clear();
    const CKRecordingObject *record = static_cast<const CKRecordingBackend &>(backend).FindObject(program);
    if (!record || record->Program.Uniforms.Size() != 1 || record->Program.Samplers.Size() != 1 ||
        record->Program.Uniforms[0].Name != "u_customData" || record->Program.Samplers[0].NativeSlot != 0)
        return Fail("program keeps an owned resource declaration");

    if (backend.DestroyObject(programDesc.VertexShader, CKRST_OBJ_SHADER) != CK_OK ||
        backend.DestroyObject(programDesc.PixelShader, CKRST_OBJ_SHADER) != CK_OK)
        return Fail("shader handles may be released after linking");
    if (backend.CreateProgram(&programDesc, &rejected) != CKERR_INVALIDPARAMETER || rejected != 0)
        return Fail("deleted shader handles cannot link another program");
    CKRenderPassDesc pass;
    pass.Rect.right = pass.Rect.bottom = 16;
    CKDrawCommand draw;
    draw.Program = program;
    draw.Constants = &constants;
    draw.VertexCount = 3;
    if (backend.BeginPass(&pass) != CK_OK || backend.Draw(&draw) != CK_OK)
        return Fail("a procedural program draws without vertex streams");
    backend.Shutdown();
    if (backend.Init(&init) != CK_OK || !backend.GetConstants(31).empty())
        return Fail("reinitialization starts with empty constant data");
    return EXIT_SUCCESS;
}

static int TestConfiguredDriver()
{
    CKRecordingRasterizerDriver driver;
    driver.Format = CKRST_SHADER_FORMAT_SPIRV;
    driver.Profile = CKRST_SHADER_PROFILE_SPIRV;
    driver.OriginBottomLeft = TRUE;
    driver.HomogeneousDepth = TRUE;
    XClassArray<CKFFShaderTarget> targets;
    driver.GetShaderTargets(targets);
    if (targets.Size() != 1 || targets[0].Format != driver.Format || targets[0].Profile != driver.Profile)
        return Fail("rasterizer targets follow configured shader conventions");
    CKRecordingBackend *backend = driver.CreateBackend();
    if (!backend)
        return Fail("configured backend");
    driver.Format = CKRST_SHADER_FORMAT_DXIL;
    driver.Profile = CKRST_SHADER_PROFILE_DX12;
    CKRasterizerInitParameters init;
    init.Width = init.Height = 16;
    if (backend->Init(&init) != CK_OK || backend->GetCaps().ShaderFormat != CKRST_SHADER_FORMAT_SPIRV ||
        backend->GetCaps().ShaderProfile != CKRST_SHADER_PROFILE_SPIRV ||
        !backend->GetCaps().OriginBottomLeft || !backend->GetCaps().HomogeneousDepth)
        return Fail("backend stores a value snapshot of conventions");
    CKFFShaderSet shaders;
    if (!driver.GetShaderSet(backend->GetCaps(), shaders) ||
        !shaders.Matches(CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_PROFILE_SPIRV) ||
        shaders.Shaders[CKRST_SHADER_FF_FRAGMENT].SamplerCount != CKFF_SHADER_SAMPLER_SLOT_COUNT ||
        shaders.Shaders[CKRST_SHADER_PRESENT_FRAGMENT].SamplerCount != 1 ||
        shaders.Shaders[CKRST_SHADER_PRESENT_VERTEX].UniformBufferCount != 0)
        return Fail("rasterizer supplies complete native-shaped fake artifacts for the actual device");
    return driver.DestroyBackend(backend) ? EXIT_SUCCESS : Fail("configured backend cleanup");
}

int main()
{
    if (TestBackendShaderTargets() != EXIT_SUCCESS || TestGenericBackend() != EXIT_SUCCESS ||
        TestConfiguredDriver() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    CKRecordingRasterizer rasterizer;
    if (rasterizer.GetDriverCount() != 0 || !rasterizer.Start(NULL))
        return Fail("rasterizer start");
    if (rasterizer.GetDriverCount() != 1 || !rasterizer.Start(NULL) || rasterizer.GetDriverCount() != 1)
        return Fail("one driver, idempotent start");

    CKRecordingRasterizerDriver *driver =
        static_cast<CKRecordingRasterizerDriver *>(rasterizer.GetDriver(0));
    if (!driver || rasterizer.GetDriver(1) != NULL)
        return Fail("driver lookup");

    CKRasterizerDriverDesc driverDesc = {};
    if (!driver->GetDesc(&driverDesc) || driverDesc.DriverIndex != 0 ||
        driverDesc.Hardware || !driverDesc.CapsFinal ||
        strcmp(driverDesc.Description.CStr(), "Recording Rasterizer") != 0)
        return Fail("driver description query");

    CKTextureDesc textureFormat;
    if (driver->GetDisplayModeCount() < 4 ||
        driver->GetTextureFormatCount() != 1 ||
        !driver->GetTextureFormat(0, &textureFormat) ||
        driver->GetTextureFormat(1, &textureFormat))
        return Fail("display modes / texture formats");

    if (!HasDisplayMode(driver, 640, 480, 32, 60) ||
        !HasDisplayMode(driver, 800, 600, 32, 60) ||
        !HasDisplayMode(driver, 1024, 768, 16, 60) ||
        !HasDisplayMode(driver, 1920, 1080, 32, 60))
        return Fail("legacy display modes");

    CKRasterizerNativeCapsDesc nativeCaps;
    if (!driver->GetNativeCaps(&nativeCaps) ||
        nativeCaps.MaxTextureSize != 4096 ||
        nativeCaps.MaxTextureStages != CKRST_MAX_TEXTURE_STAGES)
        return Fail("native caps query");

    CKRecordingBackend *backend = driver->CreateBackend();
    if (!backend)
        return Fail("CreateBackend");
    if (backend->GetDeviceStatus() == CK_OK)
        return Fail("status before Init");

    CKRasterizerInitParameters init;
    init.PosX = 10;
    init.PosY = 20;
    init.Width = 800;
    init.Height = 600;
    init.Bpp = 32;
    init.ZBpp = 24;
    init.StencilBpp = 8;
    if (backend->Init(&init) != CK_OK || backend->Init(&init) != CKERR_INVALIDOPERATION)
        return Fail("Init once");
    if (backend->GetDeviceStatus() != CK_OK || !backend->IsIdle())
        return Fail("status after Init");

    const CKRasterizerDeviceCaps &caps = backend->GetCaps();
    if ((caps.Features & (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER)) !=
            (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER) ||
        (caps.Features & CKRST_DEVCAPS_TEXTURE_READBACK) == 0 ||
        caps.MaxPasses != CKRST_MAX_PASSES ||
        caps.MaxTextureBindings != CKFF_TEXTURE_SLOT_COUNT ||
        caps.ShaderFormat != CKRST_SHADER_FORMAT_DXIL ||
        caps.ShaderProfile != CKRST_SHADER_PROFILE_DX12 ||
        caps.OriginBottomLeft || caps.HomogeneousDepth)
        return Fail("caps");

    if (backend->Resize(1, 2, 320, 240) != CK_OK || backend->Resize(0, 0, 0, 240) != CKERR_INVALIDPARAMETER)
        return Fail("Resize");

    // Handles: never zero, never reused, typed.
    CKTextureDesc texDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texDesc.Format);
    texDesc.Format.Width = 16;
    texDesc.Format.Height = 16;
    texDesc.MipMapCount = 1;
    texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_READBACK;
    CKDWORD texture = 0;
    if (backend->CreateTexture(&texDesc, NULL, &texture) != CK_OK || texture == 0 ||
        !backend->IsObjectAlive(texture, CKRST_OBJ_TEXTURE) || backend->IsObjectAlive(texture, CKRST_OBJ_VERTEXBUFFER))
        return Fail("CreateTexture");
    if (backend->DestroyObject(texture, CKRST_OBJ_VERTEXBUFFER) != CKERR_INVALIDPARAMETER ||
        backend->DestroyObject(texture, CKRST_OBJ_TEXTURE) != CK_OK ||
        backend->IsObjectAlive(texture, CKRST_OBJ_TEXTURE) ||
        backend->DestroyObject(texture, CKRST_OBJ_TEXTURE) != CKERR_INVALIDPARAMETER)
        return Fail("DestroyObject");
    CKDWORD replacement = 0;
    if (backend->CreateTexture(&texDesc, NULL, &replacement) != CK_OK || replacement == 0 || replacement == texture)
        return Fail("handles are not reused");

    // Frame protocol: draws need a pass, passes need Present.
    CKDrawCommand draw;
    draw.Program = 1;
    if (backend->Draw(&draw) != CKERR_INVALIDOPERATION)
        return Fail("draw outside a pass");
    CKRenderPassDesc pass;
    pass.Rect.right = 320;
    pass.Rect.bottom = 240;
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR;
    pass.Name = "scene";
    if (backend->BeginPass(&pass) != CK_OK || backend->IsIdle())
        return Fail("BeginPass");
    if (backend->Resize(0, 0, 640, 480) != CKERR_INVALIDOPERATION)
        return Fail("Resize inside a frame");
    if (backend->Draw(&draw) != CKERR_INVALIDPARAMETER)
        return Fail("draw with an unknown program");
    if (driver->DestroyBackend(backend))
        return Fail("DestroyBackend inside a frame");

    CKReadbackDesc readback;
    if (backend->ReadTexture(replacement, 0, &readback, NULL) != CK_OK || readback.Width != 16 ||
        readback.RequiredSize != 16 * 16 * 4 || readback.Format != _32_ARGB8888)
        return Fail("ReadTexture layout");
    CKRecordingReadbackTicket ticket;
    if (backend->ReadTexture(replacement, 0, &readback, &ticket) != CK_OK || !ticket ||
        ticket->Data.Size() != 16 * 16 * 4 || ticket->Data[0] != 0 ||
        backend->PollReadback(ticket, FALSE) != CKRST_READBACK_NEEDS_SUBMIT)
        return Fail("ReadTexture owns a pending zero image");

    CKDWORD frame = 0;
    if (backend->Submit(CKRST_PRESENT_IMMEDIATE, TRUE, &frame) != CK_OK ||
        frame == 0 || backend->GetLastSubmitId() == 0 ||
        backend->GetCompletedSubmitId() != backend->GetLastSubmitId() ||
        backend->PollReadback(ticket, FALSE) != CKRST_READBACK_READY ||
        !backend->IsIdle())
        return Fail("Present");
    if (backend->GetStats().Frames != 1 || backend->GetStats().Passes != 1 || backend->GetStats().Draws != 0)
        return Fail("stats");
    CKDWORD next = 0;
    const CKQWORD submitId = backend->GetLastSubmitId();
    if (backend->Submit(CKRST_PRESENT_UNCHANGED, FALSE, &next) != CK_OK ||
        next != frame + 1 || backend->GetLastSubmitId() != submitId + 1 ||
        backend->GetCompletedSubmitId() != backend->GetLastSubmitId())
        return Fail("frame numbers increase");

    backend->Shutdown();
    if (backend->GetDeviceStatus() != CKERR_INVALIDOPERATION || backend->BeginPass(&pass) != CKERR_INVALIDOPERATION)
        return Fail("shut down backend refuses work");
    if (!driver->DestroyBackend(backend) || driver->DestroyBackend(backend))
        return Fail("DestroyBackend");

    rasterizer.Close();
    if (rasterizer.GetDriverCount() != 0)
        return Fail("Close");
    return EXIT_SUCCESS;
}
