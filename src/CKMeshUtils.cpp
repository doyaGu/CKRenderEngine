#include "RCKMesh.h"

#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"

void (*g_BuildFaceNormalsFunc)(CKFace *, CKWORD *, int, VxVertex *, int);
void (*g_BuildNormalsFunc)(CKFace *, CKWORD *, int, VxVertex *, int);
int (*g_RayIntersection)(RCKMesh *, VxVector &, VxVector &, VxIntersectionDesc *, CK_RAYINTERSECTION, VxMatrix const &);
void (*g_NormalizeFunc)(VxVertex *, int);

void SetProcessorSpecific_FunctionsPtr() {
    // IDA @ 0x1002e020
    // Set default generic implementations
    g_BuildFaceNormalsFunc = BuildFaceNormalsGenericFunc;
    g_BuildNormalsFunc = BuildNormalsGenericFunc;
    g_RayIntersection = RayIntersectionGenericFunc;
    g_NormalizeFunc = NormalizeGenericFunc;

    // IDA lines 6-12: Check for SSE support (bit 25 of processor features)
    // Original code checks GetProcessorFeatures() & 0x2000000 and assigns SIMD versions
    // For now, use generic implementations only as SIMD versions are not implemented
    // TODO: Implement SIMD versions (BuildFaceNormalsSIMDFunc, BuildNormalsSIMDFunc, 
    //       Mesh_RayIntersectionSIMDFunc, NormalizeSIMDFunc) if needed
}

// =====================================================
// Generic Math Functions for Mesh Operations
// IDA @ 0x1002e1ba - 0x1002f52e
// =====================================================

// IDA @ 0x1002e82f: Precise texture pick for alpha testing
CKBOOL PreciseTexturePick(CKMaterial *mat, float u, float v) {
    if (!mat)
        return TRUE;

    CKTexture *tex = mat->GetTexture(0);
    if (!tex)
        return TRUE;

    // Picking threshold is independent of the material's render alpha test.
    int alphaRef = tex->GetPickThreshold();
    if (alphaRef == 0)
        return TRUE;

    // Apply texture addressing mode
    VXTEXTURE_ADDRESSMODE addrMode = mat->GetTextureAddressMode();
    switch (addrMode) {
    case VXTEXTURE_ADDRESSWRAP:
        u = u - (float) (int) u;
        if (u < 0.0f)
            u += 1.0f;
        v = v - (float) (int) v;
        if (v < 0.0f)
            v += 1.0f;
        break;
    case VXTEXTURE_ADDRESSMIRROR: {
        int ui = (int) u;
        u = u - (float) ui;
        if (u < 0.0f)
            u += 1.0f;
        if (ui & 1)
            u = 1.0f - u;
        int vi = (int) v;
        v = v - (float) vi;
        if (v < 0.0f)
            v += 1.0f;
        if (vi & 1)
            v = 1.0f - v;
        break;
    }
    case VXTEXTURE_ADDRESSCLAMP:
    case VXTEXTURE_ADDRESSBORDER:
        if (u < 0.0f)
            u = 0.0f;
        else if (u > 1.0f)
            u = 1.0f;
        if (v < 0.0f)
            v = 0.0f;
        else if (v > 1.0f)
            v = 1.0f;
        break;
    default:
        break;
    }

    // Get pixel coordinates
    int width = tex->GetWidth();
    int height = tex->GetHeight();
    int px = (int) ((float) (width - 1) * u);
    int py = (int) ((float) (height - 1) * v);

    // Get pixel alpha
    CKDWORD pixel = tex->GetPixel(px, py);
    CKDWORD alpha = pixel >> 24;

    return alpha >= (CKDWORD) alphaRef;
}

// IDA @ 0x1002e1ba: Build face normals from vertex positions
void BuildFaceNormalsGenericFunc(CKFace *faces, CKWORD *indices, int faceCount, VxVertex *vertices, int vertexCount) {
    int indexOffset = 0;     // v16
    CKFace *facePtr = faces; // a1
    for (int i = 0; i < faceCount; ++i) {
        // Get vertex positions for this face (IDA lines 22-23)
        const VxVector &v0 = vertices[indices[indexOffset]].m_Position;
        const VxVector &v1 = vertices[indices[indexOffset + 1]].m_Position;
        const VxVector &v2 = vertices[indices[indexOffset + 2]].m_Position;

        // Keep finite float geometry from overflowing or underflowing the area.
        const double e1x = static_cast<double>(v1.x) - v0.x;
        const double e1y = static_cast<double>(v1.y) - v0.y;
        const double e1z = static_cast<double>(v1.z) - v0.z;
        const double e2x = static_cast<double>(v2.x) - v0.x;
        const double e2y = static_cast<double>(v2.y) - v0.y;
        const double e2z = static_cast<double>(v2.z) - v0.z;
        const double nx = e1y * e2z - e1z * e2y;
        const double ny = e1z * e2x - e1x * e2z;
        const double nz = e1x * e2y - e1y * e2x;
        const double length = sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0.0) {
            facePtr->m_Normal.Set(static_cast<float>(nx / length),
                                 static_cast<float>(ny / length),
                                 static_cast<float>(nz / length));
        } else {
            facePtr->m_Normal.Set(0.0f, 0.0f, 0.0f);
        }

        // IDA lines 35-36
        ++facePtr;        // IDA line 35: ++a1
        indexOffset += 3; // IDA line 36: v16 += 3
    }
}

// IDA @ 0x1002e2cb: Build vertex normals by averaging face normals
void BuildNormalsGenericFunc(CKFace *faces, CKWORD *indices, int faceCount, VxVertex *vertices, int vertexCount) {
    // First build face normals (IDA line 15)
    BuildFaceNormalsGenericFunc(faces, indices, faceCount, vertices, vertexCount);

    // Clear all vertex normals (IDA lines 16-20)
    VxVertex *v = vertices;
    for (int i = 0; i < vertexCount; ++i) {
        v->m_Normal.Set(0.0f, 0.0f, 0.0f);
        ++v;
    }

    // Accumulate face normals to vertices (IDA lines 21-36)
    int indexOffset = 0;     // v14
    CKFace *facePtr = faces; // IDA uses faces pointer directly (a2)
    for (int j = 0; j < faceCount; ++j) {
        const VxVector &faceNormal = facePtr->m_Normal;

        // Add face normal to each vertex of this face using SDK operator+=
        vertices[indices[indexOffset]].m_Normal += faceNormal;
        int idx1 = indexOffset + 1; // v15

        vertices[indices[idx1]].m_Normal += faceNormal;
        int idx2 = idx1 + 1; // v15 after increment

        vertices[indices[idx2]].m_Normal += faceNormal;

        indexOffset = idx2 + 1; // v14 = v15 + 1 (IDA line 33)
        ++facePtr;              // IDA line 35: ++a2
    }

    // Normalize all vertex normals (IDA lines 37-43)
    NormalizeGenericFunc(vertices, vertexCount);
}

// IDA @ 0x1002e423: Normalize vertex normals
void NormalizeGenericFunc(VxVertex *vertices, int count) {
    for (int i = 0; i < count; ++i) {
        VxVector &normal = vertices[i].m_Normal;
        // Normalize nonzero directions without the SDK cutoff or float squares.
        const double x = normal.x;
        const double y = normal.y;
        const double z = normal.z;
        const double length = sqrt(x * x + y * y + z * z);
        if (length > 0.0) {
            normal.Set(static_cast<float>(x / length),
                       static_cast<float>(y / length),
                       static_cast<float>(z / length));
        }
    }
}

// IDA @ 0x1002ea85: Mesh ray intersection (generic implementation)
// This is a complex function with spatial partitioning optimization
int RayIntersectionGenericFunc(RCKMesh *mesh, VxVector &origin, VxVector &direction,
                               VxIntersectionDesc *desc, CK_RAYINTERSECTION mode,
                               const VxMatrix &worldMatrix) {
    float minDist = 1.0e35f; // v82
    CKBOOL foundHit = FALSE; // v74

    // Get mesh data directly from protected members (friend access)
    float *vertexPtr = (float *) mesh->m_Vertices.Begin(); // v78
    CKFace *faces = mesh->m_Faces.Begin();                 // v80
    CKWORD *indices = mesh->m_FaceVertexIndices.Begin();   // v77
    int faceCount = mesh->m_Faces.Size();                  // v83
    int vertexCount = mesh->m_Vertices.Size();             // v84
    int hitCount = 0;                                      // v79

    if (faceCount == 0 || vertexCount == 0)
        return 0;

    // Allocate classification buffer for spatial culling (IDA: CKMemoryPool)
    CKDWORD *vertexFlags = new CKDWORD[vertexCount]; // v81
    memset(vertexFlags, 0, sizeof(CKDWORD) * vertexCount);

    int earlyOutMask = 0; // v75

    // Spatial partitioning optimization for large meshes (>= 16 faces)
    if (faceCount >= 16) {
        // Two ray-orthogonal planes suffice for rejection. Double products
        // keep this optimisation independent of finite direction scales.
        const double dx = direction.x, dy = direction.y, dz = direction.z;
        double axis1x = -dz, axis1y = 0.0, axis1z = dx;
        if (axis1x * axis1x + axis1z * axis1z < FLT_EPSILON) {
            axis1x = 0.0;
            axis1y = dz;
            axis1z = -dy;
        }
        const double axis2x = dy * axis1z - dz * axis1y;
        const double axis2y = dz * axis1x - dx * axis1z;
        const double axis2z = dx * axis1y - dy * axis1x;

        earlyOutMask = -1; // All bits set

        // Classify each vertex relative to the ray
        CKDWORD *flagPtr = vertexFlags;
        for (int v = 0; v < vertexCount; ++v) {
            const VxVector &position = mesh->m_Vertices[v].m_Position;
            const double x = static_cast<double>(position.x) - origin.x;
            const double y = static_cast<double>(position.y) - origin.y;
            const double z = static_cast<double>(position.z) - origin.z;
            const double proj1 = x * axis1x + y * axis1y + z * axis1z;
            const double proj2 = x * axis2x + y * axis2y + z * axis2z;
            const double error1 = 8.0 * DBL_EPSILON *
                (fabs(x * axis1x) + fabs(y * axis1y) + fabs(z * axis1z));
            const double error2 = 8.0 * DBL_EPSILON *
                (fabs(x * axis2x) + fabs(y * axis2y) + fabs(z * axis2z));

            // A boundary or roundoff-uncertain point cannot reject a face.
            if (proj1 > error1)
                *flagPtr |= 2;
            else if (proj1 < -error1)
                *flagPtr |= 1;

            if (proj2 > error2)
                *flagPtr |= 8;
            else if (proj2 < -error2)
                *flagPtr |= 4;

            earlyOutMask &= *flagPtr;

            ++flagPtr;
        }
    }

    // If not all vertices are on one side, perform intersection tests
    if (earlyOutMask == 0) {
        int bestI1 = 0;           // v65
        int bestI2 = 0;           // v67
        int bestFaceIdx = -1;     // v66

        const VxVector *faceNormalPtr = (const VxVector *) faces; // v59 - points to face normal
        CKWORD *indexPtr = indices;                               // v64

        // Create ray structure (IDA: sub_100301F0)
        VxRay ray;
        ray.m_Origin = origin;
        ray.m_Direction = direction;

        int i1 = 1; // v57
        int i2 = 2; // v55

        // Select intersection function based on mode (IDA lines 153-158)
        typedef XBOOL (*IntersectFunc)(const VxRay &, const VxPlane &, VxVector &, float &);
        IntersectFunc culledFunc = VxIntersect::RayPlaneCulled;
        IntersectFunc twoSidedFunc = VxIntersect::RayPlane;

        if ((mode & CKRAYINTERSECTION_SEGMENT) != 0) {
            culledFunc = VxIntersect::SegmentPlaneCulled;
            twoSidedFunc = VxIntersect::SegmentPlane;
        }

        VxVector intersectPoint; // v60
        float dist;              // v63

        // Test each face (IDA lines 161-187)
        for (int f = 0; f < faceCount; ++f) {
            // Early out using vertex classification (IDA line 163)
            if (!(vertexFlags[indexPtr[2]] & vertexFlags[indexPtr[1]] & vertexFlags[indexPtr[0]])) {
                // Get vertex positions (IDA lines 165-167)
                const VxVector *v0 = (const VxVector *) &vertexPtr[8 * indexPtr[0]];
                const VxVector *v1 = (const VxVector *) &vertexPtr[8 * indexPtr[1]];
                const VxVector *v2 = (const VxVector *) &vertexPtr[8 * indexPtr[2]];

                // Get material and check if two-sided (IDA lines 168-170)
                CKMaterial *mat = mesh->GetFaceMaterial(f);
                CKBOOL twoSided = mat && mat->IsTwoSided();

                // Keep the SDK plane/culling contract, but include triangle
                // boundaries for both windings rather than its SIMD face test.
                VxPlane plane;
                plane.Create(*faceNormalPtr, *v0);
                CKBOOL hit;
                if (twoSided)
                    hit = twoSidedFunc(ray, plane, intersectPoint, dist);
                else
                    hit = culledFunc(ray, plane, intersectPoint, dist);

                if (hit) {
                    float dominant = fabsf(faceNormalPtr->x);
                    i1 = 1;
                    i2 = 2;
                    if (dominant < fabsf(faceNormalPtr->y)) {
                        dominant = fabsf(faceNormalPtr->y);
                        i1 = 0;
                        i2 = 2;
                    }
                    if (dominant < fabsf(faceNormalPtr->z)) {
                        i1 = 0;
                        i2 = 1;
                    }
                    const VxVector *corners[3] = {v0, v1, v2};
                    CKBOOL nonnegative = TRUE, nonpositive = TRUE;
                    for (int edge = 0; edge < 3; ++edge) {
                        const VxVector &a = *corners[edge];
                        const VxVector &b = *corners[(edge + 1) % 3];
                        const double side =
                            (static_cast<double>(intersectPoint[i1]) - a[i1]) *
                                (static_cast<double>(b[i2]) - a[i2]) -
                            (static_cast<double>(intersectPoint[i2]) - a[i2]) *
                                (static_cast<double>(b[i1]) - a[i1]);
                        if (side < 0.0) nonnegative = FALSE;
                        if (side > 0.0) nonpositive = FALSE;
                    }
                    hit = nonnegative || nonpositive;
                }

                if (hit) {
                    // The public occlusion query returns on its first hit.
                    if (!desc) {
                        delete[] vertexFlags;
                        return 1;
                    }
                    ++hitCount;
                    if (!foundHit || dist < minDist) {
                        foundHit = TRUE;
                        minDist = dist;
                        bestI1 = i1;
                        bestI2 = i2;
                        bestFaceIdx = f;
                    }
                    if ((mode & CKRAYINTERSECTION_FIRSTCONTACT) != 0)
                        break;
                }
            }

            // Move to next face (IDA lines 184-186)
            faceNormalPtr = (const VxVector *) ((char *) faceNormalPtr + 16); // sizeof(CKFace) = 16
            indexPtr += 3;
        }

        // Fill in intersection description (IDA lines 188-265)
        if (foundHit && desc) {
            // Calculate intersection point: origin + direction * minDist (IDA lines 190-191)
            desc->IntersectionPoint = origin + direction * minDist;

            int faceIdx = bestFaceIdx;
            int indexBase = faceIdx * 3;

            // Get vertex pointers (IDA lines 193-195)
            // These point to VxVertex structures - v42/v41/v48 in IDA
            const float *vert0 = &vertexPtr[8 * indices[indexBase]];
            const float *vert1 = &vertexPtr[8 * indices[indexBase + 1]];
            const float *vert2 = &vertexPtr[8 * indices[indexBase + 2]];

            // Get barycentric coordinates (IDA lines 196-205)
            float c0, c1, c2; // v46, v45, arg4a
            VxIntersect::GetPointCoefficients(
                desc->IntersectionPoint,
                *(const VxVector *) vert0,
                *(const VxVector *) vert1,
                *(const VxVector *) vert2,
                bestI1, bestI2, c0, c1, c2);

            // Interpolate normal: vert[1] is the Normal in VxVertex (IDA lines 206-210)
            // v42[1] = Normal, accessed as &v42[1].x which is vert0 + 3 floats
            const VxVector &n0 = *(const VxVector *) (vert0 + 3);
            const VxVector &n1 = *(const VxVector *) (vert1 + 3);
            const VxVector &n2 = *(const VxVector *) (vert2 + 3);
            desc->IntersectionNormal = c0 * n0 + c1 * n1 + c2 * n2;

            // Interpolate texture coordinates (IDA lines 211-212)
            // v42[2] = UV in VxVertex, accessed as vert0 + 6 floats
            desc->TexU = c0 * vert0[6] + c1 * vert1[6] + c2 * vert2[6]; // UV.x
            desc->TexV = c0 * vert0[7] + c1 * vert1[7] + c2 * vert2[7]; // UV.y

            // Get material for perspective correction and alpha test (IDA lines 213-262)
            CKMaterial *faceMat = mesh->GetFaceMaterial(faceIdx);
            if (faceMat) {
                // Check if perspective correction is disabled (IDA lines 216-255)
                if (!faceMat->PerspectiveCorrectionEnabled()) {
                    CKContext *ctx = mesh->m_Context;
                    RCKRenderContext *dev = (RCKRenderContext *) ctx->GetPlayerRenderContext();
                    if (dev) {
                        CK3dEntity *rootEntity = dev->m_RenderedScene->GetRootEntity();
                        if (desc->Object == (CKRenderObject *) rootEntity) {
                            // Perform perspective-correct UV interpolation
                            VxMatrix invWorld;
                            Vx3DMatrixIdentity(invWorld);
                            VxMatrix combined;
                            Vx3DMatrixIdentity(combined);

                            const VxMatrix &invWorldMat = rootEntity->GetInverseWorldMatrix();
                            Vx3DMultiplyMatrix(combined, invWorldMat, worldMatrix);

                            VxMatrix projCombined;
                            Vx3DMultiplyMatrix4(projCombined, dev->m_ProjectionMatrix, combined);

                            // Transform points to clip space
                            VxVector4 clipIntersect, clipV0, clipV1, clipV2;
                            Vx3DMultiplyMatrixVector4(&clipIntersect, projCombined, &desc->IntersectionPoint);
                            Vx3DMultiplyMatrixVector4(&clipV0, projCombined, (const VxVector *) vert0);
                            Vx3DMultiplyMatrixVector4(&clipV1, projCombined, (const VxVector *) vert1);
                            Vx3DMultiplyMatrixVector4(&clipV2, projCombined, (const VxVector *) vert2);

                            // Perspective divide
                            clipV0.w = 1.0f / clipV0.w;
                            clipV1.w = 1.0f / clipV1.w;
                            clipV2.w = 1.0f / clipV2.w;
                            clipIntersect.w = 1.0f / clipIntersect.w;

                            clipV0.x *= clipV0.w;
                            clipV1.x *= clipV1.w;
                            clipV2.x *= clipV2.w;
                            clipIntersect.x *= clipIntersect.w;

                            clipV0.y *= clipV0.w;
                            clipV1.y *= clipV1.w;
                            clipV2.y *= clipV2.w;
                            clipIntersect.y *= clipIntersect.w;

                            // Recalculate barycentric coordinates in screen space
                            int newI1 = 0, newI2 = 1;
                            VxIntersect::GetPointCoefficients(
                                *(const VxVector *) &clipIntersect,
                                *(const VxVector *) &clipV0,
                                *(const VxVector *) &clipV1,
                                *(const VxVector *) &clipV2,
                                newI1, newI2, c0, c1, c2);

                            // Recalculate UVs with perspective-correct interpolation
                            desc->TexU = c0 * vert0[6] + c1 * vert1[6] + c2 * vert2[6];
                            desc->TexV = c0 * vert0[7] + c1 * vert1[7] + c2 * vert2[7];
                        }
                    }
                }

                // Precise texture pick (alpha test) (IDA lines 256-261)
                if (!(mode & CKRAYINTERSECTION_IGNOREALPHA) &&
                    !PreciseTexturePick(faceMat, desc->TexU, desc->TexV)) {
                    delete[] vertexFlags;
                    return 0; // Hit was on transparent pixel
                }
            }

            // Final output (IDA lines 263-264)
            desc->Distance = minDist;
            desc->FaceIndex = faceIdx;
        }
    }

    delete[] vertexFlags;
    return foundHit ? hitCount : 0;
}
