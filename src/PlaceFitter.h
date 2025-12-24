#pragma once

#include "CKTypes.h"

class CK3dEntity;
class VxMatrix;

// Helper used by CKPlace::ComputeBestFitBBox in original CK2_3D.dll.
// Responsible for finding common vertices between two places and fitting an oriented bounding box.
class PlaceFitter {
public:
    PlaceFitter();

    // p1/p2 are expected to be CKPlace instances, but passed as CK3dEntity* to avoid depending
    // on a complete CKPlace type in translation units that only see forward declarations.
    CKBOOL ComputeBestFitBBox(CK3dEntity *p1, CK3dEntity *p2, VxMatrix &bboxMatrix);

private:
    int m_TargetCells;
    int m_MaxCells;
    float m_GridThreshold; // In grid-space units (cell coordinates)
    int m_MinCommonPoints;
};
