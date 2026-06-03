#ifndef CKFFDRAWPROBES_H
#define CKFFDRAWPROBES_H

#include "CKRenderConfig.h"
#include "CKRenderPerfStats.h"

#if CKRE_ENABLE_FFP_DIAGNOSTICS

class CKFFScopeTimer {
public:
    CKFFScopeTimer(double *accum, bool enabled)
        : m_Accum(enabled ? accum : nullptr),
          m_Start(enabled ? CKRenderPerfNow() : 0.0) {}
    ~CKFFScopeTimer() { if (m_Accum) *m_Accum += CKRenderPerfElapsedUs(m_Start); }
private:
    double *m_Accum;
    double m_Start;
};

#define CKFF_SCOPE_TIME(probes, field) \
    CKFFScopeTimer _ckff_t_##field((probes).TimerSlot(&CKFFFrameStats::field), (probes).TimingEnabled())
#define CKFF_PROBE(probes, call) do { (probes).call; } while (0)

#else

#define CKFF_SCOPE_TIME(probes, field) do {} while (0)
#define CKFF_PROBE(probes, call) do {} while (0)

#endif

#endif // CKFFDRAWPROBES_H
