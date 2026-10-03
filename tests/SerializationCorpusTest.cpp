// Optional local-corpus test: no behavior execution or asset file writes.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "CKContext.h"
#include "CKFile.h"
#include "CKGlobals.h"
#include "CKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderManager.h"
#include "RCKSprite.h"
#include "RCKTexture.h"
#include "SerializationGridRegistry.h"

extern void InitializeCK2_3D();
extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CorpusFile : public CKFile {
public:
    explicit CorpusFile(CKContext *context) : CKFile(context) {}
    CKERROR ReadChunks(char *path) {
        CKERROR error = OpenFile(path);
        if (error != CK_OK && error != CKERR_PLUGINSMISSING) return error;
        // Only object chunks are needed; do not extract embedded resources.
        m_IncludedFiles.Clear();
        return m_ReadFileDataDone ? CK_OK : ReadFileData(&m_Parser);
    }
};

bool IsRenderClass(CK_CLASSID cid) {
    return CKIsChildClassOf(cid, CKCID_RENDEROBJECT) ||
           CKIsChildClassOf(cid, CKCID_MESH) ||
           CKIsChildClassOf(cid, CKCID_ANIMATION) ||
           cid == CKCID_OBJECTANIMATION || cid == CKCID_MATERIAL ||
           cid == CKCID_TEXTURE || cid == CKCID_KINEMATICCHAIN || cid == CKCID_LAYER;
}

using Snapshot = std::map<CKDWORD, std::vector<CKDWORD>>;

Snapshot Capture(CKObject &object, CKStateChunk &chunk) {
    Snapshot result;
    chunk.StartRead();
    const int *words = static_cast<const int *>(chunk.LockReadBuffer());
    const int wordCount = chunk.GetDataSize() / sizeof(int);
    const bool texture = object.GetClassID() == CKCID_TEXTURE;
    const bool sprite = object.GetClassID() == CKCID_SPRITE;
    for (int pos = 0; pos + 2 <= wordCount;) {
        const CKDWORD id = words[pos];
        const int next = words[pos + 1];
        const int end = next ? next : wordCount;
        if (end < pos + 2 || end > wordCount) throw std::runtime_error("Invalid saved identifier chain");
        // CK2 WriteRawBitmap uses lossy JPEG. Encoded image bytes are not a
        // round-trip equality oracle; retain bitmap dimensions separately.
        const bool imagePayload = (texture && id == CK_STATESAVE_TEXCOMPRESSED) ||
                                  (sprite && id == CK_STATESAVE_SPRITECOMPRESSED);
        // Attribute manager data/core parameters are outside this harness.
        if (!imagePayload && id != CK_STATESAVE_NEWATTRIBUTES) {
            auto &data = result[id];
            data.assign(words + pos + 2, words + end);
            if (texture && id == CK_STATESAVE_USERMIPMAP && !data.empty())
                data.resize(1); // Keep level count; mip images also use JPEG.
            const bool fileNames = (texture && id == CK_STATESAVE_TEXFILENAMES) ||
                                   (sprite && id == CK_STATESAVE_SPRITEFILENAMES);
            const bool movieName = (texture && id == CK_STATESAVE_TEXAVIFILENAME) ||
                                   (sprite && id == CK_STATESAVE_SPRITEAVIFILENAME);
            // WriteString does not initialize its 0-3 alignment bytes.
            size_t offset = fileNames ? 1 : 0;
            const int count = fileNames && !data.empty() ? static_cast<int>(data[0]) : (movieName ? 1 : 0);
            for (int i = 0; i < count; ++i) {
                const size_t size = data.at(offset++);
                const size_t words = (size + 3) / 4;
                if (offset + words > data.size()) throw std::runtime_error("Invalid saved string");
                if (words) std::memset(reinterpret_cast<unsigned char *>(data.data() + offset) + size, 0, words * 4 - size);
                offset += words;
            }
        }
        if (!next) break;
        pos = next;
    }
    if (texture) {
        auto &bitmap = static_cast<RCKTexture &>(object);
        result[0xFFFFFFFFu] = {static_cast<CKDWORD>(bitmap.GetWidth()), static_cast<CKDWORD>(bitmap.GetHeight()),
                               static_cast<CKDWORD>(bitmap.GetSlotCount())};
    } else if (sprite) {
        auto &bitmap = static_cast<RCKSprite &>(object);
        result[0xFFFFFFFFu] = {static_cast<CKDWORD>(bitmap.GetWidth()), static_cast<CKDWORD>(bitmap.GetHeight()),
                               static_cast<CKDWORD>(bitmap.GetSlotCount())};
        // Missing external movie/image resources can leave no slots. There is
        // no meaningful current image index in that state (0 or -1 in CK2).
        if (!bitmap.GetSlotCount() && result.count(CK_STATESAVE_SPRITECURRENTIMAGE))
            result[CK_STATESAVE_SPRITECURRENTIMAGE] = {0xFFFFFFFFu};
    }
    if (CKIsChildClassOf(object.GetClassID(), CKCID_MESH) &&
        result.count(CK_STATESAVE_MESHFLAGS) && result.count(CK_STATESAVE_MESHVERTICES) &&
        result[CK_STATESAVE_MESHVERTICES].size() >= 2 && (result[CK_STATESAVE_MESHVERTICES][1] & 4)) {
        // 0x1002816A rebuilds omitted normals; 0x1001E3E4 adds these two flags.
        // The original DLL therefore need not preserve these bookkeeping bits.
        result[CK_STATESAVE_MESHFLAGS][0] &= ~(VXMESH_GENNORMALS | VXMESH_NORMAL_CHANGED);
    }
    return result;
}

void WriteCapture(FILE *output, const void *data, size_t bytes) {
    if (bytes && std::fwrite(data, 1, bytes, output) != bytes)
        throw std::runtime_error("Cannot write controller capture");
}

void CaptureController(FILE *output, CK_ID sourceId, int slot, CKAnimController &controller) {
    // Export loaded state before Evaluate can populate tangents. This optional
    // local artifact contains corpus data and must not be checked in.
    std::vector<float> times;
    const int count = controller.GetKeyCount();
    const float first = controller.GetKey(0)->TimeStep;
    const float last = controller.GetKey(count - 1)->TimeStep;
    times.push_back(first - 1.0f);
    times.push_back(last + 1.0f);
    for (int sample = 0; sample <= 16; ++sample) {
        const int index = static_cast<int>((static_cast<long long>(count - 1) * sample) / 16);
        const float start = controller.GetKey(index)->TimeStep;
        times.push_back(start);
        if (index + 1 < count) {
            const float end = controller.GetKey(index + 1)->TimeStep;
            times.push_back(end);
            for (float fraction : {0.125f, 0.5f, 0.875f}) times.push_back(start + (end - start) * fraction);
        }
    }
    for (float time : times)
        if (!std::isfinite(time)) throw std::runtime_error("Nonfinite corpus animation time");
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    const int bytes = controller.DumpKeysTo(nullptr);
    if (bytes <= 0 || bytes % 4) throw std::runtime_error("Invalid controller dump size");
    std::vector<CKDWORD> words(bytes / 4);
    controller.DumpKeysTo(words.data());
    CKDWORD header[] = {sourceId, static_cast<CKDWORD>(slot), controller.GetType(), 0,
                        static_cast<CKDWORD>(words.size()), static_cast<CKDWORD>(times.size())};
    const float length = controller.GetLength();
    std::memcpy(&header[3], &length, sizeof(length));
    WriteCapture(output, header, sizeof(header));
    WriteCapture(output, words.data(), bytes);
    for (float time : times) {
        float value[4] = {};
        const CKBOOL success = controller.Evaluate(time, value);
        WriteCapture(output, &time, sizeof(time));
        WriteCapture(output, &success, sizeof(success));
        WriteCapture(output, value, sizeof(value));
    }
    const int savedBytes = controller.DumpKeysTo(nullptr);
    words.resize(savedBytes / 4);
    controller.DumpKeysTo(words.data());
    const CKDWORD savedCount = static_cast<CKDWORD>(words.size());
    WriteCapture(output, &savedCount, sizeof(savedCount));
    WriteCapture(output, words.data(), savedBytes);
}

int Run(char *path, const char *capturePath) {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *gridRegistry = new SerializationGridRegistry(&context);
    CorpusFile file(&context);
    const CKERROR readError = file.ReadChunks(path);
    if (readError != CK_OK) {
        std::printf("FAIL read=%d\n", readError);
        return 1;
    }
    std::unique_ptr<FILE, decltype(&std::fclose)> controllerCapture(nullptr, &std::fclose);
    if (capturePath) {
        controllerCapture.reset(std::fopen(capturePath, "wb"));
        if (!controllerCapture) throw std::runtime_error("Cannot open controller capture");
        const CKDWORD magic[] = {0x434B4145u, 1};
        WriteCapture(controllerCapture.get(), magic, sizeof(magic));
    }
    int tested = 0, failures = 0, skipped = 0;
    std::map<int, int> coverage;
    std::map<CKDWORD, int> controllerCoverage;
    // Create render objects before loading to preserve their mutual links.
    // Core/behavior objects remain unresolved. Layer metadata uses a test registry.
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        if (!entry.Data || !IsRenderClass(entry.ObjectCid)) { ++skipped; continue; }
        entry.ObjPtr = context.CreateObject(entry.ObjectCid, entry.Name);
        if (!entry.ObjPtr) throw std::runtime_error("Cannot create render object");
        entry.CreatedObject = entry.ObjPtr->GetID();
        file.m_ObjectsHashTable.Insert(entry.CreatedObject, i);
    }
    for (CKFileObject &entry : file.m_FileObjects) {
        if (!entry.ObjPtr) continue;
        std::printf("LOAD id=%u class=%d version=%d\n", entry.Object, entry.ObjectCid, entry.Data->GetDataVersion());
        entry.Data->StartRead();
        if (entry.ObjPtr->Load(entry.Data, &file) != CK_OK)
            throw std::runtime_error("Render object Load failed");
    }
    std::set<CKAnimController *> capturedControllers;
    for (CKFileObject &entry : file.m_FileObjects) {
        if (!entry.ObjPtr || entry.ObjectCid != CKCID_OBJECTANIMATION) continue;
        auto *animation = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
        CKAnimController *controllers[] = {animation->GetPositionController(), animation->GetRotationController(),
            animation->GetScaleController(), animation->GetScaleAxisController(), animation->GetMorphController()};
        for (int slot = 0; slot < 5; ++slot) {
            CKAnimController *controller = controllers[slot];
            if (!controller || controller->GetKeyCount() <= 0) continue;
            ++controllerCoverage[controller->GetType()];
            if (controllerCapture && slot != 4 && capturedControllers.insert(controller).second)
                CaptureController(controllerCapture.get(), entry.Object, slot, *controller);
        }
    }
    if (controllerCapture) return 0;
    for (CKFileObject &entry : file.m_FileObjects)
        if (entry.ObjPtr) entry.ObjPtr->PostLoad();

    std::vector<CK_ID> sourceIds;
    for (CKFileObject &entry : file.m_FileObjects) {
        sourceIds.push_back(entry.Object);
        // Save-side membership checks use Object, not CreatedObject.
        entry.Object = entry.CreatedObject;
        entry.CleanData();
    }
    gridRegistry->PreSave();
    std::vector<Snapshot> snapshots;
    for (CKFileObject &entry : file.m_FileObjects) {
        // Model CKFile::EndSave: Data exists only after each object's Save.
        // ObjectAnimation relies on this to choose a previously saved owner.
        entry.Data = entry.ObjPtr ? entry.ObjPtr->Save(&file, CK_STATESAVE_ALL) : nullptr;
        if (entry.ObjPtr && !entry.Data) throw std::runtime_error("Render object Save failed");
        snapshots.push_back(entry.ObjPtr ? Capture(*entry.ObjPtr, *entry.Data) : Snapshot{});
    }
    // File loads target fresh objects. Reusing an existing object is not a
    // valid idempotence oracle for types that append state in the original DLL.
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        if (!entry.ObjPtr) continue;
        entry.ObjPtr = context.CreateObject(entry.ObjectCid, entry.Name);
        if (!entry.ObjPtr) throw std::runtime_error("Cannot create reload target");
        entry.CreatedObject = entry.ObjPtr->GetID();
        entry.Object = entry.CreatedObject;
        file.m_ObjectsHashTable.Insert(entry.CreatedObject, i);
    }
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        if (!entry.ObjPtr) continue;
        std::printf("RELOAD id=%u class=%d\n", sourceIds[i], entry.ObjectCid);
        entry.Data->StartRead();
        if (entry.ObjPtr->Load(entry.Data, &file) != CK_OK) throw std::runtime_error("Reload failed");
    }
    for (CKFileObject &entry : file.m_FileObjects)
        if (entry.ObjPtr) entry.ObjPtr->PostLoad();

    for (CKFileObject &entry : file.m_FileObjects)
        entry.CleanData();
    gridRegistry->PreSave();
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        if (!entry.ObjPtr) continue;
        const auto &expected = snapshots[i];
        entry.Data = entry.ObjPtr->Save(&file, CK_STATESAVE_ALL);
        if (!entry.Data) throw std::runtime_error("Second Save failed");
        const auto actual = Capture(*entry.ObjPtr, *entry.Data);
        if (expected != actual) {
            for (const auto &block : expected) {
                const auto found = actual.find(block.first);
                if (found == actual.end()) {
                    std::printf("FAIL missing id=%u class=%d block=%08X\n", sourceIds[i], entry.ObjectCid, block.first);
                } else if (block.second != found->second) {
                    size_t offset = 0;
                    while (offset < block.second.size() && offset < found->second.size() && block.second[offset] == found->second[offset]) ++offset;
                    std::printf("FAIL unstable id=%u class=%d block=%08X words=%zu,%zu offset=%zu values=%08X,%08X\n",
                                sourceIds[i], entry.ObjectCid, block.first, block.second.size(), found->second.size(), offset,
                                offset < block.second.size() ? block.second[offset] : 0, offset < found->second.size() ? found->second[offset] : 0);
                }
            }
            for (const auto &block : actual)
                if (!expected.count(block.first))
                    std::printf("FAIL added id=%u class=%d block=%08X\n", sourceIds[i], entry.ObjectCid, block.first);
            ++failures;
        }
        ++tested;
        ++coverage[entry.ObjectCid];
    }
    for (const auto &item : coverage)
        std::printf("COVERAGE class=%d count=%d\n", item.first, item.second);
    for (const auto &item : controllerCoverage)
        std::printf("CONTROLLER type=%08X count=%d\n", item.first, item.second);
    std::printf("RESULT tested=%d skipped=%d failures=%d\n", tested, skipped, failures);
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 4 && std::strcmp(argv[2], "--capture-controllers") == 0)) {
        std::fprintf(stderr, "Usage: serialization_corpus_tests <file.nmo|file.cmo|file.vmo> [--capture-controllers output.bin]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessorSpecific_FunctionsPtr();
    if (CKStartUp() != CK_OK) return 1;
    InitializeCK2_3D();
    std::puts("SCOPE render/layer chunks with test type registry and bitmap metadata; excludes core attributes, runtime managers, image pixels and behaviors");
    int result = 1;
    try { result = Run(argv[1], argc == 4 ? argv[3] : nullptr); }
    catch (const std::exception &error) { std::printf("FAIL %s\n", error.what()); }
    CKShutdown();
    return result;
}
