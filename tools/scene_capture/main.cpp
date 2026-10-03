// ckre_scene_capture: reference frame and capability capture tool.
//
//   ckre_scene_capture --render-engine-dir DIR [--driver N] [--scene NAME|all]
//                      [--frames K] [--size WxH] [--out DIR] [--caps-json FILE]
//                      [--native-window-handle] [--hidden] [--verbose]
//                      [--compare REFDIR [--mask-dir DIR] [--threshold T] [--min-pass P]
//                       [--require-all]] [--list-scenes]
//
// With --scene all every scene runs in its own child process (a crashing
// engine takes down one scene, not the run; present_* scenes need a fresh
// CK2_3D.ini per process). Exit codes: 0 ok, 1 usage, 2 boot failure,
// 3 scene failure, 4 capture failure, 5 comparison failure.

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "CaptureApp.h"
#include "ImageIO.h"
#include "SceneRegistry.h"

namespace {

enum ExitCode {
    EXIT_OK = 0,
    EXIT_USAGE = 1,
    EXIT_BOOT = 2,
    EXIT_SCENE = 3,
    EXIT_CAPTURE = 4,
    EXIT_COMPARE = 5,
    EXIT_CANCELLED = 130,
};

struct Args {
    CaptureOptions Capture;
    std::string Scene = "all";
    std::string OutDir = ".";
    std::string CapsJson;
    std::string CompareDir;
    std::string MaskDir;
    int Threshold = -1;       // -1 = per-scene suggestion
    float MinPass = -1.0f;    // -1 = per-scene suggestion
    bool RequireAll = false;
    bool ListScenes = false;
    std::vector<std::string> Skip; // scenes excluded from --scene all
    bool Single = false;      // internal: run the scene in this process
    bool Help = false;
    bool DriverSpecified = false;
    bool ProfileWarmupSpecified = false;
    std::vector<std::string> Raw; // original argv for child processes
};

void PrintUsage()
{
    printf("usage: ckre_scene_capture --render-engine-dir DIR [options]\n"
           "  --render-engine-dir DIR   directory holding CK2_3D.dll and its rasterizer DLLs\n"
           "  --driver N                render driver index (default 0)\n"
           "  --rasterizer NAME         stable provider: sdlgpu, bgfx or null; excludes --driver\n"
           "  --hold-ms N               keep the presented frame visible for desktop inspection\n"
           "  --scene NAME|all          scene to render (default all; see --list-scenes)\n"
           "  --frames K                frames rendered before the capture (default 1)\n"
           "  --frame-delay-ms N        delay between frames, 0..1000 ms (default 0; not profiling)\n"
           "  --capture-frames A,B      also write <scene>.frame-N.png at these one-based frames\n"
           "  --size WxH                render size (default 640x480)\n"
           "  --out DIR                 output directory for <scene>.png (default .)\n"
           "  --caps-json FILE          write the driver's Vx3DCapsDesc / Vx2DCapsDesc as JSON\n"
           "  --profile-json FILE       record render CPU wall time and public rasterizer counters\n"
           "                            requests CK_FRAMERATE_FREE to disable vblank synchronization\n"
           "                            with --scene all, writes FILE-stem.<scene>.FILE-extension\n"
           "  --profile-warmup N        omit N initial frames from profile statistics (default 1)\n"
           "  --profile-interactive-start  wait for Enter in the foreground window before profiling\n"
           "                            Escape or window close cancels the run (exit 130)\n"
           "  --native-window-handle    hand the Win32 HWND to the engine (original Virtools DLLs)\n"
           "  --hidden                  create a hidden window\n"
           "  --no-present-last         do not present the last frame before reading it back\n"
           "  --settings-ini FILE       CK2_3D.ini to load instead of the engine's own (our engine only)\n"
           "  --compare REFDIR          compare captured PNGs with REFDIR/<scene>.png\n"
           "  --mask-dir DIR            optional DIR/<scene>.mask.png (non-zero red = ignored)\n"
           "  --threshold T             per-channel tolerance 0..255 (default per scene)\n"
           "  --min-pass P              required passing pixel ratio 0..1 (default per scene)\n"
           "  --require-all             a missing reference image is a failure\n"
           "  --skip A,B                scenes to leave out of --scene all (known engine limitations)\n"
           "  --list-scenes             print the scene table and exit\n"
           "  --verbose\n");
}

bool ParseArgs(int argc, char **argv, Args &args)
{
    for (int i = 0; i < argc; ++i)
        args.Raw.push_back(argv[i]);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string &out) -> bool {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", a.c_str());
                return false;
            }
            out = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--render-engine-dir") { if (!value(args.Capture.RenderEngineDir)) return false; }
        else if (a == "--driver") { if (!value(v)) return false; args.Capture.Driver = atoi(v.c_str()); args.DriverSpecified = true; }
        else if (a == "--rasterizer") {
            if (!value(args.Capture.Rasterizer)) return false;
            if (args.Capture.Rasterizer != "sdlgpu" && args.Capture.Rasterizer != "bgfx" && args.Capture.Rasterizer != "null") return false;
        }
        else if (a == "--hold-ms") {
            if (!value(v)) return false;
            char *end = NULL;
            const long duration = strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || duration < 0 || duration > 600000) return false;
            args.Capture.HoldMilliseconds = (int)duration;
        }
        else if (a == "--scene") { if (!value(args.Scene)) return false; }
        else if (a == "--frames") { if (!value(v)) return false; args.Capture.Frames = atoi(v.c_str()); }
        else if (a == "--frame-delay-ms") {
            if (!value(v)) return false;
            char *end = NULL;
            errno = 0;
            const long delay = strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || errno == ERANGE || delay < 0 || delay > 1000) return false;
            args.Capture.FrameDelayMilliseconds = (int)delay;
        }
        else if (a == "--capture-frames") {
            if (!value(v) || v.empty()) return false;
            const char *next = v.c_str();
            while (*next) {
                char *end = NULL;
                errno = 0;
                const long frame = strtol(next, &end, 10);
                if (end == next || errno == ERANGE || frame < 1 || frame > 1000000 || (*end && *end != ',')) return false;
                args.Capture.CaptureFrames.push_back((int)frame);
                if (!*end) break;
                next = end + 1;
                if (!*next) return false;
            }
        }
        else if (a == "--size") {
            if (!value(v)) return false;
            if (sscanf(v.c_str(), "%dx%d", &args.Capture.Width, &args.Capture.Height) != 2 ||
                args.Capture.Width <= 0 || args.Capture.Height <= 0) {
                fprintf(stderr, "bad --size %s\n", v.c_str());
                return false;
            }
        }
        else if (a == "--out") { if (!value(args.OutDir)) return false; }
        else if (a == "--caps-json") { if (!value(args.CapsJson)) return false; }
        else if (a == "--profile-json") {
            if (!value(args.Capture.ProfileJson) || args.Capture.ProfileJson.empty()) return false;
        }
        else if (a == "--profile-warmup") {
            if (!value(v)) return false;
            char *end = NULL;
            errno = 0;
            const long count = strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || errno == ERANGE || count < 0 || count >= 1000000) {
                fprintf(stderr, "--profile-warmup must be an integer from 0 to 999999\n");
                return false;
            }
            args.Capture.ProfileWarmup = (int)count;
            args.ProfileWarmupSpecified = true;
        }
        else if (a == "--profile-interactive-start") args.Capture.ProfileInteractiveStart = true;
        else if (a == "--native-window-handle") args.Capture.NativeWindowHandle = true;
        else if (a == "--hidden") args.Capture.HiddenWindow = true;
        else if (a == "--no-present-last") args.Capture.PresentLastFrame = false;
        else if (a == "--settings-ini") { if (!value(args.Capture.SettingsIni)) return false; }
        else if (a == "--compare") { if (!value(args.CompareDir)) return false; }
        else if (a == "--mask-dir") { if (!value(args.MaskDir)) return false; }
        else if (a == "--threshold") { if (!value(v)) return false; args.Threshold = atoi(v.c_str()); }
        else if (a == "--min-pass") { if (!value(v)) return false; args.MinPass = (float)atof(v.c_str()); }
        else if (a == "--require-all") args.RequireAll = true;
        else if (a == "--skip") {
            if (!value(v)) return false;
            size_t start = 0;
            while (start <= v.size()) {
                size_t comma = v.find(',', start);
                if (comma == std::string::npos) comma = v.size();
                if (comma > start) args.Skip.push_back(v.substr(start, comma - start));
                start = comma + 1;
            }
        }
        else if (a == "--list-scenes") args.ListScenes = true;
        else if (a == "--verbose") args.Capture.Verbose = true;
        else if (a == "--single") args.Single = true;
        else if (a == "--help" || a == "-h") args.Help = true;
        else {
            fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    if (args.DriverSpecified && !args.Capture.Rasterizer.empty()) {
        fprintf(stderr, "--driver and --rasterizer are mutually exclusive\n");
        return false;
    }
    for (int frame : args.Capture.CaptureFrames) {
        if (frame > args.Capture.Frames || !args.Capture.PresentLastFrame) {
            fprintf(stderr, "--capture-frames requires presented frames within --frames\n");
            return false;
        }
    }
    args.Capture.CaptureFrameDirectory = args.OutDir;
    if (args.Capture.HoldMilliseconds && (args.Capture.HiddenWindow || !args.Capture.PresentLastFrame)) {
        fprintf(stderr, "--hold-ms requires a visible, presented window\n");
        return false;
    }
    if (args.ProfileWarmupSpecified && args.Capture.ProfileJson.empty()) {
        fprintf(stderr, "--profile-warmup requires --profile-json\n");
        return false;
    }
    if (args.Capture.ProfileInteractiveStart && args.Capture.ProfileJson.empty()) {
        fprintf(stderr, "--profile-interactive-start requires --profile-json\n");
        return false;
    }
    if (!args.Capture.ProfileJson.empty()) {
        if (args.Capture.FrameDelayMilliseconds) {
            fprintf(stderr, "--frame-delay-ms cannot be combined with profiling\n");
            return false;
        }
        if (args.Capture.HiddenWindow || !args.Capture.PresentLastFrame) {
            fprintf(stderr, "--profile-json requires a visible window presented on every frame\n");
            return false;
        }
        if (args.Capture.Frames <= args.Capture.ProfileWarmup || args.Capture.Frames > 1000000) {
            fprintf(stderr, "--profile-json requires --frames greater than --profile-warmup, up to 1000000 frames\n");
            return false;
        }
    }
    return true;
}

std::string JoinPath(const std::string &dir, const std::string &file)
{
    if (dir.empty() || dir == ".")
        return file;
    const char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
}

bool FileExists(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

bool WriteTextFile(const std::string &path, const char *text)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    fputs(text, f);
    fclose(f);
    return true;
}

// Runs one scene in this process. Returns an ExitCode.
int RunSingle(const Args &args, const SceneDef &scene)
{
    CaptureApp app;
    if (!app.Boot(args.Capture))
        return EXIT_BOOT;
    if (!args.CapsJson.empty() && !app.WriteCapsJson(args.CapsJson))
        return EXIT_BOOT;
    printf("[%s] engine=%s driver=%s\n", scene.Name, app.RenderEngineDll().c_str(), app.DriverName().c_str());
    RgbaImage image;
    if (!app.CaptureScene(scene, image)) {
        if (app.Cancelled()) return EXIT_CANCELLED;
        return app.Error().find("failed to build") != std::string::npos ? EXIT_SCENE : EXIT_CAPTURE;
    }
    std::string error;
    const std::string outPath = JoinPath(args.OutDir, std::string(scene.Name) + ".png");
    if (!WritePng(outPath, image, error)) {
        fprintf(stderr, "cannot write %s: %s\n", outPath.c_str(), error.c_str());
        return EXIT_CAPTURE;
    }
    printf("[%s] wrote %s (%dx%d)\n", scene.Name, outPath.c_str(), image.Width, image.Height);
    fflush(stdout);
    app.HoldVisibleFrame();
    app.Shutdown();
    return EXIT_OK;
}

// Only the caps JSON: boot, write, exit.
int RunCapsOnly(const Args &args)
{
    CaptureApp app;
    if (!app.Boot(args.Capture))
        return EXIT_BOOT;
    if (!app.WriteCapsJson(args.CapsJson))
        return EXIT_BOOT;
    printf("wrote %s (engine=%s driver=%s)\n", args.CapsJson.c_str(), app.RenderEngineDll().c_str(), app.DriverName().c_str());
    return EXIT_OK;
}

int RunChild(const Args &args, const SceneDef &scene, const std::string &settingsIni)
{
    std::vector<std::string> argsOut;
    argsOut.push_back(args.Raw[0]);
    // Forward everything except --scene / --caps-json / --compare options,
    // which the parent handles.
    for (size_t i = 1; i < args.Raw.size(); ++i) {
        const std::string &a = args.Raw[i];
        if (a == "--scene" || a == "--caps-json" || a == "--profile-json" || a == "--compare" || a == "--mask-dir" ||
            a == "--threshold" || a == "--min-pass" || a == "--settings-ini") {
            ++i;
            continue;
        }
        if (a == "--require-all" || a == "--list-scenes" || a == "--single")
            continue;
        argsOut.push_back(a);
    }
    argsOut.push_back("--single");
    argsOut.push_back("--scene");
    argsOut.push_back(scene.Name);
    if (!args.Capture.ProfileJson.empty()) {
        std::filesystem::path profile = args.Capture.ProfileJson;
        if (args.Scene == "all") {
            const std::string extension = profile.has_extension() ? profile.extension().string() : ".json";
            profile = profile.parent_path() / (profile.stem().string() + "." + scene.Name + extension);
        }
        argsOut.push_back("--profile-json");
        argsOut.push_back(profile.string());
    }
    if (!settingsIni.empty()) {
        argsOut.push_back("--settings-ini");
        argsOut.push_back(settingsIni);
    }
    std::vector<const char *> argv;
    for (const std::string &s : argsOut)
        argv.push_back(s.c_str());
    argv.push_back(NULL);

    SDL_Process *process = SDL_CreateProcess(argv.data(), false);
    if (!process) {
        fprintf(stderr, "[%s] cannot start child process: %s\n", scene.Name, SDL_GetError());
        return EXIT_CAPTURE;
    }
    int exitCode = EXIT_CAPTURE;
    if (!SDL_WaitProcess(process, true, &exitCode))
        exitCode = EXIT_CAPTURE;
    SDL_DestroyProcess(process);
    if (exitCode != EXIT_OK && exitCode != EXIT_CANCELLED)
        fprintf(stderr, "[%s] child exited with code %d\n", scene.Name, exitCode);
    return exitCode;
}

struct CompareSummary {
    int Compared = 0;
    int Passed = 0;
    int Missing = 0;
    int Failed = 0;
};

void CompareSceneImage(const Args &args, const SceneDef &scene, CompareSummary &summary, const std::string &suffix)
{
    const std::string label = std::string(scene.Name) + suffix;
    const std::string capturedPath = JoinPath(args.OutDir, label + ".png");
    const std::string referencePath = JoinPath(args.CompareDir, label + ".png");
    if (!FileExists(referencePath)) {
        ++summary.Missing;
        printf("[%s] no reference image (%s)%s\n", label.c_str(), referencePath.c_str(),
               args.RequireAll ? " FAIL" : " skipped");
        if (args.RequireAll)
            ++summary.Failed;
        return;
    }
    RgbaImage captured, reference, mask;
    std::string error;
    if (!ReadPng(capturedPath, captured, error)) {
        printf("[%s] cannot read %s: %s FAIL\n", label.c_str(), capturedPath.c_str(), error.c_str());
        ++summary.Failed;
        return;
    }
    if (!ReadPng(referencePath, reference, error)) {
        printf("[%s] cannot read %s: %s FAIL\n", label.c_str(), referencePath.c_str(), error.c_str());
        ++summary.Failed;
        return;
    }
    const RgbaImage *maskPtr = NULL;
    if (!args.MaskDir.empty()) {
        const std::string maskPath = JoinPath(args.MaskDir, std::string(scene.Name) + ".mask.png");
        if (FileExists(maskPath) && ReadPng(maskPath, mask, error))
            maskPtr = &mask;
    }
    const int threshold = args.Threshold >= 0 ? args.Threshold : scene.Threshold;
    const float minPass = args.MinPass >= 0.0f ? args.MinPass : scene.MinPass;
    RgbaImage diff;
    const CompareResult result = CompareImages(captured, reference, threshold, maskPtr, &diff);
    ++summary.Compared;
    if (result.SizeMismatch) {
        printf("[%s] size mismatch %dx%d vs %dx%d FAIL\n", label.c_str(), captured.Width, captured.Height,
               reference.Width, reference.Height);
        ++summary.Failed;
        return;
    }
    const std::string diffPath = JoinPath(args.OutDir, label + ".diff.png");
    WritePng(diffPath, diff, error);
    const bool pass = result.PassRatio() >= minPass;
    printf("[%s] pass %.6f%% (threshold %d, min %.2f%%, max diff %d, failed pixels %lld, masked %lld) %s\n", label.c_str(),
           result.PassRatio() * 100.0, threshold, minPass * 100.0f, result.MaxChannelDiff,
           result.ComparedPixels - result.PassingPixels, result.MaskedPixels, pass ? "OK" : "FAIL");
    if (pass)
        ++summary.Passed;
    else
        ++summary.Failed;
}

void CompareScene(const Args &args, const SceneDef &scene, CompareSummary &summary)
{
    CompareSceneImage(args, scene, summary, "");
    for (int frame : args.Capture.CaptureFrames)
        CompareSceneImage(args, scene, summary, ".frame-" + std::to_string(frame));
}

} // namespace

int main(int argc, char **argv)
{
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
        return EXIT_USAGE;
    }
    if (args.Help) {
        PrintUsage();
        return EXIT_OK;
    }
    if (args.ListScenes) {
        for (int i = 0; i < GetSceneCount(); ++i) {
            const SceneDef &s = GetScene(i);
            printf("%-24s oracle=%s threshold=%d min-pass=%.2f  %s\n", s.Name, s.HasOracle ? "yes" : "no ",
                   s.Threshold, s.MinPass, s.Description);
        }
        return EXIT_OK;
    }
    if (args.Capture.RenderEngineDir.empty()) {
        fprintf(stderr, "--render-engine-dir is required\n");
        PrintUsage();
        return EXIT_USAGE;
    }
    {
        // Captures, diff images and per-scene ini overrides all land in --out.
        std::error_code ec;
        std::filesystem::create_directories(args.OutDir, ec);
    }
    if (!args.Capture.ProfileJson.empty()) {
        const auto directory = std::filesystem::path(args.Capture.ProfileJson).parent_path();
        if (!directory.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec) {
                fprintf(stderr, "cannot create profile directory %s: %s\n", directory.string().c_str(), ec.message().c_str());
                return EXIT_CAPTURE;
            }
        }
    }

    std::vector<const SceneDef *> scenes;
    if (args.Scene == "all") {
        for (int i = 0; i < GetSceneCount(); ++i) {
            const SceneDef &scene = GetScene(i);
            bool skipped = false;
            for (const std::string &name : args.Skip)
                if (name == scene.Name)
                    skipped = true;
            if (skipped)
                printf("[%s] skipped (--skip)\n", scene.Name);
            else
                scenes.push_back(&scene);
        }
    } else {
        const SceneDef *scene = FindScene(args.Scene.c_str());
        if (!scene) {
            fprintf(stderr, "unknown scene '%s' (see --list-scenes)\n", args.Scene.c_str());
            return EXIT_USAGE;
        }
        scenes.push_back(scene);
    }

    if (args.Single) {
        if (scenes.size() != 1) {
            fprintf(stderr, "--single needs exactly one scene\n");
            return EXIT_USAGE;
        }
        return RunSingle(args, *scenes[0]);
    }

    int worst = EXIT_OK;
    if (!args.CapsJson.empty()) {
        const int rc = RunCapsOnly(args);
        if (rc != EXIT_OK)
            return rc;
    }

    // One child process per scene.
    if (!SDL_Init(0)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_BOOT;
    }
    std::vector<int> results(scenes.size(), EXIT_OK);
    for (size_t i = 0; i < scenes.size(); ++i) {
        const SceneDef &scene = *scenes[i];
        std::string settingsIni;
        if (scene.IniOverrides) {
            settingsIni = JoinPath(args.OutDir, std::string(scene.Name) + ".CK2_3D.ini");
            if (!WriteTextFile(settingsIni, scene.IniOverrides)) {
                fprintf(stderr, "[%s] cannot write %s\n", scene.Name, settingsIni.c_str());
                results[i] = EXIT_CAPTURE;
                continue;
            }
        }
        results[i] = RunChild(args, scene, settingsIni);
        if (results[i] == EXIT_CANCELLED) {
            SDL_Quit();
            return EXIT_CANCELLED;
        }
        if (results[i] > worst)
            worst = results[i];
    }
    SDL_Quit();

    if (!args.CompareDir.empty()) {
        CompareSummary summary;
        for (size_t i = 0; i < scenes.size(); ++i) {
            if (results[i] != EXIT_OK) {
                printf("[%s] not captured (exit %d) FAIL\n", scenes[i]->Name, results[i]);
                ++summary.Failed;
                continue;
            }
            CompareScene(args, *scenes[i], summary);
        }
        printf("compare: %d compared, %d passed, %d failed, %d without reference\n", summary.Compared,
               summary.Passed, summary.Failed, summary.Missing);
        if (summary.Failed > 0)
            return EXIT_COMPARE;
    }
    return worst;
}
