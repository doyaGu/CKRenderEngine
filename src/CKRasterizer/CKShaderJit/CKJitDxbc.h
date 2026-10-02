#ifndef CKJITDXBC_H
#define CKJITDXBC_H

#include "CKJitIR.h"

// Translates a finished fragment shader to a shader model 5.1 DXBC container
// for D3D12: input and output signatures and the program, without reflection,
// signed with the container digest. The program is lowered the way FXC lowers
// the equivalent HLSL: negation and absolute values are source modifiers, a
// SUB is an ADD of the negated operand, LE is GE with swapped operands, a
// select is MOVC and the discard is a DISCARD_NZ after all other work. Every
// declared input is in the input signature where the vertex shaders write it:
// the position as SV_Position in register 0 and the varying at location n as
// TEXCOORDn in register n + 1. Resources are SM 5.1 ranges in the layout's
// register spaces. words receives the container's little-endian dwords.
// Returns false for a program the backend cannot express (varying locations
// must be unique and below 31); words is then undefined.
bool CKJitEmitDxbc(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout,
                   XArray<uint32_t> &words);

// The digest D3D validates in dwords 1..4 of a container of count dwords:
// MD5 of everything after it, with DXBC's own final block.
void CKJitDxbcDigest(const uint32_t *words, uint32_t count, uint32_t digest[4]);

#endif // CKJITDXBC_H
