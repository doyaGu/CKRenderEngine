#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "CKGlobals.h"
#include "CKPluginManager.h"

#include "CKException.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCKKinematicChain.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKMesh.h"
#include "RCKPatchMesh.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"
#include "RCKLayer.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKCurvePoint.h"
#include "RCKCurve.h"
#include "RCK3dObject.h"
#include "RCKSprite3D.h"
#include "RCKCharacter.h"
#include "RCKPlace.h"
#include "RCKGrid.h"
#include "RCKBodyPart.h"
#include "RCKTargetCamera.h"
#include "RCKTargetLight.h"
#include "RCKSprite.h"
#include "RCKSpriteText.h"
#include "CKDebugLogger.h"
#include "CKRasterizer.h"

#define VIRTOOLS_RENDERENGIEN_GUID CKGUID(0xAABCF63, 0)

// Debug logging macros (core)
#define CK_DEBUG_LOG(msg) CK_LOG("Core", msg)
#define CK_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("Core", fmt, __VA_ARGS__)

extern CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizer *rst);

void (*g_BuildFaceNormalsFunc)(CKFace *, unsigned short *, int, VxVertex *, int);
void (*g_BuildNormalsFunc)(CKFace *, unsigned short *, int, VxVertex *, int);
int (*g_RayIntersection)(RCKMesh *, VxVector &, VxVector &, VxIntersectionDesc *, CK_RAYINTERSECTION, VxMatrix const &);
void (*g_NormalizeFunc)(VxVertex *, int);

HMODULE g_DllHandle = nullptr;
CKBOOL g_EnumerationDone = FALSE;
CKPluginInfo g_PluginInfo;

// Exported for use by RCKRenderManager
XClassArray<CKRasterizerInfo> g_RasterizersInfo;

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

    CKRST_GETINFO getInfoFunc = (CKRST_GETINFO) sl.GetFunctionPtr((CKSTRING) "CKRasterizerGetInfo");
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

        CK_DEBUG_LOG_FMT("EnumerateRasterizers: Searching in directory: %s", dir.Str());

        // Search for DX8 rasterizers
        CKDirectoryParser dp8(dir.Str(), (CKSTRING) "*DX8Rasterizer.dll", TRUE);
        char *file = dp8.GetNextFile();
        while (file != nullptr) {
            CK_DEBUG_LOG_FMT("Found DX8 rasterizer: %s", file);
            RegisterRasterizer(file);
            file = dp8.GetNextFile();
        }

        // Search for DX9 rasterizers
        CKDirectoryParser dp9(dir.Str(), (CKSTRING) "*DX9Rasterizer.dll", TRUE);
        file = dp9.GetNextFile();
        while (file != nullptr) {
            CK_DEBUG_LOG_FMT("Found DX9 rasterizer: %s", file);
            RegisterRasterizer(file);
            file = dp9.GetNextFile();
        }

        CK_DEBUG_LOG_FMT("EnumerateRasterizers: Found %d rasterizers", g_RasterizersInfo.Size());

        if (g_RasterizersInfo.Size() == 0) {
            CK_DEBUG_LOG("EnumerateRasterizers: No rasterizers found, using NULL rasterizer");
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
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT)
    CKCLASSREGISTERCID(RCKKinematicChain, CKCID_OBJECT)
    CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH)
    CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT)
    CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION)
    CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT)
    CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT)
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT)
    CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT)
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT)
    CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY)
    CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT)
    CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA)
    CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT)
    CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY)
    CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE)
    CKBuildClassHierarchyTable();
}


void SetProcessorSpecific_FunctionsPtr() {
}

CKERROR InitInstanceFct(CKContext *context) {
    CK_DEBUG_LOG("InitInstanceFct: Starting");
    new RCKRenderManager(context);
    CK_DEBUG_LOG("InitInstanceFct: RCKRenderManager created");
    return CK_OK;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo() {
    CK_DEBUG_LOG("CKGetPluginInfo: Starting");
    EnumerateRasterizers();
    CK_DEBUG_LOG("CKGetPluginInfo: Rasterizers enumerated");
    InitializeCK2_3D();
    CK_DEBUG_LOG("CKGetPluginInfo: CK2_3D initialized");
    SetProcessorSpecific_FunctionsPtr();

    g_PluginInfo.m_Author = "Virtools";
    g_PluginInfo.m_Description = "Default Render Engine";
    g_PluginInfo.m_Extension = (CKSTRING) "";
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
        CKInstallExceptionHandler();
        CK_DEBUG_LOG("DLL_PROCESS_ATTACH - CK2_3D.dll loaded with exception handler");
        break;
    case DLL_PROCESS_DETACH:
        CK_DEBUG_LOG("DLL_PROCESS_DETACH - CK2_3D.dll unloading");
        CKRemoveExceptionHandler();
        ReleaseRasterizers();
        break;
    default:
        break;
    }
    return TRUE;
}
