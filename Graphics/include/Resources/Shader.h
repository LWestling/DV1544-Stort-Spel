#pragma once
#include <d3d11.h>
#include <initializer_list>

namespace Graphics
{
    class Shader
    {
    public:
        typedef size_t Flags;
        enum ShaderType
        {
            VS = 1 << 0,
            PS = 1 << 1,
            GS = 1 << 2
        };

        Shader(ID3D11Device * device, LPCWSTR shaderPath, std::initializer_list<D3D11_INPUT_ELEMENT_DESC> inputDesc = {}, Flags = VS | PS);
        virtual ~Shader();

        inline operator ID3D11InputLayout*()    { return inputLayout    ? inputLayout    : throw "Shader has no Input Layout";    }
		inline operator ID3D11VertexShader*()   { return vertexShader   ? vertexShader   : throw "Shader has no Vertex Shader";   }
		inline operator ID3D11GeometryShader*() { return geometryShader ? geometryShader : throw "Shader has no Geometry Shader"; }
		inline operator ID3D11PixelShader*()    { return pixelShader    ? pixelShader    : throw "Shader has no Pixel Shader";    }

    private:
        ID3D11InputLayout  * inputLayout;
		ID3D11VertexShader * vertexShader;
		ID3D11GeometryShader * geometryShader;
		ID3D11PixelShader  * pixelShader;
    };

    class ComputeShader
    {
    public:
        ComputeShader(ID3D11Device * device, LPCWSTR shaderPath);
        virtual ~ComputeShader();

        //void setShader(ID3D11DeviceContext * deviceContext);
        inline operator ID3D11ComputeShader*() { return computeShader; };
    private:
        ID3D11ComputeShader * computeShader;
    };
}