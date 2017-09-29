#include "Shader.h"
#include "../ThrowIfFailed.h"
#include <Engine\Constants.h>
#include <d3dcompiler.h>
#include <iostream>
#include <Windows.h>
#ifdef DEBUG
#define SHADER_COMPILE_FLAGS D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION
#else
#define SHADER_COMPILE_FLAGS NULL
#endif

namespace Graphics
{

    Shader::Shader(ID3D11Device * device, LPCWSTR shaderPath, std::initializer_list<D3D11_INPUT_ELEMENT_DESC> inputDesc, Flags flags)
    {
        inputLayout    = nullptr;
        vertexShader   = nullptr;
        geometryShader = nullptr;
        pixelShader    = nullptr;

        ID3DBlob *vsShader, *gsShader, *psShader, *errorMsg;

        if (flags & VS)
        {
            if FAILED(D3DCompileFromFile(shaderPath, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VS", "vs_5_0", SHADER_COMPILE_FLAGS, NULL, &vsShader, &errorMsg))
            {
                OutputDebugString((char*)errorMsg->GetBufferPointer());
                throw "Failed to compile Vertex Shader";
            }

            ThrowIfFailed(device->CreateVertexShader(vsShader->GetBufferPointer(), vsShader->GetBufferSize(), NULL, &vertexShader));

            if (inputDesc.size() > 0)
            {
                ThrowIfFailed(device->CreateInputLayout(inputDesc.begin(), inputDesc.size(), vsShader->GetBufferPointer(), vsShader->GetBufferSize(), &inputLayout));
            }
        }

        if (flags & VS)
        {
            if FAILED(D3DCompileFromFile(shaderPath, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PS", "ps_5_0", SHADER_COMPILE_FLAGS, NULL, &psShader, &errorMsg))
            {
                OutputDebugString((char*)errorMsg->GetBufferPointer());
                throw "Failed to compile Pixel Shader";
            }

            ThrowIfFailed(device->CreatePixelShader(psShader->GetBufferPointer(), psShader->GetBufferSize(), NULL, &pixelShader));
        }

        if (flags & GS)
        {
            if FAILED(D3DCompileFromFile(shaderPath, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "GS", "gs_5_0", SHADER_COMPILE_FLAGS, NULL, &gsShader, &errorMsg))
            {
                OutputDebugString((char*)errorMsg->GetBufferPointer());
                throw "Failed to compile Geometry Shader";
            }

            ThrowIfFailed(device->CreateGeometryShader(gsShader->GetBufferPointer(), gsShader->GetBufferSize(), NULL, &geometryShader));
        }
    }

    Shader::~Shader()
    {
        SAFE_RELEASE(inputLayout);
        SAFE_RELEASE(vertexShader);
        SAFE_RELEASE(pixelShader);
    }

    ComputeShader::ComputeShader(ID3D11Device * device, LPCWSTR shaderPath)
    {
        ID3DBlob *csShader, *errorMsg;

        HRESULT cshr = D3DCompileFromFile(shaderPath, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CS", "cs_5_0", SHADER_COMPILE_FLAGS, NULL, &csShader, &errorMsg);
        if FAILED(cshr)
        {
            OutputDebugString((char*)errorMsg->GetBufferPointer());
            throw "Failed to compile Compute Shader";
        }

        ThrowIfFailed(device->CreateComputeShader(csShader->GetBufferPointer(), csShader->GetBufferSize(), NULL, &computeShader));
    }


    ComputeShader::~ComputeShader()
    {
        computeShader->Release();
    }
}
