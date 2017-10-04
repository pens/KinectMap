#define WIN32_LEAN_AND_MEAN  
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <kinect.h>
using namespace std;
using namespace DirectX;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void Start(HWND hwnd);
void Run();
void Stop();
void HandleInput(MSG msg);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, 
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    LPCWSTR name = L"KinectMap";
    WNDCLASSW wc{};
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = name;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(name, name, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                             CW_USEDEFAULT, 0, 0, 0, hInstance, 0);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    Start(hwnd);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            HandleInput(msg);
        }
        Run();
    }
    Stop();
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

IDXGISwapChain* swapChain;
ID3D11Device* device;
ID3D11DeviceContext* context;
ID3D11UnorderedAccessView* backBuffer;
ID3D11ComputeShader* cs;

IKinectSensor* sensor;
IMultiSourceFrameReader* frameReader;
int depthHeight;
int depthWidth;
ID3D11ShaderResourceView* depthSRV;
ID3D11Texture2D* depthTex;
ID3D11ShaderResourceView* depthToColorSRV;
ID3D11Texture2D* depthToColorTex;
ID3D11ShaderResourceView* colorSRV;
ID3D11Texture2D* colorTex;
ID3D11ShaderResourceView* infraredSRV;
ID3D11Texture2D* infraredTex;
ID3D11Buffer* csBuffer;
float vFOV;
float hFOV;

__declspec(align(16)) struct CSBuf {
    int mode = 0;
    XMFLOAT3 lightDir{0,0,.25};
} csbuf;
int modeMax = 9;

void Start(HWND hwnd) {
    RECT window;
    GetClientRect(hwnd, &window);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = true;
    scd.BufferCount = 1;
    scd.BufferDesc.Height = 1080;
    scd.BufferDesc.Width = 1920;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_UNORDERED_ACCESS;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_DEBUG,
                                  0, 0, D3D11_SDK_VERSION, &scd, &swapChain, &device, 0, &context);

    D3D11_VIEWPORT vp{};
    vp.Height = window.bottom;
    vp.Width = window.right;
    vp.MaxDepth = 1;
    context->RSSetViewports(1, &vp);

    ID3D11Texture2D* backBufTex;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBufTex);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(backBufTex, &uavd, &backBuffer);
    backBufTex->Release();

    ID3DBlob* blob;
    D3DReadFileToBlob(L"../x64/Debug/CS.cso", &blob);
    device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), 0, &cs);
    blob->Release();

    context->CSSetShader(cs, 0, 0);
    context->CSSetUnorderedAccessViews(0, 1, &backBuffer, 0);

    GetDefaultKinectSensor(&sensor);
    sensor->Open();

    sensor->OpenMultiSourceFrameReader(FrameSourceTypes_Depth | FrameSourceTypes_Color | FrameSourceTypes_Infrared, &frameReader);
    IDepthFrameSource* depthSrc;
    sensor->get_DepthFrameSource(&depthSrc);
    IFrameDescription* frameDesc;
    depthSrc->get_FrameDescription(&frameDesc);
    frameDesc->get_Height(&depthHeight);
    frameDesc->get_Width(&depthWidth);
    frameDesc->Release();
    depthSrc->Release();
    IColorFrameSource* colorSrc;
    sensor->get_ColorFrameSource(&colorSrc);
    colorSrc->get_FrameDescription(&frameDesc);
    frameDesc->get_HorizontalFieldOfView(&hFOV);
    frameDesc->get_VerticalFieldOfView(&vFOV);
	colorSrc->Release();
    
    D3D11_TEXTURE2D_DESC td{};
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.Width = depthWidth;
    td.Height = depthHeight;
    td.Format = DXGI_FORMAT_R16_UNORM;
    device->CreateTexture2D(&td, 0, &depthTex);
    device->CreateShaderResourceView(depthTex, 0, &depthSRV);

    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.Width = 1920;
    td.Height = 1080;
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    device->CreateTexture2D(&td, 0, &depthToColorTex);
    device->CreateShaderResourceView(depthToColorTex, 0, &depthToColorSRV);

    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.Width = 1920;
    td.Height = 1080;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateTexture2D(&td, 0, &colorTex);
    device->CreateShaderResourceView(colorTex, 0, &colorSRV);

    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.Width = depthWidth;
    td.Height = depthHeight;
    td.Format = DXGI_FORMAT_R16_UNORM;
    device->CreateTexture2D(&td, 0, &infraredTex);
    device->CreateShaderResourceView(infraredTex, 0, &infraredSRV);

    context->CSSetShaderResources(0, 1, &depthSRV);
    context->CSSetShaderResources(1, 1, &depthToColorSRV);
    context->CSSetShaderResources(2, 1, &colorSRV);
    context->CSSetShaderResources(3, 1, &infraredSRV);

    D3D11_BUFFER_DESC bd{};
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA srd{};
    bd.ByteWidth = sizeof(csbuf);
    srd.pSysMem  = &csbuf;
    device->CreateBuffer(&bd, &srd, &csBuffer);

    context->CSSetConstantBuffers(0, 1, &csBuffer);
}

void Run() {
    if (csbuf.lightDir.x > 1)
        csbuf.lightDir.x = -1;
    else
        csbuf.lightDir.x += .0000001f;

    IMultiSourceFrame* multiFrame;
    HRESULT hrr = frameReader->AcquireLatestFrame(&multiFrame);
    if (!multiFrame) return;

    IDepthFrameReference* depthRef;
    multiFrame->get_DepthFrameReference(&depthRef);
    IDepthFrame* depthFrame;
    depthRef->AcquireFrame(&depthFrame);
    depthRef->Release();

    IInfraredFrameReference* infraredRef;
    multiFrame->get_InfraredFrameReference(&infraredRef);
    IInfraredFrame* infraredFrame;
    infraredRef->AcquireFrame(&infraredFrame);
    infraredRef->Release();

    IColorFrameReference* colorRef;
    multiFrame->get_ColorFrameReference(&colorRef);
    IColorFrame* colorFrame;
    colorRef->AcquireFrame(&colorFrame);
    colorRef->Release();

	if (!depthFrame || !colorFrame || !infraredFrame) {
		depthFrame && depthFrame->Release();
		colorFrame && colorFrame->Release();
		infraredFrame && infraredFrame->Release();
		return;
	}
    unsigned int capacity;
    unsigned short* buffer;
    depthFrame->AccessUnderlyingBuffer(&capacity, &buffer);

    DepthSpacePoint* csPoints = new DepthSpacePoint[1920 * 1080];
    ICoordinateMapper* coordMapper;
    sensor->get_CoordinateMapper(&coordMapper);
    HRESULT hr = coordMapper->MapColorFrameToDepthSpace(depthWidth * depthHeight,
                                                        buffer,
                                                        1920 * 1080,
                                                        csPoints);
    D3D11_MAPPED_SUBRESOURCE msr{};
    hr = context->Map(depthToColorTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    memcpy(msr.pData, csPoints, 1920 * 1080 * sizeof(DepthSpacePoint));
    msr.RowPitch = 1920 * sizeof(DepthSpacePoint);
    context->Unmap(depthToColorTex, 0);
    delete[] csPoints;

    context->Map(depthTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    memcpy(msr.pData, buffer, capacity * sizeof(short));
    msr.RowPitch = depthWidth * sizeof(short);
    context->Unmap(depthTex, 0);
    depthFrame->Release();

    infraredFrame->AccessUnderlyingBuffer(&capacity, &buffer);
    context->Map(infraredTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    memcpy(msr.pData, buffer, capacity * sizeof(short));
    msr.RowPitch = depthWidth * sizeof(short);
    context->Unmap(infraredTex, 0);
    infraredFrame->Release();
	
    ColorImageFormat cif;
    colorFrame->get_RawColorImageFormat(&cif);

    unsigned char* buff = new unsigned char[1920 * 1080 * 4];
    hr = colorFrame->CopyConvertedFrameDataToArray(1920 * 1080 * 4, buff, ColorImageFormat_Rgba);
    context->Map(colorTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    memcpy(msr.pData, buff, 1920 * 1080 * 4);
    msr.RowPitch = 1920 * 4;
    context->Unmap(colorTex, 0);
    colorFrame->Release();
    delete[] buff;

    context->Map(csBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    memcpy(msr.pData, &csbuf, sizeof(csbuf));
    context->Unmap(csBuffer, 0);

    context->Dispatch(120, 68, 1);
    swapChain->Present(0, 0);

	multiFrame->Release();
}

void Stop() {
    sensor->Close();
}

void HandleInput(MSG msg) {
    if (msg.message == WM_KEYDOWN) {
        if (msg.wParam == 'W') {
            ++csbuf.mode;
            if (csbuf.mode > modeMax)
                csbuf.mode = 0;
        } else if (msg.wParam == 'S') {
            --csbuf.mode;
            if (csbuf.mode < 0)
                csbuf.mode = modeMax;
        }

        D3D11_MAPPED_SUBRESOURCE msr{};
        context->Map(csBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
        memcpy(msr.pData, &csbuf, sizeof(csbuf));
        context->Unmap(csBuffer, 0);
    }
}