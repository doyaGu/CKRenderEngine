#include "CKRenderDiagnosticsContext.h"
#include "CKRenderSettings.h"
#include "CKDebugLogger.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>

namespace {
thread_local const CKRenderDiagnosticHandle *Current = nullptr;
std::atomic<uint64_t> NextId{1};
}

struct CKRenderDiagnosticsContext::Storage {
    CKRenderDiagnosticHandle Interface = {};
    CKRenderSettingsState *Configuration = nullptr;
    CKDebugLogger Output;
    struct DeleteState { void operator()(void *pointer) const { ::operator delete(pointer); } };
    std::array<std::unique_ptr<void, DeleteState>, CK_DIAGNOSTIC_SLOT_COUNT> Data;
    std::array<size_t, CK_DIAGNOSTIC_SLOT_COUNT> Sizes = {};

    explicit Storage(bool legacy) : Output(legacy) {
        Interface.Owner = this;
        Interface.Id = legacy ? 0 : NextId.fetch_add(1, std::memory_order_relaxed);
        Interface.State = [](void *owner, CKRenderDiagnosticSlot slot, size_t size) -> void * {
            auto &storage = *static_cast<Storage *>(owner);
            assert(slot >= 0 && slot < CK_DIAGNOSTIC_SLOT_COUNT);
            auto &data = storage.Data[slot];
            if (!data) {
                storage.Sizes[slot] = size;
                data.reset(::operator new(size));
                std::memset(data.get(), 0, size);
            }
            assert(storage.Sizes[slot] == size);
            return data.get();
        };
        Interface.Settings = [](void *owner) { return static_cast<Storage *>(owner)->Configuration; };
        Interface.Logger = [](void *owner) { return &static_cast<Storage *>(owner)->Output; };
        Interface.OutputEnabled = [](void *owner) { return static_cast<Storage *>(owner)->Output.IsOutputEnabled(); };
        Interface.Write = [](void *owner, const char *tag, const char *message, bool raw) {
            auto &output = static_cast<Storage *>(owner)->Output;
            if (raw) output.WriteFileLine(message);
            else output.LogTagged(tag, message);
        };
    }
};

CKRenderDiagnosticsContext::CKRenderDiagnosticsContext(bool legacy) : m_Storage(new Storage(legacy)) {}
CKRenderDiagnosticsContext::~CKRenderDiagnosticsContext() { delete m_Storage; }

void CKRenderDiagnosticsContext::Configure(CKRenderSettingsState *settings) {
    m_Storage->Configuration = settings;
    for (auto &data : m_Storage->Data) data.reset();
    m_Storage->Sizes.fill(0);
    CKRenderSettingsView debug(CKRenderSettingsSection::Debug, settings ? &settings->Configuration : nullptr);
    XString path;
    const bool explicitPath = debug.GetString("LogPath", path) && !path.Empty();
    if (!explicitPath) {
        char filename[96];
        if (m_Storage->Interface.Id)
            snprintf(filename, sizeof(filename), "CK2_3D_Debug.context-%llu.log",
                static_cast<unsigned long long>(m_Storage->Interface.Id));
        else snprintf(filename, sizeof(filename), "CK2_3D_Debug.log");
        path = CKRenderModuleSiblingFile((const void *)&CKRenderCurrentDiagnostics, filename);
        if (path.Empty()) path = filename;
    }
    m_Storage->Output.SetLogFilePath(path.CStr());
    m_Storage->Output.EnableOutput(debug.GetBool("Output", false));
}

const CKRenderDiagnosticHandle *CKRenderDiagnosticsContext::Handle() const { return &m_Storage->Interface; }
const CKRenderDiagnosticHandle *CKRenderCurrentDiagnostics() { return Current; }
CKRenderDiagnosticScope::CKRenderDiagnosticScope(const CKRenderDiagnosticHandle *context) : m_Previous(Current) { Current = context; }
CKRenderDiagnosticScope::~CKRenderDiagnosticScope() { Current = m_Previous; }

void *CKRenderDiagnosticState(CKRenderDiagnosticSlot slot, size_t size) {
    static CKRenderDiagnosticsContext fallback(true);
    const auto *context = Current ? Current : fallback.Handle();
    return context->State(context->Owner, slot, size);
}
