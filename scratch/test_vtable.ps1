$source = @"
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

#pragma comment(lib, "d3d11.lib")

int main() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr)) {
        printf("D3D11CreateDevice failed: 0x%08X\n", hr);
        return 1;
    }
    void** vtable = *reinterpret_cast<void***>(context);
    printf("context vtable: %p\n", vtable);
    for (int i = 0; i < 50; i++) {
        printf("vfunc[%d]: %p\n", i, vtable[i]);
    }
    context->Release();
    device->Release();
    return 0;
}
"@
Set-Content -Path "scratch\test_vtable.cpp" -Value $source
& "cl.exe" /nologo /O2 "scratch\test_vtable.cpp" /Fe:"scratch\test_vtable.exe" /link "d3d11.lib"
if (Test-Path "scratch\test_vtable.exe") {
    & "scratch\test_vtable.exe"
}
