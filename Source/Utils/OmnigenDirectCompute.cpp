#include "stdafx.h"
#include "OmnigenDirectCompute.h"
#include <d3dcompiler.h>

#define COMPILE_SHADERS 0

ID3D11Device* OmnigenDirectCompute::sD3DDevice = nullptr;
ID3D11DeviceContext* OmnigenDirectCompute::sD3DContext = nullptr;

OmnigenDirectCompute::OmnigenDirectCompute()
{
    // Initialize device
    if (!sD3DDevice)
        initializeDevice();
}

OmnigenDirectCompute::~OmnigenDirectCompute()
{
    safeRelease(&computeShader);
    safeRelease(&sD3DContext);
    safeRelease(&sD3DDevice);
    clearBuffers();
}

void OmnigenDirectCompute::setShader(LPCWSTR shaderFilename, LPCSTR entryPoint)
{
    if (currentShaderName == shaderFilename && currentShaderEntryPoint == entryPoint)
        return;

    currentShaderName = shaderFilename;
    currentShaderEntryPoint = entryPoint;

    ID3DBlob* shaderBytecode = nullptr;
#if COMPILE_SHADERS
    shaderBytecode = compileShader(currentShaderName, currentShaderEntryPoint);
#else
    shaderBytecode = loadPrecompiledShader(currentShaderName);
#endif

    Q_ASSERT(shaderBytecode);
    
    loadComputeShader(shaderBytecode);
    sD3DContext->CSSetShader(computeShader, nullptr, 0);
}

std::wstring OmnigenDirectCompute::getPrecompiledShaderName(LPCWSTR shaderFilename)
{
    std::wstring path = shaderFilename;

    // Remove "hlsl"
    for (int i = 0; i < 4; ++i)
        path.pop_back();

    // Add "cso"
    path += L"cso";

    return path;
}

ID3DBlob* OmnigenDirectCompute::compileShader(LPCWSTR shaderFilename, LPCSTR shaderEntryPoint)
{
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_ALL_RESOURCES_BOUND;
#if defined( _DEBUG )
    dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ID3DBlob* pErrorBlob = nullptr;
    ID3DBlob* pBlob = nullptr;

    HRESULT hr = D3DCompileFromFile(shaderFilename, nullptr, nullptr, shaderEntryPoint, "cs_5_0", dwShaderFlags, NULL, &pBlob, &pErrorBlob);
    if (pErrorBlob)
    {
        OmniLog(FAILED(hr) ? ELoggingLevel::Error : ELoggingLevel::Warn) <<= (const char*)pErrorBlob->GetBufferPointer();
        pErrorBlob->Release();
    }

    if (FAILED(hr))
    {
        OmniLog(ELoggingLevel::Error) <<= "Failed to compile compute shader!";
        if (pBlob)
            pBlob->Release();

        return nullptr;
    }

    std::wstring path = getPrecompiledShaderName(shaderFilename);
    D3DWriteBlobToFile(pBlob, path.data(), true);

    return pBlob;
}

ID3DBlob* OmnigenDirectCompute::loadPrecompiledShader(LPCWSTR shaderFilename)
{
    std::wstring path = getPrecompiledShaderName(shaderFilename);

    ID3DBlob* pBlob = nullptr;
    HRESULT hr = D3DReadFileToBlob(path.data(), &pBlob);
    if (FAILED(hr))
    {
        OmniLog(ELoggingLevel::Error) <<= "Failed to load precompiled compute shader!";
        return nullptr;
    }

    return pBlob;
}

void OmnigenDirectCompute::setReadOnlyBuffers(const std::vector<ConstGPUBufferData>& data)
{
    srcDataGPUBuffers.resize(data.size());
    srcDataGPUBufferViews.resize(data.size());

    for (int i = 0; i < data.size(); ++i)
        createReadOnlyBuffer(data[i], i);

    sD3DContext->CSSetShaderResources(0, data.size(), srcDataGPUBufferViews.data());
}

void OmnigenDirectCompute::setReadWriteBuffers(const std::vector<GPUBufferData>& data)
{
    readWriteBufferData = data;
    destDataGPUBuffers.resize(data.size());
    destDataGPUBufferViews.resize(data.size());

    for (int i = 0; i < data.size(); ++i)
        createReadWriteBuffer(data[i], i);

    sD3DContext->CSSetUnorderedAccessViews(0, data.size(), destDataGPUBufferViews.data(), nullptr);
}

void OmnigenDirectCompute::run(int xthreads, int ythreads, int zthreads)
{
    sD3DContext->Dispatch(xthreads, ythreads, zthreads);
    getShaderOutput();
    clearBuffers();
}

bool OmnigenDirectCompute::createReadOnlyBuffer(const ConstGPUBufferData& data, int idx)
{
    auto& targetBuffer = srcDataGPUBuffers[idx];
    auto& targetBufferView = srcDataGPUBufferViews[idx];

    // First we create a buffer in GPU memory
    D3D11_BUFFER_DESC descGPUBuffer;
    ZeroMemory(&descGPUBuffer, sizeof(descGPUBuffer));
    descGPUBuffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    descGPUBuffer.ByteWidth = data.elementsCount * data.byteStride;
    descGPUBuffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    descGPUBuffer.StructureByteStride = data.byteStride;

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = data.dataPtr;
    if (FAILED(sD3DDevice->CreateBuffer(&descGPUBuffer, &initData, &targetBuffer)))
        return false;

    // Now we create a view on the resource. DX11 requires you to send the data to shaders using a "shader view"
    D3D11_BUFFER_DESC descBuf;
    ZeroMemory(&descBuf, sizeof(descBuf));
    targetBuffer->GetDesc(&descBuf);

    D3D11_SHADER_RESOURCE_VIEW_DESC descView;
    ZeroMemory(&descView, sizeof(descView));
    descView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    descView.BufferEx.FirstElement = 0;

    descView.Format = DXGI_FORMAT_UNKNOWN;
    descView.BufferEx.NumElements = data.elementsCount;

    if (FAILED(sD3DDevice->CreateShaderResourceView(targetBuffer, &descView, &targetBufferView)))
        return false;

    return true;
}

bool OmnigenDirectCompute::createReadWriteBuffer(const GPUBufferData& data, int idx)
{
    auto& targetBuffer = destDataGPUBuffers[idx];
    auto& targetBufferView = destDataGPUBufferViews[idx];

    // Work buffer for the GPU
    D3D11_BUFFER_DESC descGPUBuffer;
    ZeroMemory(&descGPUBuffer, sizeof(descGPUBuffer));
    descGPUBuffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    descGPUBuffer.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    descGPUBuffer.ByteWidth = data.elementsCount * data.byteStride;
    descGPUBuffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    descGPUBuffer.StructureByteStride = data.byteStride;

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = data.dataPtr;
    if (FAILED(sD3DDevice->CreateBuffer(&descGPUBuffer, &initData, &targetBuffer)))
        return false;

    // UAV
    D3D11_BUFFER_DESC descBuf;
    ZeroMemory(&descBuf, sizeof(descBuf));
    targetBuffer->GetDesc(&descBuf);

    D3D11_UNORDERED_ACCESS_VIEW_DESC descView;
    ZeroMemory(&descView, sizeof(descView));
    descView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    descView.Buffer.FirstElement = 0;
    descView.Format = DXGI_FORMAT_UNKNOWN;
    descView.Buffer.NumElements = data.elementsCount;

    if (data.isAppend)
        descView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;

    if (FAILED(sD3DDevice->CreateUnorderedAccessView(targetBuffer, &descView, &targetBufferView)))
        return false;

    return true;
}

bool OmnigenDirectCompute::loadComputeShader(ID3DBlob* shaderBytecode)
{
    HRESULT hr = sD3DDevice->CreateComputeShader(shaderBytecode->GetBufferPointer(), shaderBytecode->GetBufferSize(), nullptr, &computeShader);
    if (FAILED(hr))
    {
        OmniLog(ELoggingLevel::Error) <<= "Failed to create compute shader!";
        return false;
    }

    return true;
}

void OmnigenDirectCompute::getShaderOutput()
{
    for (int i = 0; i < destDataGPUBuffers.size(); ++i)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (sD3DContext->Map(destDataGPUBuffers[i], 0, D3D11_MAP_READ, 0, &mappedResource) != S_OK)
        {
            OmniLog(ELoggingLevel::Error) <<= "Failed to map memory for compute shader output!";
            return;
        }

        memcpy(readWriteBufferData[i].dataPtr, mappedResource.pData, readWriteBufferData[i].elementsCount * readWriteBufferData[i].byteStride);

        sD3DContext->Unmap(destDataGPUBuffers[i], 0);
    }
}

void OmnigenDirectCompute::clearBuffers()
{
    for (auto& p : destDataGPUBuffers)
        safeRelease(&p);

    for (auto& p : destDataGPUBufferViews)
        safeRelease(&p);

    for (auto& p : srcDataGPUBuffers)
        safeRelease(&p);

    for (auto& p : srcDataGPUBufferViews)
        safeRelease(&p);

    destDataGPUBuffers.clear();
    destDataGPUBufferViews.clear();
    srcDataGPUBuffers.clear();
    srcDataGPUBufferViews.clear();
}

bool OmnigenDirectCompute::initializeDevice()
{
    quint64 createDeviceFlags = D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    static quint64 numDriverTypes = ARRAYSIZE(driverTypes);

    static D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1
    };
    static quint64 numFeatureLevels = ARRAYSIZE(featureLevels);

    static D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;
    for (quint64 driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
    {
        D3D_DRIVER_TYPE driverType = driverTypes[driverTypeIndex];
        hr = D3D11CreateDevice(nullptr, driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels, D3D11_SDK_VERSION, &sD3DDevice, &featureLevel, &sD3DContext);
        if (SUCCEEDED(hr))
            break;
    }

    if (FAILED(hr))
    {
        OmniLog(ELoggingLevel::Error) <<= "Failed to create D3D device!";
        return false;
    }

    return true;
}
