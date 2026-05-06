#ifndef CKFRUSTUMCULLER_H
#define CKFRUSTUMCULLER_H

#include "VxMath.h"
#include "CKTypes.h"

// Frustum test results
#define CKFC_OUTSIDE  0
#define CKFC_INTERSECT 1
#define CKFC_INSIDE   2

class CKFrustumCuller {
public:
    CKFrustumCuller();

    // Extract frustum planes from a view-projection matrix
    void SetFromViewProj(const VxMatrix &viewProj);

    // Test an axis-aligned bounding box against the frustum
    // Returns: CKFC_OUTSIDE, CKFC_INTERSECT, or CKFC_INSIDE
    int TestBox(const VxBbox &box) const;

    // Compute screen-space extents of a world-space AABB
    CKBOOL GetScreenExtents(const VxBbox &box, const VxMatrix &viewProj,
                           float viewportW, float viewportH,
                           CKRECT *screenRect) const;

private:
    // 6 frustum planes: left, right, bottom, top, near, far
    // Each plane stored as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0 is inside
    float m_Planes[6][4];
};

#endif // CKFRUSTUMCULLER_H
