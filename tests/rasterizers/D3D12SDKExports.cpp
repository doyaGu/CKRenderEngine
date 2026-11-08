// Select the app-local runtime deployed beside Windows GPU test executables.
extern "C" {
__declspec(dllexport) extern const unsigned int D3D12SDKVersion = CKRE_TEST_D3D12_SDK_VERSION;
__declspec(dllexport) const char *D3D12SDKPath = ".\\D3D12\\";
}
