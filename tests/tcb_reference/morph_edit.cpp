#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Synthetic editing fixtures for the exact original Win32 controller ABI.
// Input: payload DWORD count, payload, operation count, 64-byte operations.
struct Operation {
    uint32_t kind;
    int argument;
    float time;
    uint32_t flags; // allocate normals, input positions, input normals, generic AddKey
    float positions[9];
    uint32_t normals[3];
};
struct MorphKey { float time; float *positions; uint32_t *normals; };

int wmain(int argc, wchar_t **argv) {
    static_assert(sizeof(void *) == 4 && sizeof(Operation) == 64, "Win32 fixture ABI");
    if (argc != 3) return 2;
    const std::wstring root = argv[1], bin = root + L"/Bin";
    SetDllDirectoryW(bin.c_str());
    HMODULE core = LoadLibraryExW((bin + L"/CK2.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    HMODULE dll = LoadLibraryExW((root + L"/RenderEngines/CK2_3D.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!core || !dll) return 3;
    wchar_t loaded[32768];
    GetModuleFileNameW(core, loaded, 32768);
    std::fwprintf(stderr, L"CK2: %ls\n", loaded);
    GetModuleFileNameW(dll, loaded, 32768);
    std::fwprintf(stderr, L"Render: %ls\n", loaded);
    FILE *input = nullptr;
    if (_wfopen_s(&input, argv[2], L"rb") || !input) return 4;
    uint32_t words = 0, operationCount = 0;
    if (fread(&words, 4, 1, input) != 1 || words < 3 || words > 10000) return 5;
    std::vector<uint32_t> payload(words);
    if (fread(payload.data(), 4, words, input) != words ||
        fread(&operationCount, 4, 1, input) != 1 || operationCount > 1000) return 5;
    std::vector<Operation> operations(operationCount);
    if (fread(operations.data(), sizeof(Operation), operationCount, input) != operationCount) return 5;
    fclose(input);

    struct KeyframeData { void *slots[5] = {}; float length = 20; int refs = 1; void *owner = nullptr; } data;
    using Factory = void *(__thiscall *)(void *, uint32_t);
    auto factory = reinterpret_cast<Factory>(reinterpret_cast<uintptr_t>(dll) + 0x4A922);
    void *controller = factory(&data, 0x73847810u);
    if (!controller) return 6;
    auto table = *reinterpret_cast<void ***>(controller);
    auto fields = reinterpret_cast<int *>(controller);
    using BufferMethod = int (__thiscall *)(void *, void *);
    using GetKey = MorphKey *(__thiscall *)(void *, int);
    using AddKey = int (__thiscall *)(void *, MorphKey *, int);
    using AddGeneric = int (__thiscall *)(void *, MorphKey *);
    using AddTime = int (__thiscall *)(void *, float, int);
    using IndexMethod = void (__thiscall *)(void *, int);
    auto getKey = reinterpret_cast<GetKey>(table[3]);
    auto dump = reinterpret_cast<BufferMethod>(table[5]);
    if (reinterpret_cast<BufferMethod>(table[6])(controller, payload.data()) != int(words * 4)) return 7;

    std::printf("{\"states\":[");
    for (uint32_t step = 0; step < operationCount; ++step) {
        const auto &op = operations[step];
        std::vector<MorphKey> before;
        for (int i = 0; i < fields[2]; ++i) before.push_back(*getKey(controller, i));
        float positions[9];
        uint32_t normals[3];
        std::memcpy(positions, op.positions, sizeof(positions));
        std::memcpy(normals, op.normals, sizeof(normals));
        MorphKey source = {op.time, (op.flags & 2) ? positions : nullptr, (op.flags & 4) ? normals : nullptr};
        int result = 0, cloneAliases = 0;
        switch (op.kind) {
        case 0:
            if (op.argument < 0 || op.argument > 5) return 8;
            reinterpret_cast<IndexMethod>(table[12])(controller, op.argument);
            break;
        case 1:
            if (fields[5] < 0 || fields[5] > 3) return 8;
            result = (op.flags & 8) ? reinterpret_cast<AddGeneric>(table[2])(controller, &source) :
                reinterpret_cast<AddKey>(table[9])(controller, &source, op.flags & 1);
            break;
        case 2:
            result = reinterpret_cast<AddTime>(table[10])(controller, op.time, op.flags & 1);
            break;
        case 3:
            if (op.argument < 0 || op.argument >= fields[2]) return 8;
            result = (op.flags & 8) ? reinterpret_cast<AddGeneric>(table[2])(controller, getKey(controller, op.argument)) :
                reinterpret_cast<AddKey>(table[9])(controller, getKey(controller, op.argument), op.flags & 1);
            break;
        case 4:
            if (op.argument >= fields[2]) return 8;
            reinterpret_cast<IndexMethod>(table[4])(controller, op.argument);
            break;
        case 5:
            result = (op.flags & 8) ? reinterpret_cast<AddGeneric>(table[2])(controller, nullptr) :
                reinterpret_cast<AddKey>(table[9])(controller, nullptr, op.flags & 1);
            break;
        case 6: {
            // Use a separate loaded source, then destroy it before the dump.
            const int bytes = dump(controller, nullptr);
            std::vector<uint32_t> sourceWords(bytes / 4);
            dump(controller, sourceWords.data());
            KeyframeData cloneData;
            void *copy = factory(&cloneData, 0x73847810u);
            reinterpret_cast<BufferMethod>(table[6])(copy, sourceWords.data());
            using Clone = int (__thiscall *)(void *, void *);
            result = reinterpret_cast<Clone>(table[8])(controller, copy);
            for (int i = 0; i < fields[2]; ++i) {
                const auto *a = getKey(controller, i), *b = getKey(copy, i);
                if (a->positions && a->positions == b->positions) cloneAliases |= 1;
                if (a->normals && a->normals == b->normals) cloneAliases |= 2;
            }
            using Destroy = void (__thiscall *)(void *, unsigned);
            reinterpret_cast<Destroy>(table[0])(copy, 1);
            break;
        }
        default: return 8;
        }
        int aliases = cloneAliases, stable = 1;
        for (int i = 0; i < fields[2]; ++i) {
            const auto *key = getKey(controller, i);
            if (key->positions == positions) aliases |= 1;
            if (key->normals == normals) aliases |= 2;
            for (const auto &old : before) {
                if (op.kind != 6 && old.time == key->time &&
                    (old.positions != key->positions || old.normals != key->normals)) stable = 0;
            }
        }
        // A copied key must survive both caller mutation and caller lifetime.
        for (auto &value : positions) value = -777.0f;
        for (auto &value : normals) value = 0x24681357u;
        // Original default VxVector construction leaves x/y unspecified. Fill
        // newly created, caller-unsupplied positions before serializing them.
        if (fields[2] > int(before.size()) && (op.kind == 2 || (op.kind == 1 && !(op.flags & 2)))) {
            if (fields[5] > 3) return 8;
            auto *key = getKey(controller, result);
            if (key->positions) std::memcpy(key->positions, op.positions, fields[5] * 12);
        }
        std::printf("%s{\"result\":%d,\"keyCount\":%d,\"vertexCount\":%d,\"stable\":%d,\"aliases\":%d,\"storage\":[",
            step ? "," : "", result, fields[2], fields[5], stable, aliases);
        for (int i = 0; i < fields[2]; ++i) {
            const auto *key = getKey(controller, i);
            std::printf("%s%d", i ? "," : "", (key->positions ? 1 : 0) | (key->normals ? 2 : 0));
        }
        std::printf("],\"words\":[");
        // Count-only changes can temporarily exceed the allocated arrays. The
        // fixture restores the original count before requesting another dump.
        if (op.kind != 0 || (op.flags & 1)) {
            const int bytes = dump(controller, nullptr);
            if (bytes < 12 || bytes % 4 || bytes > 400000) return 9;
            std::vector<uint32_t> saved(bytes / 4);
            if (dump(controller, saved.data()) != bytes) return 9;
            for (size_t i = 0; i < saved.size(); ++i) std::printf("%s%u", i ? "," : "", saved[i]);
        }
        std::printf("]}");
    }
    std::puts("]}");
    using Destroy = void (__thiscall *)(void *, unsigned);
    reinterpret_cast<Destroy>(table[0])(controller, 1);
    return 0;
}
