// The DXBC companion must keep the DXIL fallback's column accumulation order.
// FXC's mul(matrix, vector) starts with Y, then MADs X; DXC starts with X,
// then MADs Y. The rounding difference changes reflection coordinates enough
// to cross cube-filter tap boundaries when a lit draw switches to fragment JIT.
// Explicit MADs retain contraction, while precise prevents reassociation.
#if CKFF_NATIVE_DXBC
float4 ckffDxbcMul(float4x4 m, float4 value)
{
    precise float4 result = float4(m[0][0], m[1][0], m[2][0], m[3][0]) * value.x;
    result = mad(float4(m[0][1], m[1][1], m[2][1], m[3][1]), value.y, result);
    result = mad(float4(m[0][2], m[1][2], m[2][2], m[3][2]), value.z, result);
    result = mad(float4(m[0][3], m[1][3], m[2][3], m[3][3]), value.w, result);
    return result;
}
#define mul ckffDxbcMul
#endif
