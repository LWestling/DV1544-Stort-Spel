#pragma once
#include "Resources\Shader.h"
#include <Engine\Constants.h>
#include "Utility\ShaderResource.h"
#include <CommonStates.h>
#include "Structs.h"

namespace Graphics
{
    class Menu
    {
    public:
        Menu(ID3D11Device * device, ID3D11DeviceContext * contex);
        ~Menu();

        void drawMenu(ID3D11Device * device, ID3D11DeviceContext * contex, Graphics::MenuInfo * info, ID3D11RenderTargetView * backBuffer);
        
        void unloadTextures();


    private:
        void mapButtons(ID3D11DeviceContext * contex, ButtonInfo * info);
        void createVBuffers(ID3D11Device * device);
        void loadTextures(ID3D11Device * device, ID3D11DeviceContext * contex);
        

        ID3D11ShaderResourceView * menuTexture;
        ID3D11ShaderResourceView * buttonTexture;
        ID3D11Buffer * buttonQuad;
        ID3D11Buffer * menuQuad;

        DirectX::CommonStates * states;

        Shader shader;
        MenuInfo * active;
        bool loaded;
    };
}