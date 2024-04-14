#ifndef RCKPLACE_H
#define RCKPLACE_H

#include "RCK3dEntity.h"

class RCKPlace : public RCK3dEntity {
public:
    //-----------------------------------------------------
    // Camera

    virtual CKCamera *GetDefaultCamera();

    virtual void SetDefaultCamera(CKCamera *cam);

    //-----------------------------------------------------
    // Portals

    /********************************************************
    Summary: Adds a portal.

    Arguments:
        place: A pointer to the CKPlace that is seen through the given portal.
        portal: A pointer to the CK3DEntity that represents the portal extents.
    Remarks:
    + There can be more than one portal linking this place to a given place.
    + Adding a portal between one place and another automatically adds the
    portal from the other place to this one.
    + if portal is NULL, the given place is considered to be seen wherever
    the camera is in this place.
    See Also:RemovePortal,GetPortalCount,GetPortal,ViewportClip
    *********************************************************/
    virtual void AddPortal(CKPlace *place, CK3dEntity *portal);
    /********************************************************
    Summary: Removes a portal.

    Arguments:
        place: A pointer to the CKPlace that is seen through the given portal.
        portal: A pointer to the CK3DEntity that represents the portal extents.
    See Also:AddPortal,GetPortalCount,GetPortal,ViewportClip
    *********************************************************/
    virtual void RemovePortal(CKPlace *place, CK3dEntity *portal);
    /********************************************************
    Summary: Returns the number of portals in this place.

    Return Value:
        Number of portals in this place.
    See Also:AddPortal,GetPortalCount,GetPortal,ViewportClip
    *********************************************************/
    virtual int GetPortalCount();
    /************************************************
    Summary: Returns the place seen by a given portal.
    Arguments:
        i: Index of the portal to return.
        portal: Address of a pointer to a CK3dEntity that will be filled with the result portal.
    Return Value:
        A pointer to the place seen by the portal.

    See Also:AddPortal,GetPortalCount,ViewportClip
    ************************************************/
    virtual CKPlace *GetPortal(int i, CK3dEntity **portal);
    /************************************************
    Summary: Clipping rectangle.
    Return Value:
        A VxRect that contains the extents that can be seen when in this place.
    Remarks:
    + The portal manager updates this clipping rect when parsing the places.
    + For example if a given place as only one portal the clipping rect will
    be reduce to enclose only this portal so that rendering of the remaining
    places are clipped to this portal.
    See Also:AddPortal,GetPortalCount,GetPortal
    ************************************************/
    virtual VxRect &ViewportClip();
    /************************************************
    Summary: Automatically computes a matrix standing for the portal boundaries
    between place1 and place2.
    Arguments:
    p2: Second place.
    BBoxMatrix: a reference to a matrix in which the calculation will be put.
    Return Value:
        TRUE if the bounding box matrix was generated correctly,
        FALSE if some problem occurred.
    Remarks:
    + The position and orientation of BBoxMatrix will match the best fitting bounding
    box between Place1 and Place2.
    + The scale of BBoxMatrix match the world half-size of the best fitting bounding
    box between Place1 and Place2.
    + More simply, if you set an unitary object's world matrix to BBoxMatrix
    after calling ComputeBestFitBBox() function, this object (if it's a cube) should
    wrap up the common vertices between Place1 and Place2.
    See Also:AddPortal
    ************************************************/
    virtual CKBOOL ComputeBestFitBBox(CKPlace *p2, VxMatrix &BBoxMatrix);

    explicit RCKPlace(CKContext *Context, CKSTRING name = nullptr);
    ~RCKPlace() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPlace *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    XSArray<CK3dEntity *> m_Portals;
    CK_ID field_1B0;
    CK_ID m_DefaultCamera;
    VxRect m_ClippingRect;
};

#endif // RCKPLACE_H
