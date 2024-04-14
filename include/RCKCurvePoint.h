#ifndef RCKCURVEPOINT_H
#define RCKCURVEPOINT_H

#include "RCK3dEntity.h"

class RCKCurvePoint : public RCK3dEntity {
public:
    //----------------------------------------------
    // Curve functions

    /************************************************
    Summary: Gets the curve which owns this control point.
    Return Value: Pointer to CKCurve

    See also: CKCurve
    ************************************************/
    virtual CKCurve *GetCurve();

    //----------------------------------------------
    // TCB Parameters

    /*************************************************
    Summary: Gets the Bias of the curve at this point.
    Return Value: Bias Value.

    See Also: SetBias, SetTension, SetContinuity.
    *************************************************/
    virtual float GetBias();

    /*************************************************
    Summary: Sets the Bias of the curve at this point

    Arguments:
        b: Bias value
    Remarks:
    See Also: GetBias, SetTension, SetContinuity.
    *************************************************/
    virtual void SetBias(float b);

    /*************************************************
    Summary: Gets the Tension of the curve at this point

     Return Value: Tension Value.
    See Also: SetBias, SetTension, SetContinuity.
    *************************************************/
    virtual float GetTension();

    /*************************************************
    Summary: Sets the Tension of the curve at this point

    Arguments:
        t: Tension value
    See Also: SetBias, GetTension, SetContinuity.
    *************************************************/
    virtual void SetTension(float t);

    /*************************************************
    Summary: Gets the Continuity of the curve at this point

    Return Value: Continuity value.
    See Also: SetBias, SetTension, SetContinuity.
    *************************************************/
    virtual float GetContinuity();

    /*************************************************
    Summary: Sets the Continuity of the curve at this point

    Arguments:
        c: Continuity value.
    See Also: SetBias, SetTension, GetContinuity.
    *************************************************/
    virtual void SetContinuity(float c);

    /*************************************************
    Summary: Checks whether the curve is linear from this point to the next.
    Return Value: TRUE if it is linear, FALSE otherwise.

    See Also: SetLinear
    *************************************************/
    virtual CKBOOL IsLinear();

    /*************************************************
    Summary: Sets the curve to be linear from this point to the next.

    Arguments:
        linear: TRUE to set the curve to linear FALSE otherwise.
    See Also: IsLinear
    *************************************************/
    virtual void SetLinear(CKBOOL linear = FALSE);

    /*************************************************
    Summary: Uses TCB data or explicit tangents

    Arguments: use : TRUE to force usage of TCB data, FALSE to use tangents.
    Remarks:
        + Each curve point has incoming and outgoing tangents. These tangents can be automatically calculated
        using TCB parameters ( SetTension,SetContinuity,SetBias) or given explicitly through SetTangents
    See Also:SetTangents,SetTension,SetContinuity,SetBias
    *************************************************/
    virtual void UseTCB(CKBOOL use = TRUE);

    /************************************************
    Summary: Checks the usage of TCB parameters.

    Return Value: TRUE to force usage of TCB data, FALSE to use tangents.
    See Also: UseTCB
    ************************************************/
    virtual CKBOOL IsTCB();

    //---------------------------------------------

    /*************************************************
    Summary: Gets Curve length at this point.
    Return value: Length of the curve from the first control point to this point.

    See Also: CKCurve::GetLength
    *************************************************/
    virtual float GetLength();

    //----------------------------------------------
    // Tangents

    /*************************************************
    Summary: Gets the tangents of the curve at this control point.

    Arguments:
        in: pointer to VxVector
        out:pointer to VxVector
    See also: SetTangents
    *************************************************/
    virtual void GetTangents(VxVector *in, VxVector *out);

    /*************************************************
    Summary: Sets the tangents at this point.
    Arguments:
        in: A pointer to VxVector to the incoming tangent
        out: A pointer to VxVector to the outgoing tangent
    Remarks:
        + Sets the incoming and outgoing tangents to the
          curve at this control point.

    See also: GetTangents
    *************************************************/
    virtual void SetTangents(VxVector *in, VxVector *out);

    /*************************************************
    Summary: Notifies the curve if any parameter gets modified.
    Remarks:
        + Notifies the curve that it should be updated.

    See also:
    *************************************************/
    virtual void NotifyUpdate();

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions
    CKBOOL IsHiddenByParent() override;
    CKBOOL IsVisible() override;

    explicit RCKCurvePoint(CKContext *Context, CKSTRING name = nullptr);
    ~RCKCurvePoint() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    void Rotate(const VxVector *Axis, float Angle, RCK3dEntity *Ref = NULL, CKBOOL KeepChildren = FALSE) override;
    void Translate(const VxVector *Vect, RCK3dEntity *Ref = NULL, CKBOOL KeepChildren = FALSE) override;
    void AddScale(const VxVector *Scale, CKBOOL KeepChildren = FALSE, CKBOOL Local = TRUE) override;
    void SetPosition(const VxVector *Pos, RCK3dEntity *Ref = NULL, CKBOOL KeepChildren = FALSE) override;
    void SetOrientation(const VxVector *Dir, const VxVector *Up, const VxVector *Right = NULL, RCK3dEntity *Ref = NULL, CKBOOL KeepChildren = FALSE) override;
    void SetQuaternion(const VxQuaternion *Quat, RCK3dEntity *Ref = NULL, CKBOOL KeepChildren = FALSE, CKBOOL KeepScale = FALSE) override;
    void SetScale(const VxVector *Scale, CKBOOL KeepChildren = FALSE, CKBOOL Local = TRUE) override;
    CKBOOL ConstructWorldMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) override;
    CKBOOL ConstructWorldMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat, const VxQuaternion *Shear, float Sign) override;
    CKBOOL ConstructLocalMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) override;
    CKBOOL ConstructLocalMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat, const VxQuaternion *Shear, float Sign) override;
    void SetLocalMatrix(const VxMatrix &Mat, CKBOOL KeepChildren = FALSE) override;
    void SetWorldMatrix(const VxMatrix &Mat, CKBOOL KeepChildren = FALSE) override;

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKCurvePoint *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKCurve *m_Curve;
    float m_Tension;
    float m_Continuity;
    float m_Bias;
    float m_Length;
    VxVector m_ReservedVector;
    VxVector m_TangentIn;
    VxVector m_m_TangentOut;
    VxVector m_NotUsedVector;
    CKBOOL m_UseTCB;
    CKBOOL m_Linear;
};

#endif // RCKCURVEPOINT_H
