#ifndef CKRASTERIZERVALIDATION_H
#define CKRASTERIZERVALIDATION_H

#include "CKRasterizer.h"

inline CKERROR CKRasterizerValidateDiscard(CKDWORD Flags)
{
    const CKDWORD individualMask = CKRST_DISCARD_BINDINGS |
                                   CKRST_DISCARD_INDEX_BUFFER |
                                   CKRST_DISCARD_INSTANCE_DATA |
                                   CKRST_DISCARD_STATE |
                                   CKRST_DISCARD_TRANSFORM |
                                   CKRST_DISCARD_VERTEX_STREAMS;
    return Flags == CKRST_DISCARD_ALL || (Flags & ~individualMask) == 0
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

inline CKDWORD CKRasterizerKnownObjectMask()
{
    return CKRST_OBJ_TEXTURE |
           CKRST_OBJ_VERTEXBUFFER |
           CKRST_OBJ_INDEXBUFFER |
           CKRST_OBJ_SHADER |
           CKRST_OBJ_PROGRAM |
           CKRST_OBJ_UNIFORM |
           CKRST_OBJ_FRAMEBUFFER |
           CKRST_OBJ_VERTEXLAYOUT |
           CKRST_OBJ_OCCLUSIONQUERY |
           CKRST_OBJ_INDIRECTBUFFER;
}

inline CKERROR CKRasterizerValidateObjectMask(CKDWORD TypeMask)
{
    return TypeMask == CKRST_OBJ_ALL ||
           (TypeMask & ~CKRasterizerKnownObjectMask()) == 0
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

inline CKERROR CKRasterizerValidateSampler(const CKSamplerDesc *Sampler)
{
    if (!Sampler)
        return CK_OK;

    const bool minValid = Sampler->MinFilter == CKRST_FILTER_NEAREST ||
                          Sampler->MinFilter == CKRST_FILTER_LINEAR ||
                          Sampler->MinFilter == CKRST_FILTER_ANISOTROPIC;
    const bool magValid = Sampler->MagFilter == CKRST_FILTER_NEAREST ||
                          Sampler->MagFilter == CKRST_FILTER_LINEAR ||
                          Sampler->MagFilter == CKRST_FILTER_ANISOTROPIC;
    const bool mipValid = Sampler->MipFilter >= CKRST_FILTER_NONE &&
                          Sampler->MipFilter <= CKRST_FILTER_ANISOTROPIC;
    const bool addressValid =
        Sampler->AddressU >= CKRST_ADDRESS_WRAP && Sampler->AddressU <= CKRST_ADDRESS_BORDER &&
        Sampler->AddressV >= CKRST_ADDRESS_WRAP && Sampler->AddressV <= CKRST_ADDRESS_BORDER &&
        Sampler->AddressW >= CKRST_ADDRESS_WRAP && Sampler->AddressW <= CKRST_ADDRESS_BORDER;
    const bool compareValid = Sampler->CompareFunc >= CKRST_COMPARE_NONE &&
                              Sampler->CompareFunc <= CKRST_COMPARE_ALWAYS;

    return minValid && magValid && mipValid && addressValid && compareValid &&
           Sampler->BorderColor <= 0x0f
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

inline CKDWORD CKRasterizerVertexElementSize(const CKVertexElementDesc &Element)
{
    switch (Element.Type) {
    case CKRST_ATTRIBTYPE_INT8:
    case CKRST_ATTRIBTYPE_UINT8:
        return Element.Count >= 3 ? 4 : Element.Count;
    case CKRST_ATTRIBTYPE_UINT10:
        return 4;
    case CKRST_ATTRIBTYPE_INT16:
    case CKRST_ATTRIBTYPE_UINT16:
    case CKRST_ATTRIBTYPE_HALF:
        return Element.Count >= 3 ? 8 : Element.Count * 2;
    case CKRST_ATTRIBTYPE_FLOAT:
        return Element.Count * 4;
    default:
        return 0;
    }
}

inline CKERROR CKRasterizerValidateVertexLayout(
    const CKVertexLayoutDesc *Desc)
{
    if (!Desc || !Desc->Elements || Desc->ElementCount == 0 ||
        Desc->ElementCount > CKRST_ATTRIB_COUNT || Desc->Stride == 0)
        return CKERR_INVALIDPARAMETER;

    for (CKDWORD i = 0; i < Desc->ElementCount; ++i) {
        const CKVertexElementDesc &element = Desc->Elements[i];
        const CKDWORD size = CKRasterizerVertexElementSize(element);
        if (static_cast<CKDWORD>(element.Attrib) >= CKRST_ATTRIB_COUNT ||
            element.Count < 1 || element.Count > 4 || size == 0 ||
            (element.AsInt != FALSE &&
             (element.Type == CKRST_ATTRIBTYPE_HALF ||
              element.Type == CKRST_ATTRIBTYPE_FLOAT)) ||
            element.Offset >= Desc->Stride ||
            size > static_cast<CKDWORD>(Desc->Stride - element.Offset))
            return CKERR_INVALIDPARAMETER;

        const CKDWORD begin = element.Offset;
        const CKDWORD end = begin + size;
        for (CKDWORD j = 0; j < i; ++j) {
            const CKVertexElementDesc &previous = Desc->Elements[j];
            const CKDWORD previousBegin = previous.Offset;
            const CKDWORD previousEnd = previousBegin +
                CKRasterizerVertexElementSize(previous);
            if (previous.Attrib == element.Attrib ||
                (begin < previousEnd && previousBegin < end))
                return CKERR_INVALIDPARAMETER;
        }
    }
    return CK_OK;
}

inline CKERROR CKRasterizerValidateDrawState(CKDrawState State)
{
    const CKDWORD depthFunc = (State.Lo >> 6) & 0xF;
    const CKDWORD cullMode = (State.Lo >> 10) & 0x3;
    const CKDWORD fillMode = (State.Lo >> 12) & 0x3;
    const CKDWORD blendSrc = (State.Lo >> 16) & 0xF;
    const CKDWORD blendDst = (State.Lo >> 20) & 0xF;
    const CKDWORD blendSrcA = (State.Lo >> 24) & 0xF;
    const CKDWORD blendDstA = (State.Lo >> 28) & 0xF;
    const CKDWORD blendEq = State.Mid & 0x7;
    const CKDWORD blendEqA = (State.Mid >> 3) & 0x7;
    const CKDWORD primitive = (State.Mid >> 6) & 0x7;
    const CKDWORD stencilFunc = (State.Mid >> 10) & 0xF;
    const CKDWORD stencilFail = (State.Mid >> 14) & 0xF;
    const CKDWORD stencilZFail = (State.Mid >> 18) & 0xF;
    const CKDWORD stencilPass = (State.Mid >> 22) & 0xF;
    const CKDWORD backFunc = State.Hi & 0xF;
    const CKDWORD backFail = (State.Hi >> 4) & 0xF;
    const CKDWORD backZFail = (State.Hi >> 8) & 0xF;
    const CKDWORD backPass = (State.Hi >> 12) & 0xF;

    const bool depthValid = (State.Lo & CKRST_STATE_DEPTH_TEST) != 0
        ? depthFunc >= VXCMP_NEVER && depthFunc <= VXCMP_ALWAYS
        : depthFunc == 0 || (depthFunc >= VXCMP_NEVER && depthFunc <= VXCMP_ALWAYS);
    const bool colorBlendValid = blendSrc == 0
        ? blendDst == 0
        : blendSrc >= VXBLEND_ZERO && blendSrc <= VXBLEND_BOTHINVSRCALPHA &&
          blendDst >= VXBLEND_ZERO && blendDst <= VXBLEND_BOTHINVSRCALPHA;
    const bool alphaBlendValid = blendSrcA == 0
        ? blendDstA == 0
        : blendSrcA >= VXBLEND_ZERO && blendSrcA <= VXBLEND_BOTHINVSRCALPHA &&
          blendDstA >= VXBLEND_ZERO && blendDstA <= VXBLEND_BOTHINVSRCALPHA;
    const bool blendEquationValid =
        (blendEq == 0 || (blendEq >= VXBLENDOP_ADD && blendEq <= VXBLENDOP_MAX)) &&
        (blendEqA == 0 || (blendEqA >= VXBLENDOP_ADD && blendEqA <= VXBLENDOP_MAX));
    const bool stencilEnabled = (State.Mid & CKRST_STENCIL_ENABLE) != 0;
    const bool frontStencilValid = !stencilEnabled ||
        (stencilFunc >= VXCMP_NEVER && stencilFunc <= VXCMP_ALWAYS &&
         stencilFail >= VXSTENCILOP_KEEP && stencilFail <= VXSTENCILOP_DECR &&
         stencilZFail >= VXSTENCILOP_KEEP && stencilZFail <= VXSTENCILOP_DECR &&
         stencilPass >= VXSTENCILOP_KEEP && stencilPass <= VXSTENCILOP_DECR);
    const bool hasBackStencil = backFunc != 0 || backFail != 0 ||
                                backZFail != 0 || backPass != 0;
    const bool backStencilValid = !hasBackStencil ||
        (stencilEnabled && backFunc >= VXCMP_NEVER && backFunc <= VXCMP_ALWAYS &&
         backFail >= VXSTENCILOP_KEEP && backFail <= VXSTENCILOP_DECR &&
         backZFail >= VXSTENCILOP_KEEP && backZFail <= VXSTENCILOP_DECR &&
         backPass >= VXSTENCILOP_KEEP && backPass <= VXSTENCILOP_DECR);

    if (!depthValid || cullMode == 3 || fillMode == 3 ||
        !colorBlendValid || !alphaBlendValid || !blendEquationValid ||
        primitive > VX_TRIANGLEFAN ||
        !frontStencilValid || !backStencilValid ||
        (State.Mid & 0xFC000000UL) != 0 || (State.Hi & 0xFFFE0000UL) != 0)
        return CKERR_INVALIDPARAMETER;

    if (primitive == VX_TRIANGLEFAN || blendSrc > VXBLEND_SRCALPHASAT ||
        blendDst > VXBLEND_SRCALPHASAT || blendSrcA > VXBLEND_SRCALPHASAT ||
        blendDstA > VXBLEND_SRCALPHASAT)
        return CKERR_NOTIMPLEMENTED;

    return CK_OK;
}

#endif
