#ifndef CKSDLGPU_SHADER_JOB_H
#define CKSDLGPU_SHADER_JOB_H

#include "CKSdlGpuRasterizerContext.h"

// Creates shaders on the worker in the order added. Their code is static.
class CKSdlGpuRasterizerContext::ShaderJob : public CKSdlGpuJob {
public:
    explicit ShaderJob(CKSdlGpuRasterizerContext &context)
        : Context(context), Device(context.Device) {}
    ~ShaderJob() override;

    bool Empty() const { return Shaders.Size() == 0; }
    void Add(const std::shared_ptr<CKSdlGpuShader> &shader, SDL_GPUShaderFormat format);

    void Run() override;
    // Fails as creating a shader at once does; the programs of a missing
    // one draw nothing.
    void Complete() override;

private:
    struct Pending {
        std::shared_ptr<CKSdlGpuShader> Shader;
        SDL_GPUShaderFormat Format;
    };
    CKSdlGpuRasterizerContext &Context;
    SDL_GPUDevice *Device;
    XClassArray<Pending> Shaders;
    int Created = 0;
    // The SDL error of the first shader not created.
    XString Error;
};

#endif // CKSDLGPU_SHADER_JOB_H
