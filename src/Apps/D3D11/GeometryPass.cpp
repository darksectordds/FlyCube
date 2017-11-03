#include "GeometryPass.h"

#include <Utilities/DXUtility.h>
#include <Utilities/FileUtility.h>
#include <Utilities/State.h>
#include <D3Dcompiler.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/gtx/transform.hpp>

GeometryPass::GeometryPass(Context& context, Model<DX11Mesh>& model, Camera& camera, int width, int height)
    : m_context(context)
    , m_model(model)
    , m_width(width)
    , m_height(height)
    , m_program(m_context.device)
    , m_camera(camera)
{
    InitGBuffers();
    CreaetDepth();
}

void GeometryPass::OnUpdate()
{
    glm::mat4 projection, view, model;
    m_camera.GetMatrix(projection, view, model);

    float model_scale = 0.01f;
    model = glm::scale(glm::vec3(model_scale)) * model;

    m_program.vs.cbuffer.ConstantBuffer.model = glm::transpose(model);
    m_program.vs.cbuffer.ConstantBuffer.view = glm::transpose(view);
    m_program.vs.cbuffer.ConstantBuffer.projection = glm::transpose(projection);
    m_program.vs.cbuffer.ConstantBuffer.normalMatrix = glm::transpose(glm::transpose(glm::inverse(model)));
    m_program.ps.cbuffer.Light.light_ambient = glm::vec3(0.2f);
    m_program.ps.cbuffer.Light.light_diffuse = glm::vec3(1.0f);
    m_program.ps.cbuffer.Light.light_specular = glm::vec3(0.5f);
}

void GeometryPass::OnRender()
{
    m_program.UseProgram(m_context.device_context);
    m_context.device_context->IASetInputLayout(m_program.vs.input_layout.Get());

    std::vector<ID3D11RenderTargetView*> rtvs = {
        m_position_rtv.Get(),
        m_normal_rtv.Get(),
        m_ambient_rtv.Get(),
        m_diffuse_rtv.Get(),
        m_specular_rtv.Get(),
    };

    float bgColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    for (auto & rtv : rtvs)
        m_context.device_context->ClearRenderTargetView(rtv, bgColor);
    m_context.device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    m_context.device_context->OMSetRenderTargets(rtvs.size(), rtvs.data(), m_depth_stencil_view.Get());

    auto& state = CurState<bool>::Instance().state;
    for (DX11Mesh& cur_mesh : m_model.meshes)
    {
        m_context.device_context->IASetIndexBuffer(cur_mesh.indices_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        cur_mesh.SetVertexBuffer(m_context, m_program.vs.geometry.POSITION, VertexType::kPosition);
        cur_mesh.SetVertexBuffer(m_context, m_program.vs.geometry.NORMAL, VertexType::kNormal);
        cur_mesh.SetVertexBuffer(m_context, m_program.vs.geometry.TEXCOORD, VertexType::kTexcoord);
        cur_mesh.SetVertexBuffer(m_context, m_program.vs.geometry.TANGENT, VertexType::kTangent);

        if (!state["disable_norm"])
            cur_mesh.SetTexture(m_context, aiTextureType_HEIGHT, m_program.ps.texture.normalMap);
        else
            cur_mesh.UnsetTexture(m_context, m_program.ps.texture.normalMap);
        cur_mesh.SetTexture(m_context, aiTextureType_OPACITY, m_program.ps.texture.alphaMap);
        cur_mesh.SetTexture(m_context, aiTextureType_AMBIENT, m_program.ps.texture.ambientMap);
        cur_mesh.SetTexture(m_context, aiTextureType_DIFFUSE, m_program.ps.texture.diffuseMap);
        cur_mesh.SetTexture(m_context, aiTextureType_SPECULAR, m_program.ps.texture.specularMap);
        cur_mesh.SetTexture(m_context, aiTextureType_SHININESS, m_program.ps.texture.glossMap);

        m_program.ps.cbuffer.Material.material_ambient = cur_mesh.material.amb;
        m_program.ps.cbuffer.Material.material_diffuse = cur_mesh.material.dif;
        m_program.ps.cbuffer.Material.material_specular = cur_mesh.material.spec;
        m_program.ps.cbuffer.Material.material_shininess = cur_mesh.material.shininess;

        m_program.ps.UpdateCBuffers(m_context.device_context);

        m_context.device_context->DrawIndexed(cur_mesh.indices.size(), 0, 0);
    }
}

void GeometryPass::OnResize(int width, int height)
{
    if (width == m_width && height == m_height)
        return;

    m_width = width;
    m_height = height;
    InitGBuffers();
    CreaetDepth();
}

void GeometryPass::CreaetDepth()
{
    D3D11_TEXTURE2D_DESC depth_stencil_desc = {};
    depth_stencil_desc.Width = m_width;
    depth_stencil_desc.Height = m_height;
    depth_stencil_desc.MipLevels = 1;
    depth_stencil_desc.ArraySize = 1;
    depth_stencil_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_stencil_desc.SampleDesc.Count = 1;
    depth_stencil_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_stencil_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ASSERT_SUCCEEDED(m_context.device->CreateTexture2D(&depth_stencil_desc, nullptr, &m_depth_stencil_buffer));
    ASSERT_SUCCEEDED(m_context.device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), nullptr, &m_depth_stencil_view));
}

void GeometryPass::CreateRtvSrv(ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv)
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

void GeometryPass::InitGBuffers()
{
    CreateRtvSrv(m_position_rtv, m_position_srv);
    CreateRtvSrv(m_normal_rtv, m_normal_srv);
    CreateRtvSrv(m_ambient_rtv, m_ambient_srv);
    CreateRtvSrv(m_diffuse_rtv, m_diffuse_srv);
    CreateRtvSrv(m_specular_rtv, m_specular_srv);
}
