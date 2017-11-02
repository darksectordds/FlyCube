#include "DX11Scene.h"
#include <Utilities/DXUtility.h>
#include <Utilities/FileUtility.h>
#include <Utilities/State.h>
#include <D3Dcompiler.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/gtx/transform.hpp>

DX11Scene::DX11Scene(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_context(m_width, m_height)
    , m_shader_geometry_pass(m_context.device)
    , m_shader_light_pass(m_context.device)
    , m_model_of_file("model/sponza/sponza.obj")
    , m_model_square("model/square.obj")
{
    for (DX11Mesh& cur_mesh : m_model_of_file.meshes)
    {
        cur_mesh.SetupMesh(m_context.device, m_context.device_context);
    }

    for (DX11Mesh& cur_mesh : m_model_square.meshes)
    {
        cur_mesh.SetupMesh(m_context.device, m_context.device_context);
    }

    CreateRT();
    CreateViewPort();
    CreateSampler();
    InitGBuffer();

    m_camera.SetViewport(m_width, m_height);
    m_context.device_context->PSSetSamplers(0, 1, m_texture_sampler.GetAddressOf());
    m_context.device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

IScene::Ptr DX11Scene::Create(int width, int height)
{
    return std::make_unique<DX11Scene>(width, height);
}

void DX11Scene::OnUpdate()
{
    UpdateCameraMovement();
    UpdateAngle();

    glm::mat4 projection, view, model;
    m_camera.GetMatrix(projection, view, model);

    float model_scale_ = 0.01f;
    model = glm::scale(glm::vec3(model_scale_)) * model;

    m_shader_geometry_pass.vs.cbuffer.ConstantBuffer.model = glm::transpose(model);
    m_shader_geometry_pass.vs.cbuffer.ConstantBuffer.view = glm::transpose(view);
    m_shader_geometry_pass.vs.cbuffer.ConstantBuffer.projection = glm::transpose(projection);
    m_shader_geometry_pass.vs.cbuffer.ConstantBuffer.normalMatrix = glm::transpose(glm::transpose(glm::inverse(model)));
    m_shader_geometry_pass.vs.UpdateCBuffers(m_context.device_context);

    float light_r = 2.5;
    glm::vec3 light_pos_ = glm::vec3(light_r * cos(m_angle), 25.0f, light_r * sin(m_angle));

    glm::vec3 cameraPosView = glm::vec3(glm::vec4(m_camera.GetCameraPos(), 1.0));
    glm::vec3 lightPosView = glm::vec3(glm::vec4(light_pos_, 1.0));

    m_shader_light_pass.ps.cbuffer.ConstantBuffer.lightPos = glm::vec4(lightPosView, 0);
    m_shader_light_pass.ps.cbuffer.ConstantBuffer.viewPos = glm::vec4(cameraPosView, 0.0);
    m_shader_light_pass.ps.UpdateCBuffers(m_context.device_context);
}

void DX11Scene::OnRender()
{
    GeometryPass();
    LightPass();
    ASSERT_SUCCEEDED(m_context.swap_chain->Present(0, 0));
}

void DX11Scene::GeometryPass()
{
    m_context.device_context->RSSetViewports(1, &m_viewport);

    m_shader_geometry_pass.UseProgram(m_context.device_context);
    m_shader_geometry_pass.UseProgram(m_context.device_context);
    m_context.device_context->IASetInputLayout(m_shader_geometry_pass.vs.input_layout.Get());

    std::vector<ID3D11RenderTargetView*> rtvs = {
        m_position_rtv.Get(),
        m_normal_rtv.Get(),
        m_ambient_rtv.Get(),
        m_diffuse_rtv.Get(),
        m_specular_rtv.Get(),
    };

    float bgColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    for (auto & rtv : rtvs)
    {
        m_context.device_context->ClearRenderTargetView(rtv, bgColor);
    }
    m_context.device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_context.device_context->OMSetRenderTargets(rtvs.size(), rtvs.data(), m_depth_stencil_view.Get());

    m_shader_geometry_pass.ps.cbuffer.Light.light_ambient = glm::vec3(0.2f);
    m_shader_geometry_pass.ps.cbuffer.Light.light_diffuse = glm::vec3(1.0f);
    m_shader_geometry_pass.ps.cbuffer.Light.light_specular = glm::vec3(0.5f);

    auto& state = CurState<bool>::Instance().state;

    for (DX11Mesh& cur_mesh : m_model_of_file.meshes)
    {
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_geometry_pass.vs.geometry.POSITION, cur_mesh.positions_buffer, sizeof(glm::vec3), 0);
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_geometry_pass.vs.geometry.NORMAL, cur_mesh.normals_buffer, sizeof(glm::vec3), 0);
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_geometry_pass.vs.geometry.TEXCOORD, cur_mesh.texcoords_buffer, sizeof(glm::vec2), 0);
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_geometry_pass.vs.geometry.TANGENT, cur_mesh.tangents_buffer, sizeof(glm::vec3), 0);
        m_context.device_context->IASetIndexBuffer(cur_mesh.indices_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        if (!state["disable_norm"])
            cur_mesh.SetTexture(m_context.device_context, aiTextureType_HEIGHT, m_shader_geometry_pass.ps.texture.normalMap);
        else
            cur_mesh.UnsetTexture(m_context.device_context, m_shader_geometry_pass.ps.texture.normalMap);
        cur_mesh.SetTexture(m_context.device_context, aiTextureType_OPACITY, m_shader_geometry_pass.ps.texture.alphaMap);
        cur_mesh.SetTexture(m_context.device_context, aiTextureType_AMBIENT, m_shader_geometry_pass.ps.texture.ambientMap);
        cur_mesh.SetTexture(m_context.device_context, aiTextureType_DIFFUSE, m_shader_geometry_pass.ps.texture.diffuseMap);
        cur_mesh.SetTexture(m_context.device_context, aiTextureType_SPECULAR, m_shader_geometry_pass.ps.texture.specularMap);
        cur_mesh.SetTexture(m_context.device_context, aiTextureType_SHININESS, m_shader_geometry_pass.ps.texture.glossMap);

        m_shader_geometry_pass.ps.cbuffer.Material.material_ambient = cur_mesh.material.amb;
        m_shader_geometry_pass.ps.cbuffer.Material.material_diffuse = cur_mesh.material.dif;
        m_shader_geometry_pass.ps.cbuffer.Material.material_specular = cur_mesh.material.spec;
        m_shader_geometry_pass.ps.cbuffer.Material.material_shininess = cur_mesh.material.shininess;
        m_shader_geometry_pass.ps.UpdateCBuffers(m_context.device_context);

        m_context.device_context->DrawIndexed(cur_mesh.indices.size(), 0, 0);
    }
}

void DX11Scene::LightPass()
{
    m_shader_light_pass.UseProgram(m_context.device_context);
    m_context.device_context->IASetInputLayout(m_shader_light_pass.vs.input_layout.Get());

    m_context.device_context->OMSetRenderTargets(1, m_render_target_view.GetAddressOf(), m_depth_stencil_view.Get());

    float bgColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_context.device_context->ClearRenderTargetView(m_render_target_view.Get(), bgColor);
    m_context.device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    for (DX11Mesh& cur_mesh : m_model_square.meshes)
    {
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_light_pass.vs.geometry.POSITION, cur_mesh.positions_buffer, sizeof(glm::vec3), 0);
        cur_mesh.SetVertexBuffer(m_context.device_context, m_shader_light_pass.vs.geometry.TEXCOORD, cur_mesh.texcoords_buffer, sizeof(glm::vec2), 0);
        m_context.device_context->IASetIndexBuffer(cur_mesh.indices_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        m_context.device_context->PSSetShaderResources(m_shader_light_pass.ps.texture.gPosition, 1, m_position_srv.GetAddressOf());
        m_context.device_context->PSSetShaderResources(m_shader_light_pass.ps.texture.gNormal,   1, m_normal_srv.GetAddressOf());
        m_context.device_context->PSSetShaderResources(m_shader_light_pass.ps.texture.gAmbient,  1, m_ambient_srv.GetAddressOf());
        m_context.device_context->PSSetShaderResources(m_shader_light_pass.ps.texture.gDiffuse,  1, m_diffuse_srv.GetAddressOf());
        m_context.device_context->PSSetShaderResources(m_shader_light_pass.ps.texture.gSpecular, 1, m_specular_srv.GetAddressOf());
        m_context.device_context->DrawIndexed(cur_mesh.indices.size(), 0, 0);
    }
}

void DX11Scene::OnResize(int width, int height)
{
    if (width != m_width || height != m_height)
    {
        m_width = width;
        m_height = height;

        m_render_target_view.Reset();

        DXGI_SWAP_CHAIN_DESC desc = {};
        ASSERT_SUCCEEDED(m_context.swap_chain->GetDesc(&desc));
        ASSERT_SUCCEEDED(m_context.swap_chain->ResizeBuffers(1, width, height, desc.BufferDesc.Format, desc.Flags));

        CreateRT();
        CreateViewPort();
        InitGBuffer();
        m_camera.SetViewport(m_width, m_height);
    }
}

void DX11Scene::CreateRT()
{
    //Create our BackBuffer
    ComPtr<ID3D11Texture2D> BackBuffer;
    ASSERT_SUCCEEDED(m_context.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer));

    //Create our Render Target
    ASSERT_SUCCEEDED(m_context.device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &m_render_target_view));

    //Describe our Depth/Stencil Buffer
    D3D11_TEXTURE2D_DESC depthStencilDesc;
    depthStencilDesc.Width = m_width;
    depthStencilDesc.Height = m_height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    ASSERT_SUCCEEDED(m_context.device->CreateTexture2D(&depthStencilDesc, nullptr, &m_depth_stencil_buffer));
    ASSERT_SUCCEEDED(m_context.device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), nullptr, &m_depth_stencil_view));
}

void DX11Scene::CreateViewPort()
{
    ZeroMemory(&m_viewport, sizeof(D3D11_VIEWPORT));
    m_viewport.TopLeftX = 0;
    m_viewport.TopLeftY = 0;
    m_viewport.Width = m_width;
    m_viewport.Height = m_height;
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
}

void DX11Scene::CreateSampler()
{
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    ASSERT_SUCCEEDED(m_context.device->CreateSamplerState(&sampDesc, &m_texture_sampler));
}

void DX11Scene::CreateRTV(ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv)
{
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = m_width;
    texture_desc.Height = m_height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    ASSERT_SUCCEEDED(m_context.device->CreateTexture2D(&texture_desc, nullptr, &texture));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    ASSERT_SUCCEEDED(m_context.device->CreateShaderResourceView(texture.Get(), &srv_desc, &srv));
    ASSERT_SUCCEEDED(m_context.device->CreateRenderTargetView(texture.Get(), nullptr, &rtv));
}

void DX11Scene::InitGBuffer()
{
    CreateRTV(m_position_rtv, m_position_srv);
    CreateRTV(m_normal_rtv, m_normal_srv);
    CreateRTV(m_ambient_rtv, m_ambient_srv);
    CreateRTV(m_diffuse_rtv, m_diffuse_srv);
    CreateRTV(m_specular_rtv, m_specular_srv);
}
