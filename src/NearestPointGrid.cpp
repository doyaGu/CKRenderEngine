/// @file NearestPointGrid.cpp
/// @brief Implementation of spatial grid for nearest point queries

#include "NearestPointGrid.h"

#include <cmath>

////////////////////////////////////////////////////////////////////////////////
// Constructor
////////////////////////////////////////////////////////////////////////////////
NearestPointGrid::NearestPointGrid() : m_SizeX(0), m_SizeY(0), m_SizeZ(0), m_SizeXY(0), m_SizeXYZ(0), m_Threshold(0.1f) {
    m_Threshold2 = m_Threshold * m_Threshold;
}

////////////////////////////////////////////////////////////////////////////////
// Destructor
////////////////////////////////////////////////////////////////////////////////
NearestPointGrid::~NearestPointGrid() {
    for (int a = 0; a < m_SizeXYZ; ++a) {
        delete m_Grid[a];
    }
}

////////////////////////////////////////////////////////////////////////////////
// SetGridDimensions
////////////////////////////////////////////////////////////////////////////////
void NearestPointGrid::SetGridDimensions(int sizeX, int sizeY, int sizeZ) {
    for (int a = 0; a < m_SizeXYZ; ++a) {
        delete m_Grid[a];
    }

    m_SizeX = sizeX;
    m_SizeY = sizeY;
    m_SizeZ = sizeZ;
    m_SizeXY = m_SizeX * m_SizeY;
    m_SizeXYZ = m_SizeXY * m_SizeZ;

    m_Grid.Resize(m_SizeXYZ);
    m_Grid.Memset(0);
}

////////////////////////////////////////////////////////////////////////////////
// AddPoint
////////////////////////////////////////////////////////////////////////////////
void NearestPointGrid::AddPoint(const VxVector &p, int index) {
    Cell *&cell = GetCell((int) p.x, (int) p.y, (int) p.z);
    if (cell == NULL) {
        cell = new Cell;
    }
    Point point;
    point.pt = p;
    point.index = index;
    cell->PushBack(point);
}

////////////////////////////////////////////////////////////////////////////////
// FindNearPoint
////////////////////////////////////////////////////////////////////////////////
int NearestPointGrid::FindNearPoint(const VxVector &p) {
    if (m_SizeX <= 0 || m_SizeY <= 0 || m_SizeZ <= 0)
        return -1;

    // Calculate search bounds
    int cellMinX = (int) (p.x - m_Threshold);
    if (cellMinX < 0) cellMinX = 0;
    int cellMinY = (int) (p.y - m_Threshold);
    if (cellMinY < 0) cellMinY = 0;
    int cellMinZ = (int) (p.z - m_Threshold);
    if (cellMinZ < 0) cellMinZ = 0;

    int cellMaxX = (int) (p.x + m_Threshold);
    if (cellMaxX >= m_SizeX) cellMaxX = m_SizeX - 1;
    int cellMaxY = (int) (p.y + m_Threshold);
    if (cellMaxY >= m_SizeY) cellMaxY = m_SizeY - 1;
    int cellMaxZ = (int) (p.z + m_Threshold);
    if (cellMaxZ >= m_SizeZ) cellMaxZ = m_SizeZ - 1;

    int bestIndex = -1;
    float bestDistance2 = m_Threshold2;

    // Search cells in region
    for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
        for (int cellY = cellMinY; cellY <= cellMaxY; ++cellY) {
            for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
                Cell *cell = GetCell(cellX, cellY, cellZ);
                if (cell) {
                    int cellPtCount = cell->Size();
                    for (int a = 0; a < cellPtCount; ++a) {
                        const VxVector &cellPt = (*cell)[a].pt;
                        const float dx = cellPt.x - p.x;
                        if (fabsf(dx) > m_Threshold) continue;
                        const float dy = cellPt.y - p.y;
                        if (fabsf(dy) > m_Threshold) continue;
                        const float dz = cellPt.z - p.z;
                        if (fabsf(dz) > m_Threshold) continue;

                        const float distance2 = dx * dx + dy * dy + dz * dz;
                        if (distance2 > bestDistance2)
                            continue;

                        bestDistance2 = distance2;
                        bestIndex = (*cell)[a].index;
                    }
                }
            }
        }
    }
    return bestIndex;
}
