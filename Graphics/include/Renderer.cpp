#include <Graphics\include\Renderer.h>
#include <stdio.h>
#include <Graphics\include\ThrowIfFailed.h>
#include <Engine\Constants.h>

#include <Engine\Profiler.h>

#define USE_TEMP_CUBE false
#define ANIMATION_HIJACK_RENDER false

#if USE_TEMP_CUBE
#include "TempCube.h"
#endif
#if ANIMATION_HIJACK_RENDER
#include "Animation\AnimatedTestCube.h"
#endif

#define MAX_DEBUG_POINTS 10000

namespace Graphics
{

	Renderer::Renderer(ID3D11Device * device, ID3D11DeviceContext * deviceContext, ID3D11RenderTargetView * backBuffer, Camera *camera)
		: forwardPlus(device, SHADER_PATH("ForwardPlus.hlsl"), VERTEX_DESC)
		, fullscreenQuad(device, SHADER_PATH("FullscreenQuad.hlsl"), { { "POSITION", 0, DXGI_FORMAT_R8_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } })
		, depthStencil(device, WIN_WIDTH, WIN_HEIGHT)
        , instanceSBuffer(device, CpuAccess::Write, INSTANCE_CAP)
        , instanceOffsetBuffer(device)
		, skyRenderer(device, SHADOW_MAP_RESOLUTION)
		, postProcessor(device, deviceContext)
		, fakeBackBuffer(device, WIN_WIDTH, WIN_HEIGHT)
		, fakeBackBufferSwap(device, WIN_WIDTH, WIN_HEIGHT)
		, glowMap(device, WIN_WIDTH, WIN_HEIGHT)
        ,menu(device, deviceContext)
        ,hud(device, deviceContext)
    #pragma region RenderDebugInfo
        , debugPointsBuffer(device, CpuAccess::Write, MAX_DEBUG_POINTS)
        , debugRender(device, SHADER_PATH("DebugRender.hlsl"))
        , debugColorBuffer(device)
    #pragma endregion

	{
		this->device = device;
		this->deviceContext = deviceContext;
		this->backBuffer = backBuffer;
		
		initialize(device, deviceContext);

        viewPort = { 0 };
        viewPort.Width = WIN_WIDTH;
        viewPort.Height = WIN_HEIGHT;
        viewPort.MaxDepth = 1.0f;

		states = new DirectX::CommonStates(device);
		grid.initialize(camera, device, deviceContext, &resourceManager);

        //menuSprite = std::make_unique<DirectX::SpriteBatch>(deviceContext);
        createBlendState();
    }


    Renderer::~Renderer()
    {
		delete states;

		SAFE_RELEASE(transparencyBlendState);

		SAFE_RELEASE(glowTest);
        resourceManager.release();

    }

    void Renderer::initialize(ID3D11Device *gDevice, ID3D11DeviceContext* gDeviceContext)
    {
        resourceManager.initialize(gDevice, gDeviceContext);

		//temp
		DirectX::CreateWICTextureFromFile(device, TEXTURE_PATH("glowMapTree.png"), NULL, &glowTest);


    }

	void Renderer::updateLight(float deltaTime, Camera * camera)
	{
		skyRenderer.update(deviceContext, deltaTime, camera->getPos());
	}

    void Renderer::render(Camera * camera)
    {
        menu.unloadTextures();
#if ANIMATION_HIJACK_RENDER

        renderQueue.clear();
        static Camera cam(device, WIN_WIDTH, WIN_HEIGHT);
        static UINT ticks = 0;
        ticks++;
        cam.updateLookAt({ 5 * sinf(ticks * 0.001f), 5 * cosf(ticks * 0.001f), 5 }, { 0,0,0 }, deviceContext);

        ID3D11Buffer *cameraBuffer = cam.getBuffer();
        deviceContext->VSSetConstantBuffers(0, 1, &cameraBuffer);
        deviceContext->PSSetConstantBuffers(0, 1, &cameraBuffer);

        static float clearColor[4] = { 0,0,0,1 };
        deviceContext->ClearRenderTargetView(backBuffer, clearColor);
        deviceContext->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH, 1.f, 0);

        deviceContext->RSSetViewports(1, &viewPort);

        static AnimatedTestCube testCube(device);
        static Shader testShader(device, SHADER_PATH("AnimationTest.hlsl"), ANIMATED_VERTEX_DESC);

        deviceContext->IASetInputLayout(testShader);
        deviceContext->VSSetShader(testShader, nullptr, 0);
        deviceContext->PSSetShader(testShader, nullptr, 0);

        deviceContext->IASetVertexBuffers(0, 1, testCube, &testCube.stride, &testCube.offset);
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deviceContext->RSSetState(states->CullNone());

        deviceContext->OMSetRenderTargets(1, &backBuffer, depthStencil);

        deviceContext->Draw(testCube.vertexCount, 0);

        auto jointTransforms = testSkeleton.getJointTransforms(testAnimation, testAnimation.getTotalDuration() * ((ticks % 1000) / 1000.f));

        static StructuredBuffer<Matrix> jointBuffer(device, CpuAccess::Write, testSkeleton.getJointCount());
        Matrix* bufferPtr = jointBuffer.map(deviceContext);
        memcpy(bufferPtr, jointTransforms.data(), sizeof(Matrix) * jointTransforms.size());
        jointBuffer.unmap(deviceContext);

        ID3D11ShaderResourceView * jointView = jointBuffer.getSRV();
        deviceContext->VSSetShaderResources(0, 1, &jointView);

#else
        cull();
        writeInstanceData();

		//Drawshadows does not actually draw anything, it just sets up everything for drawing shadows
		skyRenderer.drawShadows(deviceContext, &forwardPlus);
		draw();


		ID3D11Buffer *cameraBuffer = camera->getBuffer();
		deviceContext->VSSetConstantBuffers(0, 1, &cameraBuffer);
		deviceContext->PSSetConstantBuffers(0, 1, &cameraBuffer);

		static float clearColor[4] = { 0, 0.5, 0.7, 1 };
		static float blackClearColor[4] = {0};
		deviceContext->ClearRenderTargetView(fakeBackBuffer, clearColor);
		deviceContext->ClearRenderTargetView(glowMap, blackClearColor);
		deviceContext->ClearRenderTargetView(backBuffer, clearColor);
        deviceContext->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH, 1.f, 0);


		deviceContext->RSSetViewports(1, &viewPort);
		deviceContext->RSSetState(states->CullCounterClockwise());

        deviceContext->IASetInputLayout(forwardPlus);
        deviceContext->VSSetShader(forwardPlus, nullptr, 0);

        deviceContext->PSSetShader(nullptr, nullptr, 0);
        deviceContext->OMSetRenderTargets(0, nullptr, depthStencil);
        deviceContext->OMSetDepthStencilState(states->DepthDefault(), 0);

        draw();

        deviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    #pragma region Light Temp
        static 	float f = 59.42542;
        f += 0.001f;

        auto lights = grid.getLights();

        Light *ptr = lights->map(deviceContext);
        for (int i = 0; i < NUM_LIGHTS; i++) {
            ptr[i].color = DirectX::SimpleMath::Vector3(
                ((unsigned char)(5 + i * 53 * i + 4)) / 255.f,
                ((unsigned char)(66 + i * 23 + 4)) / 255.f,
                ((unsigned char)(11 + i * 455 + 4)) / 255.f
            );
            ptr[i].positionWS = (ptr[i].color * 2 - DirectX::SimpleMath::Vector3(1.f)) * 2;
            ptr[i].positionWS.x = sin(f) * ptr[i].positionWS.x * 8;
            ptr[i].positionWS.y = 1.f;
            ptr[i].positionWS.z = cos(f) * ptr[i].positionWS.z * 8;

            //ptr[i].positionWS = DirectX::SimpleMath::Vector3(0.f, 1.f, 1.f - 1 / 8.f - i / 4.f);
            ptr[i].positionVS = DirectX::SimpleMath::Vector4::Transform(DirectX::SimpleMath::Vector4(ptr[i].positionWS.x, ptr[i].positionWS.y, ptr[i].positionWS.z, 1.f), camera->getView());
			ptr[i].range = i / 1.f;// 1.f + ((unsigned char)(i * 53 * i + 4)) / 255.f * i;
            ptr[i].intensity = 1.f;
        }

        lights->unmap(deviceContext);
    #pragma endregion

        grid.cull(camera, states, depthStencil, device, deviceContext, &resourceManager);

        deviceContext->IASetInputLayout(forwardPlus);
        deviceContext->VSSetShader(forwardPlus, nullptr, 0);
        deviceContext->PSSetShader(forwardPlus, nullptr, 0);

		

		ID3D11ShaderResourceView *SRVs[] = {
			grid.getOpaqueIndexList()->getSRV(),
			grid.getOpaqueLightGridSRV(),
			grid.getLights()->getSRV(),
			*skyRenderer.getDepthStencil()
		};
		auto sampler = states->LinearClamp();
		deviceContext->PSSetShaderResources(0, 4, SRVs);
		deviceContext->PSSetSamplers(0, 1, &sampler);

		ID3D11SamplerState * samplers[] = { skyRenderer.getSampler() };
		deviceContext->PSSetSamplers(1, 1, samplers);

		ID3D11Buffer *lightBuffs[] =
		{
			skyRenderer.getShaderBuffer(),
			skyRenderer.getLightMatrixBuffer()
		};
		
		deviceContext->PSSetConstantBuffers(1, 1, &lightBuffs[0]);
		deviceContext->VSSetConstantBuffers(2, 1, &lightBuffs[1]);

		ID3D11RenderTargetView * rtvs[] =
		{
			fakeBackBuffer,
			glowMap
		};
		deviceContext->OMSetRenderTargets(2, rtvs, depthStencil);
		
		draw();
		skyRenderer.renderSky(deviceContext, camera);

		ID3D11RenderTargetView * rtvNULL[2] = {nullptr};

        deviceContext->OMSetRenderTargets(2, rtvNULL, nullptr);

        ZeroMemory(SRVs, sizeof(SRVs));
        deviceContext->PSSetShaderResources(0, 4, SRVs);

        deviceContext->RSSetState(states->CullCounterClockwise());
        SHORT tabKeyState = GetAsyncKeyState(VK_TAB);
        if ((1 << 15) & tabKeyState)
        {
            this->drawToBackbuffer(grid.getDebugSRV());
        }
        
#endif

		///////Post effext
		postProcessor.addGlow(deviceContext, fakeBackBuffer, glowMap, &fakeBackBufferSwap);


		drawToBackbuffer(fakeBackBufferSwap);

        renderDebugInfo();
        hud.drawHUD(deviceContext, backBuffer, transparencyBlendState);
        
    }


    void Renderer::queueRender(RenderInfo * renderInfo)
    {
        if (renderQueue.size() > INSTANCE_CAP)
            throw "Renderer Exceeded Instance Cap.";

        renderQueue.push_back(renderInfo);
    }

    void Renderer::queueRenderDebug(RenderDebugInfo * debugInfo)
    {
        renderDebugQueue.push_back(debugInfo);
    }

    void Renderer::queueText(TextString * text)
    {
        hud.queueText(text);
    }

    void Renderer::fillHUDInfo(HUDInfo * info)
    {
        hud.fillHUDInfo(info);
    }


    //void Graphics::Renderer::renderMenu(MenuInfo * info)
    //{
    //	this->spriteBatch->Begin();
    //	for (size_t i = 0; i < info->m_buttons.size(); i++)
    //	{
    //		/*this->spriteBatch->Draw(info->m_buttons.at(i)->m_texture, info->m_buttons.at(i)->m_rek);*/
    //	}
    //	this->spriteBatch->End();
 //  
    //}



    void Renderer::cull()
    {
        instanceQueue.clear();
        for (RenderInfo * info : renderQueue)
        {
            if (info->render)
            {
                instanceQueue[info->meshId].push_back({ info->translation });
            }
        }
        renderQueue.clear();
    }

    void Renderer::writeInstanceData()
    {
        InstanceData* ptr = instanceSBuffer.map(deviceContext);
        for (InstanceQueue_t::value_type & pair : instanceQueue)
        {
            void * data = pair.second.data();
            size_t size = pair.second.size() * sizeof(InstanceData);
            memcpy(ptr, data, size);
            ptr = (InstanceData*)((char*)ptr + size);
        }
        instanceSBuffer.unmap(deviceContext);
    }

    void Renderer::draw()
    {
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deviceContext->VSSetConstantBuffers(3, 1, instanceOffsetBuffer);
        deviceContext->VSSetShaderResources(20, 1, instanceSBuffer);


        UINT instanceOffset = 0;
        for (InstanceQueue_t::value_type & pair : instanceQueue)
        {
            instanceOffsetBuffer.write(deviceContext, &instanceOffset, sizeof(UINT));
            instanceOffset += pair.second.size();

#if USE_TEMP_CUBE
            static TempCube tempCube(device);
			ModelInfo model = resourceManager.getModelInfo(CUBE);



			static UINT stride = sizeof(Vertex), offset = 0;
			deviceContext->IASetVertexBuffers(0, 1, &tempCube.vertexBuffer, &stride, &offset);

			static ID3D11ShaderResourceView * modelTextures[3] = { nullptr };
			modelTextures[0] = model.diffuseMap;
			modelTextures[1] = model.normalMap;
			modelTextures[2] = model.specularMap;
			deviceContext->PSSetShaderResources(10, 3, modelTextures);

			deviceContext->DrawInstanced(36, (UINT)pair.second.size(), 0, 0);
#else
            ModelInfo model = resourceManager.getModelInfo(pair.first);

            static UINT stride = sizeof(Vertex), offset = 0;
            deviceContext->IASetVertexBuffers(0, 1, &model.vertexBuffer, &stride, &offset);
            deviceContext->IASetIndexBuffer(model.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            static ID3D11ShaderResourceView * modelTextures[4] = { nullptr };
            modelTextures[0] = model.diffuseMap;
            modelTextures[1] = model.normalMap;
            modelTextures[2] = model.specularMap;
			modelTextures[3] = glowTest;
            deviceContext->PSSetShaderResources(10, 4, modelTextures);

            deviceContext->DrawIndexedInstanced((UINT)model.indexCount, (UINT)pair.second.size(), 0, 0, 0);
#endif
        }
    }

    void Renderer::drawToBackbuffer(ID3D11ShaderResourceView * texture)
    {
        float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        deviceContext->ClearRenderTargetView(backBuffer, clearColor);

        deviceContext->PSSetShaderResources(0, 1, &texture);

        UINT zero = 0;
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        deviceContext->OMSetRenderTargets(1, &backBuffer, nullptr);

        deviceContext->IASetInputLayout(fullscreenQuad);
        deviceContext->VSSetShader(fullscreenQuad, nullptr, 0);
        deviceContext->PSSetShader(fullscreenQuad, nullptr, 0);

        static ID3D11SamplerState * pointClamp = states->PointClamp();
        deviceContext->PSSetSamplers(0, 1, &pointClamp);

        deviceContext->Draw(4, 0);

        ID3D11ShaderResourceView * srvNull = nullptr;
        deviceContext->PSSetShaderResources(0, 1, &srvNull);
    }

    void Renderer::drawGUI()
    {
        
        
    }

    //draws the menu
    void Renderer::drawMenu(Graphics::MenuInfo * info)
    {
        deviceContext->RSSetViewports(1, &viewPort);
        menu.drawMenu(device, deviceContext, info, backBuffer);

    }

    //creates a vetrex buffer for the GUI
    

	


    void Renderer::renderDebugInfo()
    {
        //PROFILE_BEGIN("Renderer::renderDebugInfo()");
        if (renderDebugQueue.size() == 0) return;

        deviceContext->OMSetRenderTargets(1, &backBuffer, depthStencil);

        deviceContext->VSSetShaderResources(0, 1, debugPointsBuffer);
        deviceContext->PSSetConstantBuffers(1, 1, debugColorBuffer);

        deviceContext->IASetInputLayout(nullptr);
        deviceContext->VSSetShader(debugRender, nullptr, 0);
        deviceContext->PSSetShader(debugRender, nullptr, 0);

        for (RenderDebugInfo * info : renderDebugQueue)
        {
            if (info->points->size() > MAX_DEBUG_POINTS)
            {
                throw "vector is bigger than structured buffer";
            }

            debugPointsBuffer.write( 
                deviceContext, 
                info->points->data(), 
                info->points->size() * sizeof(DirectX::SimpleMath::Vector3)
            );

            debugColorBuffer.write(
                deviceContext,
                &info->color,
                sizeof(DirectX::SimpleMath::Color)
            );

            deviceContext->IASetPrimitiveTopology(info->topology);
            deviceContext->OMSetDepthStencilState(info->useDepth ? states->DepthDefault() : states->DepthNone(), 0);
            deviceContext->Draw(info->points->size(), 0);
        }

        renderDebugQueue.clear();
        //PROFILE_END();
    }

    void Renderer::createBlendState()
    {
        D3D11_BLEND_DESC BlendState;
        ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
        BlendState.RenderTarget[0].BlendEnable = TRUE;
        BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
        BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendState.RenderTarget[0].RenderTargetWriteMask = 0x0f;

        ThrowIfFailed(this->device->CreateBlendState(&BlendState, &transparencyBlendState));
    }


}
