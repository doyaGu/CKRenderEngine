#ifndef RCKCURVE_H
#define RCKCURVE_H

#include "RCK3dEntity.h"

class RCKCurve : public RCK3dEntity {
public:
    //------------------------------------
    // Length

    /************************************************
    Summary: Returns the length of the curve
    Return value: Length of the curve
    Remarks:
        + The curve length is updated every time a curve point moves.

    See also: SetLength
    ************************************************/
    virtual float GetLength();

    //------------------------------
    // Open/Closed Curves

    /************************************************
    Summary: Sets the curve as opened .

    See also: IsOpen,Close
    ************************************************/
    virtual void Open();

    /************************************************
    Summary: Sets the curve as closed .

    See also: IsOpen,Open
    ************************************************/
    virtual void Close();

    /************************************************
    Summary: Checks the curve as opened or closed.
    Return Value: TRUE if the curve is opened.

    See also: Close, Open
    ************************************************/
    virtual CKBOOL IsOpen();

    //----------------------------------------
    // Get Point position in the world referential

    /************************************************
    Summary: Gets a position along the curve.
    Arguments:
        step: A float coefficient between 0 and 1 indicating position along the curve.
        pos: A pointer to a VxVector which will be filled with position on the curve at specified step.
        dir: A optional pointer to a VxVector which will contain the direction of the curve at the specified step.
    Return Value: CK_OK if successful or an error code otherwise.
    Remarks:
        + The returned position is given in the world referential.

    See also: GetLocalPos
    ************************************************/
    virtual CKERROR GetPos(float step, VxVector *pos, VxVector *dir = NULL);

    /************************************************
    Summary: Gets a local position along the curve.

    Arguments:
        step: A float coefficient between 0 and 1 indicating position along the curve.
        pos: A pointer to a VxVector which will be filled with position on the curve at specified step.
        dir: A optional pointer to a VxVector which will contain the direction of the curve at the specified step.

    Return Value: CK_OK if successful or an error code otherwise.
    Remarks:
        + The returned position is given in the curve referential

    See also: GetPos
    ************************************************/
    virtual CKERROR GetLocalPos(float step, VxVector *pos, VxVector *dir = NULL);

    //---------------------------------------
    // Control points

    /************************************************
    Summary: Gets tangents to a control point
    Arguments:
        index: Index of the control point to get the tangents of.
        pt: A pointer to the CKCurvePoint to get the tangents of.
        in:  A pointer to a VxVector containing incoming tangent.
        out: A pointer to a VxVector containing outgoing tangent.
    Return Value: CK_OK if successful,an error code otherwise.

    See also: SetTangents
    ************************************************/
    virtual CKERROR GetTangents(int index, VxVector *in, VxVector *out);
    virtual CKERROR GetTangents(CKCurvePoint *pt, VxVector *in, VxVector *out);

    /************************************************
    Summary: Sets tangents to a control point
    Arguments:
        index: Index of the control point to get the tangents of.
        pt: A pointer to the CKCurvePoint to get the tangents of.
        in:  A pointer to a VxVector containing incoming tangent.
        out: A pointer to a VxVector containing outgoing tangent.

    Return Value: CK_OK if successful,an error code otherwise.

    See also: GetTangents
    ************************************************/
    virtual CKERROR SetTangents(int index, VxVector *in, VxVector *out);
    virtual CKERROR SetTangents(CKCurvePoint *pt, VxVector *in, VxVector *out);

    /************************************************
    Summary: Sets the fitting coefficient for the curve.
    Arguments:
        fit: Fitting coefficient.
    Remarks:
        {Image:FittingCoef}
        + A fitting coefficient of 0 make the curve pass by every control point.

    See also: GetFittingCoeff
    ************************************************/
    virtual void SetFittingCoeff(float fit);

    virtual float GetFittingCoeff();

    //------------------------------
    // Control points

    /************************************************
    Summary: Removes a control point.
    Arguments:
        pt: A pointer to the control point to remove.
        removeall: TRUE if all references to the control point should be removed.
    Return Value: CK_OK if successful or CKERR_INVALIDPARAMETER if pt is an invalid control point

    See also: RemoveAllControlPoints,InsertControlPoint,AddControlPoint,GetControlPointCount,GetControlPoint
    ************************************************/
    virtual CKERROR RemoveControlPoint(CKCurvePoint *pt, CKBOOL removeall = FALSE);

    /************************************************
    Summary: Inserts a control point in the curve.
    Arguments:
        prev: A pointer to the control point after which the point should be inserted.
        pt: A pointer to the control point to insert.
    Return Value: CK_OK if successful or an error code otherwise.

    See also: RemoveControlPoint,AddControlPoint,GetControlPointCount,GetControlPoint
    ************************************************/
    virtual CKERROR InsertControlPoint(CKCurvePoint *prev, CKCurvePoint *pt);

    /************************************************
    Summary: Adds a control point to the curve.
    Arguments:
        pt: A pointer to the CKCurvePoint to add.
    Return Value: CK_OK if successful, an error code otherwise.

    See also: RemoveControlPoint,InsertControlPoint,GetControlPointCount,GetControlPoint
    ************************************************/
    virtual CKERROR AddControlPoint(CKCurvePoint *pt);

    /************************************************
    Summary: Returns the number of control points
    Return Value: Number of control points

    See also: RemoveControlPoint,InsertControlPoint,AddControlPoint,GetControlPoint
    ************************************************/
    virtual int GetControlPointCount();

    /************************************************
    Summary: Gets a control point according to its index.
    Arguments:
        pos: Index of the control point to retrieve.
    Return Value: A pointer to the CKCurvePoint.

    See also: RemoveControlPoint,InsertControlPoint,GetControlPointCount,AddControlPoint
    ************************************************/
    virtual CKCurvePoint *GetControlPoint(int pos);

    /************************************************
    Summary: Removes all the control points
    Return Value: CK_OK if successful, an error code otherwise.

    See also: RemoveControlPoint
    ************************************************/
    virtual CKERROR RemoveAllControlPoints();

    //------------------------------
    // Mesh Representation

    /************************************************
    Summary: Sets the number of segments used to represent the curve.
    Arguments:
        steps: Number of segments.
    Return Value: CK_OK, if successful
    Remarks:
        + A line mesh can be created to represent the curve with CreateLineMesh, this method
        sets the number of segments used for this mesh.

    See also: GetStepCount
    ************************************************/
    virtual CKERROR SetStepCount(int steps);

    virtual int GetStepCount();

    /************************************************
    Summary: Creates a line mesh to represent the curve
    Return value: CK_OK if successful an error code otherwise.

    See also: SetStepCount,SetColor
    ************************************************/
    virtual CKERROR CreateLineMesh();

    virtual CKERROR UpdateMesh();

    /************************************************
    Summary: Gets the color used for the curve mesh.
    Return Value: Current color.

    See also: SetColor
    ************************************************/
    virtual VxColor GetColor();

    /************************************************
    Summary: Sets the color used for the curve mesh.
    Arguments:
        Color: new color for the curve.
    Remarks:

    See also: GetColor,CreateLineMesh
    ************************************************/
    virtual void SetColor(const VxColor &Color);

    virtual void Update();

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions
    CKBOOL IsHiddenByParent() override;
    CKBOOL IsVisible() override;

    explicit RCKCurve(CKContext *Context, CKSTRING name = nullptr);
    ~RCKCurve() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    void CheckPreDeletion() override;

    int GetMemoryOccupation() override;
    int IsObjectUsed(CKObject *o, CK_CLASSID cid) override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context, CKBOOL iCaller = TRUE) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    CKBOOL Render(CKRenderContext *Dev, CKDWORD Flags = CKRENDER_UPDATEEXTENTS) override;

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKCurve *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    XArray<RCKCurvePoint> m_ControlPoints;
    CKBOOL m_Opened;
    CKDWORD m_Length;
    int m_StepCount;
    float m_FittingCoeff;
    CKDWORD m_Color;
    CKDWORD field_1C8;
};

#endif // RCKCURVE_H
