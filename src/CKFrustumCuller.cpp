#include "CKFrustumCuller.h"
#include <cmath>

CKFrustumCuller::CKFrustumCuller() {
    memset(m_Planes, 0, sizeof(m_Planes));
}

void CKFrustumCuller::SetFromViewProj(const VxMatrix &vp) {
    // Extract frustum planes from the combined view-projection matrix.
    // Virtools uses row-major matrices and row vectors (v * M).
    // For row-major: plane extraction from columns of the transposed matrix.
    // Left:   row3 + row0
    // Right:  row3 - row0
    // Bottom: row3 + row1
    // Top:    row3 - row1
    // Near:   row2
    // Far:    row3 - row2

    const float *m = (const float *)&vp;
    // Row-major layout: m[row][col], so m[row*4+col]

    // Left plane
    m_Planes[0][0] = m[3]  + m[0];
    m_Planes[0][1] = m[7]  + m[4];
    m_Planes[0][2] = m[11] + m[8];
    m_Planes[0][3] = m[15] + m[12];

    // Right plane
    m_Planes[1][0] = m[3]  - m[0];
    m_Planes[1][1] = m[7]  - m[4];
    m_Planes[1][2] = m[11] - m[8];
    m_Planes[1][3] = m[15] - m[12];

    // Bottom plane
    m_Planes[2][0] = m[3]  + m[1];
    m_Planes[2][1] = m[7]  + m[5];
    m_Planes[2][2] = m[11] + m[9];
    m_Planes[2][3] = m[15] + m[13];

    // Top plane
    m_Planes[3][0] = m[3]  - m[1];
    m_Planes[3][1] = m[7]  - m[5];
    m_Planes[3][2] = m[11] - m[9];
    m_Planes[3][3] = m[15] - m[13];

    // Near plane
    m_Planes[4][0] = m[2];
    m_Planes[4][1] = m[6];
    m_Planes[4][2] = m[10];
    m_Planes[4][3] = m[14];

    // Far plane
    m_Planes[5][0] = m[3]  - m[2];
    m_Planes[5][1] = m[7]  - m[6];
    m_Planes[5][2] = m[11] - m[10];
    m_Planes[5][3] = m[15] - m[14];

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(m_Planes[i][0] * m_Planes[i][0] +
                         m_Planes[i][1] * m_Planes[i][1] +
                         m_Planes[i][2] * m_Planes[i][2]);
        if (len > 0.0f) {
            float invLen = 1.0f / len;
            m_Planes[i][0] *= invLen;
            m_Planes[i][1] *= invLen;
            m_Planes[i][2] *= invLen;
            m_Planes[i][3] *= invLen;
        }
    }
}

int CKFrustumCuller::TestBox(const VxBbox &box) const {
    int result = CKFC_INSIDE;

    for (int i = 0; i < 6; i++) {
        float nx = m_Planes[i][0];
        float ny = m_Planes[i][1];
        float nz = m_Planes[i][2];
        float d  = m_Planes[i][3];

        // Find the positive vertex (farthest along plane normal)
        float px = (nx >= 0.0f) ? box.Max.x : box.Min.x;
        float py = (ny >= 0.0f) ? box.Max.y : box.Min.y;
        float pz = (nz >= 0.0f) ? box.Max.z : box.Min.z;

        // If positive vertex is outside, the whole box is outside
        if (nx * px + ny * py + nz * pz + d < 0.0f)
            return CKFC_OUTSIDE;

        // Find the negative vertex (closest along plane normal)
        float qx = (nx >= 0.0f) ? box.Min.x : box.Max.x;
        float qy = (ny >= 0.0f) ? box.Min.y : box.Max.y;
        float qz = (nz >= 0.0f) ? box.Min.z : box.Max.z;

        // If negative vertex is outside, the box intersects this plane
        if (nx * qx + ny * qy + nz * qz + d < 0.0f)
            result = CKFC_INTERSECT;
    }

    return result;
}

CKBOOL CKFrustumCuller::GetScreenExtents(
    const VxBbox &box, const VxMatrix &viewProj,
    float viewportW, float viewportH, CKRECT *screenRect) const
{
    if (!screenRect) return FALSE;

    float minX = 1e30f, minY = 1e30f;
    float maxX = -1e30f, maxY = -1e30f;

    // Project all 8 corners of the AABB
    VxVector corners[8] = {
        {box.Min.x, box.Min.y, box.Min.z},
        {box.Max.x, box.Min.y, box.Min.z},
        {box.Min.x, box.Max.y, box.Min.z},
        {box.Max.x, box.Max.y, box.Min.z},
        {box.Min.x, box.Min.y, box.Max.z},
        {box.Max.x, box.Min.y, box.Max.z},
        {box.Min.x, box.Max.y, box.Max.z},
        {box.Max.x, box.Max.y, box.Max.z},
    };

    const float *m = (const float *)&viewProj;

    for (int i = 0; i < 8; i++) {
        float x = corners[i].x;
        float y = corners[i].y;
        float z = corners[i].z;

        // Row vector x row-major matrix
        float cx = x * m[0] + y * m[4] + z * m[8]  + m[12];
        float cy = x * m[1] + y * m[5] + z * m[9]  + m[13];
        float cw = x * m[3] + y * m[7] + z * m[11] + m[15];

        if (cw <= 0.0f) {
            // Behind camera - box extends behind, fill entire screen
            screenRect->left = 0;
            screenRect->top = 0;
            screenRect->right = (int)viewportW;
            screenRect->bottom = (int)viewportH;
            return TRUE;
        }

        float invW = 1.0f / cw;
        float sx = (cx * invW * 0.5f + 0.5f) * viewportW;
        float sy = (1.0f - (cy * invW * 0.5f + 0.5f)) * viewportH;

        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
    }

    screenRect->left = (int)minX;
    screenRect->top = (int)minY;
    screenRect->right = (int)(maxX + 1.0f);
    screenRect->bottom = (int)(maxY + 1.0f);
    return TRUE;
}
