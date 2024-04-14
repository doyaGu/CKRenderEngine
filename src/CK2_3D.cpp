#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "CKGlobals.h"
#include "CKPluginManager.h"

#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "CKKinematicChain.h"
#include "CKMaterial.h"
#include "CKTexture.h"
#include "CKMesh.h"
#include "CKPatchMesh.h"
#include "CKAnimation.h"
#include "CKKeyedAnimation.h"
#include "CKObjectAnimation.h"
#include "CKLayer.h"
#include "CKRenderObject.h"
#include "CK2dEntity.h"
#include "CK3dEntity.h"
#include "CKCamera.h"
#include "CKLight.h"
#include "CKCurvePoint.h"
#include "CKCurve.h"
#include "CK3dObject.h"
#include "CKSprite3D.h"
#include "CKCharacter.h"
#include "CKPlace.h"
#include "CKGrid.h"
#include "CKBodyPart.h"
#include "CKTargetCamera.h"
#include "CKTargetLight.h"
#include "CKSprite.h"
#include "CKSpriteText.h"
#include "CKRasterizer.h"

#define VIRTOOLS_RENDERENGIEN_GUID CKGUID(0xAABCF63, 0)

extern CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizer *rst);

HMODULE g_DllHandle = nullptr;
CKBOOL g_EnumerationDone = FALSE;
CKPluginInfo g_PluginInfo;
XClassArray<CKRasterizerInfo>g_RasterizersInfo;

void RegisterRasterizer(char *dll) {
    if (!dll || *dll == '\0')
        return;

    VxSharedLibrary sl;
    INSTANCE_HANDLE instance = sl.Load(dll);
    if (!instance)
        return;

    for (CKRasterizerInfo *it = g_RasterizersInfo.Begin(); it != g_RasterizersInfo.End(); ++it) {
        if (it->DllInstance == instance) {
            sl.ReleaseLibrary();
            return;
        }
    }

    CKRST_GETINFO getInfoFunc = (CKRST_GETINFO)sl.GetFunctionPtr("CKRasterizerGetInfo");
    if (!getInfoFunc)
        sl.ReleaseLibrary();

    CKRasterizerInfo info;
    getInfoFunc(&info);
    info.DllInstance = instance;
    info.DllName = dll;
    g_RasterizersInfo.PushBack(info);
}

void EnumerateRasterizers() {
    char moduleName[MAX_PATH];

    if (!g_EnumerationDone) {
        VxGetModuleFileName(g_DllHandle, moduleName, MAX_PATH);
        CKPathSplitter ps(moduleName);

        XString dir = ps.GetDrive();
        dir << ps.GetDir();

        CKDirectoryParser dp(dir.Str(), "*DX8Rasterizer.dll", TRUE);
        char *file = dp.GetNextFile();
        while (file != nullptr) {
            RegisterRasterizer(file);
            file = dp.GetNextFile();
        }

        if (g_RasterizersInfo.Size() == 0) {
            CKRasterizerInfo info;
            info.StartFct = CKNULLRasterizerStart;
            info.CloseFct = CKNULLRasterizerClose;
            info.DllInstance = nullptr;
            info.DllName = "";
            info.Desc = "NULL Rasterizer";
            g_RasterizersInfo.PushBack(info);
        }

        g_EnumerationDone = TRUE;
    }
}

void InitializeCK2_3D() {
    CKCLASSREGISTERCID(CKRenderContext, CKCID_OBJECT)
    CKCLASSREGISTERCID(CKKinematicChain, CKCID_OBJECT)
    CKCLASSREGISTERCID(CKMaterial, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(CKTexture, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(CKMesh, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(CKPatchMesh, CKCID_MESH)
    CKCLASSREGISTERCID(CKAnimation, CKCID_SCENEOBJECT)
    CKCLASSREGISTERCID(CKKeyedAnimation, CKCID_ANIMATION)
    CKCLASSREGISTERCID(CKObjectAnimation, CKCID_SCENEOBJECT)
    CKCLASSREGISTERCID(CKLayer, CKCID_OBJECT)
    CKCLASSREGISTERCID(CKRenderObject, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(CK2dEntity, CKCID_RENDEROBJECT)
    CKCLASSREGISTERCID(CK3dEntity, CKCID_RENDEROBJECT)
    CKCLASSREGISTERCID(CKCamera, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKLight, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKCurvePoint, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKCurve, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CK3dObject, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKSprite3D, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKCharacter, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKPlace, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKGrid, CKCID_3DENTITY)
    CKCLASSREGISTERCID(CKBodyPart, CKCID_3DOBJECT)
    CKCLASSREGISTERCID(CKTargetCamera, CKCID_CAMERA)
    CKCLASSREGISTERCID(CKTargetLight, CKCID_LIGHT)
    CKCLASSREGISTERCID(CKSprite, CKCID_2DENTITY)
    CKCLASSREGISTERCID(CKSpriteText, CKCID_SPRITE)
    CKBuildClassHierarchyTable();
}


void SetProcessorSpecific_FunctionsPtr() {

}

CKERROR InitInstanceFct(CKContext *context) {
    new RCKRenderManager(context);
    return CK_OK;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo() {
    EnumerateRasterizers();
    InitializeCK2_3D();
    SetProcessorSpecific_FunctionsPtr();

    g_PluginInfo.m_Author = "Virtools";
    g_PluginInfo.m_Description = "Default Render Engine";
    g_PluginInfo.m_Extension = "";
    g_PluginInfo.m_Type = CKPLUGIN_RENDERENGINE_DLL;
    g_PluginInfo.m_Version = 0x000001;
    g_PluginInfo.m_InitInstanceFct = InitInstanceFct;
    g_PluginInfo.m_ExitInstanceFct = nullptr;
    g_PluginInfo.m_GUID = VIRTOOLS_RENDERENGIEN_GUID;
    g_PluginInfo.m_Summary = "Virtools Default Rendering Engine";
    return &g_PluginInfo;
}

void ReleaseRasterizers() {
    const int count = g_RasterizersInfo.Size();
    for (int i = 0; i < count; ++i) {
        VxSharedLibrary sl;
        CKRasterizerInfo &info = g_RasterizersInfo[i];
        sl.Attach(info.DllInstance);
        sl.ReleaseLibrary();
        info.DllInstance = nullptr;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_DllHandle = hModule;
            break;
        case DLL_PROCESS_DETACH:
            ReleaseRasterizers();
            break;
        default:
            break;
    }
    return TRUE;
}