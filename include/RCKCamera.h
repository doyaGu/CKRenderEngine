#ifndef RCKCAMERA_H
#define RCKCAMERA_H

#include "RCK3dEntity.h"

class RCKCamera : public RCK3dEntity {
public:
    /*************************************************
    Summary: Returns the front clipping plane distance

    Return Value:
        Front clipping plane distance.
    Remarks:
        + The front plane distance is the distance below which nothing is seen by the camera.
        + By default, at creation time, the front plane distance is equal to 1.0f.
    See also: GetBackPlane,SetFrontPlane
    *************************************************/
    virtual float GetFrontPlane();

    /*************************************************
    Summary: Sets the front clipping plane distance

    Arguments:
        front: The new front clipping plane distance
    Remarks:
        + The front plane distance is the distance below which nothing is seen by the camera.
        + By default, at creation time, the front plane distance is equal to 1.0f.
    See also: SetBackPlane,GetFrontPlane
    *************************************************/
    virtual void SetFrontPlane(float front);

    /*************************************************
    Summary: Returns the back clipping plane distance

    Return Value:
        Back clipping plane distance.
    Remarks:
        + The back plane distance is the distance beyond which nothing is seen by the camera.
        + By default, at creation time, the back clipping plane distance is equal to 4000.0f.
    See also: SetBackPlane,SetFrontPlane
    *************************************************/
    virtual float GetBackPlane();

    /*************************************************
    Summary: Sets the back clipping plane distance

    Arguments:
        back: The new back clipping plane distance
    Remarks:
        + The back plane distance is the distance beyond which nothing is seen by the camera.
        + By default, at creation time, the back clipping plane distance is equal to 4000.0f.
    See also: GetBackPlane,SetFrontPlane
    *************************************************/
    virtual void SetBackPlane(float back);

    /*************************************************
    Summary: Returns the field of view of the camera

    Return Value:
        Field of view of the camera.
    Remarks:
        + By default, the field of view is equal to: 0.5f ( 30 Degree )
    See also: SetFov
    *************************************************/
    virtual float GetFov();

    /*************************************************
    Summary: Changes the field of view of the camera

    Arguments:
         fov: The new field of view of the camera
    Remarks:
        + By default, the field of view is equal to: 0.5f ( 30 Degree )
    See also: GetFov
    *************************************************/
    virtual void SetFov(float fov);

    /*************************************************
    Summary: Returns the current projection type

    Return Value:
        CK_PERSPECTIVEPROJECTION : perspective projection (default)
        CK_ORTHOGRAPHICPROJECTION : orthographic projection
    Remarks:
        + The project type is either orthographic or perspective.
    See also: SetProjectionType,SetOrthographicZoom
    *************************************************/
    virtual int GetProjectionType();

    /*************************************************
    Summary: Sets the projection type of the camera

    Arguments:
        proj: The new projection type
    Remarks:
    + The project type is either orthographic or perspective.
    + proj = CK_PERSPECTIVEPROJECTION : perspective projection (default)
    + proj = CK_ORTHOGRAPHICPROJECTION : orthographic projection
    + If the projection is orthographic, you can specify a zoom value.
    See also: GetProjectionType,SetOrthographicZoom
    *************************************************/
    virtual void SetProjectionType(int proj);

    /*************************************************
    Summary: Changes the zoom value in orthographic projection

    Arguments:
        zoom: The new zoom value
    Remarks:
    + Changes the zoom value. Valid only if in orthographic projection mode.
    + The bigger the value, the bigger the objects...
    + By default, at creation time, the zoom of the camera is equal to: 1.0f
    See also: GetOrthographicZoom,SetProjectionType
    *************************************************/
    virtual void SetOrthographicZoom(float zoom);

    /*************************************************
    Summary: Returns the zoom value in orthographic projection

    Return value
        Zoom factor.
    See also: SetOrthographicZoom,SetProjectionType
    *************************************************/
    virtual float GetOrthographicZoom();

    //---------------------------------------
    // AspectRatio

    /*****************************************************
    Summary:Sets the aspect ratio

    Remarks:
        + The given aspect ratio will be used to resize the
        viewport of the render context on which this camera is
        attached.
    See also: GetAspectRatio,CKRenderContext::SetViewRect,CKRenderContext::AttachViewpointToCamera
    *****************************************************/
    virtual void SetAspectRatio(int width, int height);

    /*****************************************************
    Summary:Gets the aspect ratio

    Remarks:
        + The given aspect ratio will be used to resize the
        viewport of the render context on which this camera is
        attached.
    See also: SetAspectRatio,CKRenderContext::SetViewRect,CKRenderContext::AttachViewpointToCamera
    *****************************************************/
    virtual void GetAspectRatio(int &width, int &height);

    //-------------------------------------
    // Result Projection Matrix

    /*****************************************************
    Summary: Computes the projection matrix

    Remarks:
        + This method uses the fov,aspect ratio, back and front clipping to compute
        a projection matrix.
    See also: VxMatrix::Perspective,VxMatrix::Orthographic
    *****************************************************/
    virtual void ComputeProjectionMatrix(VxMatrix &mat);

    //---------------------------------------
    // Roll Angle

    /*****************************************************
    Summary:Rolls back the camera to vertical.

    Remarks:
        + Rolls back the camera to vertical by aligning its Up axis (Y)
        with the world up axis.
        + If the camera as a target this method as no effect as the orientation
        of the camera will be forced toward its target.

    See Also:CKTargetCamera,SetTarget,Roll
    *****************************************************/
    virtual void ResetRoll();

    /*****************************************************
    Summary:Rolls the camera of the desired angle,

    Arguments:
        angle: Angle of rotation around Z axis in radians.
    Remarks:
    + If the camera as a target this method as no effect as the orientation
    of the camera will be forced toward its target.
    + To align the camera back with the Up vector in the world use ResetRoll
    See Also:CKTargetCamera,SetTarget,ResetRoll
    *****************************************************/
    virtual void Roll(float angle);

    //-------------------------------------------
    // Target Access

    /*****************************************************
    Summary:Returns the target of this camera.

    Return Value:
        A pointer to the CK3dEntity used as target for this camera.
    Remarks:
        + This method is only implemented for a CKTargetCamera
        otherwise it returns NULL.
    See Also:CKTargetCamera,SetTarget
    *****************************************************/
    virtual RCK3dEntity *GetTarget();

    /*****************************************************
    Summary:Sets the target of this camera.

    Arguments:
        target: A pointer to the CK3dEntity to use as a target or NULL to remove the current target.
    Remarks:
        + This method is only implemented for a CKTargetCamera.
        + If the camera has a target its orientation it always
        updated toward the given target.
    See Also:CKTargetCamera,GetTarget
    *****************************************************/
    virtual void SetTarget(RCK3dEntity *target);

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions

    explicit RCKCamera(CKContext *Context, CKSTRING name = nullptr);
    ~RCKCamera() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKCamera *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    float m_Fov;
    float m_FrontPlane;
    float m_BackPlane;
    int m_ProjectionType;
    float m_OrthographicZoom;
    int m_Width;
    int m_Height;
};

#endif // RCKCAMERA_H
