#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cwchar>

// Local reference runner for the exact CK2_3D.dll analyzed in IDA.
// Input: type, length, payload DWORD count, controller payload, sample count, times.
// Morph inputs append vertex count, byte stride and output mask (1: vertices, 2: normals).
int wmain(int argc, wchar_t **argv) {
    static_assert(sizeof(void *) == 4, "Original controller ABI is Win32");
    if (argc != 3 && (argc != 4 || std::wcscmp(argv[3], L"--dump") != 0)) return 2;
    std::wstring root = argv[1], bin = root + L"/Bin";
    SetDllDirectoryW(bin.c_str());
    HMODULE core = LoadLibraryExW((bin + L"/CK2.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    HMODULE dll = LoadLibraryExW((root + L"/RenderEngines/CK2_3D.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!core || !dll) { std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 3; }
    wchar_t loaded[32768];
    GetModuleFileNameW(core, loaded, 32768);
    std::fwprintf(stderr, L"CK2: %ls\n", loaded);
    GetModuleFileNameW(dll, loaded, 32768);
    std::fwprintf(stderr, L"Render: %ls\n", loaded);
    FILE *input = nullptr;
    if (_wfopen_s(&input, argv[2], L"rb") || !input) return 4;
    uint32_t type = 0, words = 0, samples = 0;
    float length = 0;
    if (fread(&type, 4, 1, input) != 1 || fread(&length, 4, 1, input) != 1 || fread(&words, 4, 1, input) != 1 || words > 1000000) return 5;
    std::vector<uint32_t> payload(words);
    if (fread(payload.data(), 4, words, input) != words || fread(&samples, 4, 1, input) != 1 || samples > 100000) return 5;
    std::vector<float> times(samples);
    if (fread(times.data(), 4, samples, input) != samples) return 5;
    const bool morph = type == 0x73847810u;
    uint32_t vertexCount = 0, vertexStride = 0, outputMask = 0;
    if (morph) {
        if (words < 3 || fread(&vertexCount, 4, 1, input) != 1 ||
            fread(&vertexStride, 4, 1, input) != 1 || fread(&outputMask, 4, 1, input) != 1 ||
            vertexCount > payload[1] || vertexCount > 1000 || vertexStride < 12 ||
            vertexStride > 128 || vertexStride % 4 || outputMask > 3 ||
            ((outputMask & 2) && !payload[2] && payload[0])) return 5;
    }
    fclose(input);
    struct KeyframeData { void *slots[5] = {}; float length; int refs = 1; void *owner = nullptr; } data;
    data.length = length;
    using Factory = void *(__thiscall *)(void *, uint32_t);
    auto factory = reinterpret_cast<Factory>(reinterpret_cast<uintptr_t>(dll) + 0x4A922);
    void *controller = factory(&data, type);
    if (!controller) return 6;
    auto table = *reinterpret_cast<void ***>(controller);
    using Read = int (__thiscall *)(void *, void *);
    using Evaluate = int (__thiscall *)(void *, float, void *);
    using Destroy = void (__thiscall *)(void *, unsigned);
    const int consumed = reinterpret_cast<Read>(table[6])(controller, payload.data());
    if (consumed != static_cast<int>(words * 4)) return 7;
    std::printf("{\"type\":%u,\"samples\":[", type);
    for (uint32_t i = 0; i < samples; ++i) {
        if (morph) {
            // Include guards and stride padding in the captured output.
            std::vector<float> vertices(vertexCount * vertexStride / 4 + 8, -12345.0f);
            std::vector<uint32_t> normals(vertexCount + 2, 0x13572468u);
            using EvaluateMorph = int (__thiscall *)(void *, float, int, void *, uint32_t, void *);
            const int success = reinterpret_cast<EvaluateMorph>(table[11])(
                controller, times[i], vertexCount, (outputMask & 1) ? vertices.data() + 4 : nullptr,
                vertexStride, (outputMask & 2) ? normals.data() + 1 : nullptr);
            std::printf("%s{\"time\":%.9g,\"success\":%d,\"vertices\":[", i ? "," : "", times[i], success);
            for (size_t j = 0; j < vertices.size(); ++j)
                std::printf("%s%.9g", j ? "," : "", vertices[j]);
            std::printf("],\"normals\":[");
            for (size_t j = 0; j < normals.size(); ++j)
                std::printf("%s%u", j ? "," : "", normals[j]);
            std::printf("]}");
            continue;
        }
        float result[4] = {};
        if (!reinterpret_cast<Evaluate>(table[1])(controller, times[i], result)) return 8;
        std::printf("%s{\"time\":%.9g,\"value\":[%.9g,%.9g,%.9g,%.9g]}",
            i ? "," : "", times[i], result[0], result[1], result[2], result[3]);
    }
    std::printf("]");
    if (argc == 4) {
        using Dump = int (__thiscall *)(void *, void *);
        auto dump = reinterpret_cast<Dump>(table[5]);
        const int bytes = dump(controller, nullptr);
        if (bytes < 4 || bytes % 4 || bytes > 4000000) return 9;
        std::vector<uint32_t> saved(bytes / 4);
        if (dump(controller, saved.data()) != bytes) return 9;
        std::printf(",\"serialized\":[");
        for (size_t i = 0; i < saved.size(); ++i)
            std::printf("%s%u", i ? "," : "", saved[i]);
        std::printf("]");
        if (type == 0x921AB801u || type == 0x18AB4404u) {
            using GetKey = void *(__thiscall *)(void *, int);
            std::printf(",\"tangents\":[");
            for (uint32_t i = 0; i < payload[0]; ++i) {
                auto key = reinterpret_cast<const float *>(reinterpret_cast<GetKey>(table[3])(controller, i));
                std::printf("%s[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]", i ? "," : "",
                            key[5], key[6], key[7], key[8], key[9], key[10]);
            }
            std::printf("]");
        }
    }
    std::puts("}");
    reinterpret_cast<Destroy>(table[0])(controller, 1);
    return 0;
}
