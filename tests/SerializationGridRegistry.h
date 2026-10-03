#pragma once

#include <string>
#include <vector>

#include "CKContext.h"
#include "CKGridManager.h"

// Test-only registry for metadata carried in Layer chunks. This is not a
// runtime GridManager: classification, spatial queries and manager chunks are
// deliberately outside the serialization harness.
class SerializationGridRegistry : public CKGridManager {
public:
    explicit SerializationGridRegistry(CKContext *context)
        : CKGridManager(context, GRID_MANAGER_GUID, "SerializationGridRegistry"), types(1) {
        m_Remap = nullptr;
        m_RemapCount = 1;
        context->RegisterNewManager(this);
    }
    int GetTypeFromName(CKSTRING name) override {
        for (size_t i = 1; i < types.size(); ++i)
            if (types[i].name == (name ? name : "")) return static_cast<int>(i);
        return 0;
    }
    CKSTRING GetTypeName(int type) override { return types.at(type).name.data(); }
    CKERROR SetTypeName(int type, CKSTRING name) override {
        types.at(type).name = name ? name : "";
        return CK_OK;
    }
    int RegisterType(CKSTRING name) override {
        const int existing = GetTypeFromName(name);
        if (existing) return existing;
        Type type;
        type.name = name ? name : "";
        types.push_back(type);
        return static_cast<int>(types.size() - 1);
    }
    int UnRegisterType(CKSTRING) override { return 0; }
    CKERROR SetAssociatedParam(int type, CKGUID guid) override { types.at(type).parameter = guid; return CK_OK; }
    CKGUID GetAssociatedParam(int type) override { return types.at(type).parameter; }
    CKERROR SetAssociatedColor(int type, VxColor *value) override { types.at(type).color = *value; return CK_OK; }
    CKERROR GetAssociatedColor(int type, VxColor *value) override { *value = types.at(type).color; return CK_OK; }
    int GetLayerTypeCount() override { return static_cast<int>(types.size()); }
    int GetClassificationFromName(CKSTRING) override { return 0; }
    CKSTRING GetClassificationName(int) override { return nullptr; }
    int RegisterClassification(CKSTRING) override { return 0; }
    int GetGridClassificationCategory() override { return 0; }
    const XObjectPointerArray &GetGridArray(int) override { return grids; }
    CKGrid *GetNearestGrid(VxVector *, CK3dEntity *) override { return nullptr; }
    CKGrid *GetPreferredGrid(VxVector *, CK3dEntity *) override { return nullptr; }
    CKBOOL IsInGrid(CKGrid *, VxVector *, CK3dEntity *) override { return FALSE; }
    int GetGridObjectCount(int) override { return 0; }
    CKGrid *GetGridObject(int, int) override { return nullptr; }
    void FillGridWithObjectShape(CK3dEntity *, int, void *) override {}
    void FillGridWithObjectShape(CK3dEntity *, int, int, void *) override {}
    void Init() override {}
    CKERROR OnCKInit() override { return CK_OK; }
    CKERROR PostClearAll() override { return CK_OK; }
    CKERROR PostLoad() override { return CK_OK; }
    CKERROR PreSave() override {
        remap.assign(types.size(), 0);
        m_Remap = remap.data();
        m_RemapCount = 1;
        return CK_OK;
    }
    CKERROR OnCKReset() override { return CK_OK; }
    CKERROR PreProcess() override { return CK_OK; }
    CKDWORD GetValidFunctionsMask() override { return 0; }
    CKERROR LoadData(CKStateChunk *, CKFile *) override { return CK_OK; }
    CKStateChunk *SaveData(CKFile *) override { return nullptr; }
    void ClearData() override {}
    void InitData() override {}

private:
    struct Type {
        std::string name;
        CKGUID parameter = CKGUID(0, 0);
        VxColor color = VxColor(0.0f, 0.0f, 0.0f, 0.0f);
    };
    std::vector<Type> types;
    std::vector<int> remap;
    XObjectPointerArray grids;
};
