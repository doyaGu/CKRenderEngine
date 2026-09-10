#ifndef CK_RENDER_PROFILE_H
#define CK_RENDER_PROFILE_H

// Optional external timeline annotations. Disabled builds do not evaluate
// arguments, read a clock, allocate storage, or depend on the NVTX headers.
#if defined(CKRE_ENABLE_PROFILING) && CKRE_ENABLE_PROFILING

#include <cstdint>
#include <nvtx3/nvToolsExt.h>

class CKRenderProfileScope {
public:
    explicit CKRenderProfileScope(const char *name) { nvtxRangePushA(name); }
    ~CKRenderProfileScope() { nvtxRangePop(); }
    CKRenderProfileScope(const CKRenderProfileScope &) = delete;
    CKRenderProfileScope &operator=(const CKRenderProfileScope &) = delete;
};

inline void CKRenderProfileValue(const char *name, std::uint64_t value) {
    nvtxEventAttributes_t event = {};
    event.version = NVTX_VERSION;
    event.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
    event.messageType = NVTX_MESSAGE_TYPE_ASCII;
    event.message.ascii = name;
    event.payloadType = NVTX_PAYLOAD_TYPE_UNSIGNED_INT64;
    event.payload.ullValue = value;
    nvtxMarkEx(&event);
}

#define CKRE_PROFILE_JOIN_IMPL(a, b) a##b
#define CKRE_PROFILE_JOIN(a, b) CKRE_PROFILE_JOIN_IMPL(a, b)
#define CKRE_PROFILE_SCOPE(Name) CKRenderProfileScope CKRE_PROFILE_JOIN(_ckreProfile, __LINE__)(Name)
#define CKRE_PROFILE_VALUE(Name, Value) CKRenderProfileValue(Name, Value)
#define CKRE_PROFILE_CALL(Name, Expression) ([&]() { CKRenderProfileScope scope(Name); return (Expression); }())

#else

#define CKRE_PROFILE_SCOPE(Name) do {} while (0)
#define CKRE_PROFILE_VALUE(Name, Value) do {} while (0)
#define CKRE_PROFILE_CALL(Name, Expression) (Expression)

#endif
#endif
