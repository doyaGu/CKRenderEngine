#ifndef CKSDLGPU_DXIL_CODEC_H
#define CKSDLGPU_DXIL_CODEC_H

#include "CKSdlGpuShaderStreams.h"

namespace CKSdlGpuPack {

class DxilBitcodeDecoder;

// Rebuilds the DXIL containers of a pack from its symbol streams, one after
// another in pack order.
class DxilDecoder {
public:
    explicit DxilDecoder(SymbolStreams &symbols) : m_Symbols(symbols), m_Bitcode(NULL) {}
    ~DxilDecoder();
    DxilDecoder(const DxilDecoder &) = delete;
    DxilDecoder &operator=(const DxilDecoder &) = delete;

    CKBOOL Decode(CKBYTE *out, CKDWORD size);

private:
    SymbolStreams &m_Symbols;
    DxilBitcodeDecoder *m_Bitcode;      // made by the first Decode
};

} // namespace CKSdlGpuPack

#endif
