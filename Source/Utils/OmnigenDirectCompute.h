#pragma once
#include <d3d11.h>

struct GPUBufferData
{
    void* dataPtr = nullptr;
    quint64 elementsCount = 0;
    quint64 byteStride = 0;
    bool isAppend = false;
};

struct ConstGPUBufferData
{
    const void* dataPtr = nullptr;
    quint64 elementsCount = 0;
    quint64 byteStride = 0;
};

class OmnigenDirectCompute
{
public:
    OmnigenDirectCompute();
    ~OmnigenDirectCompute();
    void setShader(LPCWSTR shaderFilename, LPCSTR entryPoint = "main");
    void setReadOnlyBuffers(const std::vector<ConstGPUBufferData>& data);
    void setReadWriteBuffers(const std::vector<GPUBufferData>& data);
    void run(int xthreads, int ythreads, int zthreads);

private:
    std::wstring getPrecompiledShaderName(LPCWSTR shaderFilename);
    ID3DBlob* compileShader(LPCWSTR shaderFilename, LPCSTR shaderEntryPoint);
    ID3DBlob* loadPrecompiledShader(LPCWSTR shaderFilename);
    bool createReadOnlyBuffer(const ConstGPUBufferData& data, int idx);
    bool createReadWriteBuffer(const GPUBufferData& data, int idx);
    bool loadComputeShader(ID3DBlob* shaderBytecode);
    void getShaderOutput();
    void clearBuffers();

    static bool initializeDevice();

    template <class T> 
    static inline void safeRelease(T** ppT)
    {
        if (*ppT)
        {
            (*ppT)->Release();
            *ppT = NULL;
        }
    }

    // Core
    static ID3D11Device* sD3DDevice;
    static ID3D11DeviceContext* sD3DContext;

    std::vector<ID3D11Buffer*> srcDataGPUBuffers;
    std::vector<ID3D11ShaderResourceView*> srcDataGPUBufferViews;

    std::vector<ID3D11Buffer*> destDataGPUBuffers;
    std::vector<ID3D11UnorderedAccessView*> destDataGPUBufferViews;

    LPCWSTR currentShaderName = nullptr;
    LPCSTR currentShaderEntryPoint = nullptr;
    ID3D11ComputeShader* computeShader = nullptr;

    // Data
    std::vector<GPUBufferData> readWriteBufferData;
};