#include "DXSample.h"
#include <chrono>
#include <SOIL.h>
#include <Util.h>
#include <Utility.h>
#include <FileUtility.h>

DXSample::DXSample(int width, int height)
    : m_width(width)
    , m_height(height)
{}

DXSample::~DXSample()
{}

void DXSample::OnInit()
{
    CreateDeviceAndSwapChain();

    m_shader_geometry_pass.reset(new ShaderGeometryPass(m_device));
    m_shader_light_pass.reset(new ShaderLightPass(m_device));

    CreateRT();
    CreateViewPort();
    CreateSampler();
    m_model_of_file = CreateGeometry("model/export3dcoat/export3dcoat.obj");
    m_model_square = CreateGeometry("model/square.obj");
    InitGBuffer();

    m_device_context->PSSetSamplers(0, 1, &m_texture_sampler);
    m_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_device_context->RSSetViewports(1, &m_viewport);
}

void DXSample::OnUpdate()
{
    static std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
    static std::chrono::time_point<std::chrono::system_clock> end = std::chrono::system_clock::now();
    static float angle = 0.0;

    end = std::chrono::system_clock::now();
    int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    start = std::chrono::system_clock::now();

    if (m_use_rotare)
        angle += elapsed / 2e6f;

    float z_width = (m_model_of_file->bound_box.z_max - m_model_of_file->bound_box.z_min);
    float y_width = (m_model_of_file->bound_box.y_max - m_model_of_file->bound_box.y_min);
    float x_width = (m_model_of_file->bound_box.y_max - m_model_of_file->bound_box.y_min);
    float model_width = (z_width + y_width + x_width) / 3.0f;
    float scale = 256.0f / std::max(z_width, x_width);
    model_width *= scale;

    float offset_x = (m_model_of_file->bound_box.x_max + m_model_of_file->bound_box.x_min) / 2.0f;
    float offset_y = (m_model_of_file->bound_box.y_max + m_model_of_file->bound_box.y_min) / 2.0f;
    float offset_z = (m_model_of_file->bound_box.z_max + m_model_of_file->bound_box.z_min) / 2.0f;

    glm::vec3 cameraPosition = glm::vec3(0.0f, model_width * 0.25f, -model_width * 2.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 model = glm::rotate(-angle, glm::vec3(0.0, 1.0, 0.0)) * glm::translate(glm::vec3(-offset_x, -offset_y, -offset_z)) * glm::scale(glm::vec3(scale, scale, scale));
    glm::mat4 view = glm::lookAtRH(cameraPosition, cameraTarget, cameraUp);
    glm::mat4 proj = glm::perspectiveFovRH(45.0f * (3.14f / 180.0f), (float)m_width, (float)m_height, 0.1f, 1024.0f);

    
    m_shader_geometry_pass->uniform.model = glm::transpose(model);
    m_shader_geometry_pass->uniform.view = glm::transpose(view);
    m_shader_geometry_pass->uniform.projection = glm::transpose(proj);
    m_shader_light_pass->uniform.lightPos = glm::vec4(cameraPosition, 0.0);
    m_shader_light_pass->uniform.viewPos = glm::vec4(cameraPosition, 0.0);

    m_device_context->UpdateSubresource(m_shader_geometry_pass->uniform_buffer.Get(), 0, nullptr, &m_shader_geometry_pass->uniform, 0, 0);
    m_device_context->UpdateSubresource(m_shader_light_pass->uniform_buffer.Get(), 0, nullptr, &m_shader_light_pass->uniform, 0, 0);
}

void DXSample::OnRender()
{
    GeometryPass();
    LightPass();
    ASSERT_SUCCEEDED(m_swap_chain->Present(0, 0));
}

void DXSample::GeometryPass()
{
    m_device_context->VSSetShader(m_shader_geometry_pass->vertex_shader.Get(), 0, 0);
    m_device_context->PSSetShader(m_shader_geometry_pass->pixel_shader.Get(), 0, 0);
    m_device_context->VSSetConstantBuffers(0, 1, m_shader_geometry_pass->uniform_buffer.GetAddressOf());
    m_device_context->IASetInputLayout(m_shader_geometry_pass->input_layout.Get());

    std::vector<ID3D11RenderTargetView*> rtvs = {
        m_position_rtv.Get(),
        m_normal_rtv.Get(),
        m_ambient_rtv.Get(),
        m_diffuse_rtv.Get(),
        m_specular_rtv.Get(),
        m_gloss_rtv.Get(),
    };

    float bgColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    for (auto & rtv : rtvs)
    {
        m_device_context->ClearRenderTargetView(rtv, bgColor);
    }
    m_device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_device_context->OMSetRenderTargets(rtvs.size(), rtvs.data(), m_depth_stencil_view.Get());

    for (size_t mesh_id = 0; mesh_id < m_model_of_file->meshes.size(); ++mesh_id)
    {
        DX11Mesh & cur_mesh = m_model_of_file->meshes[mesh_id];

        UINT stride = sizeof(cur_mesh.vertices[0]);
        UINT offset = 0;
        m_device_context->IASetVertexBuffers(0, 1, cur_mesh.vertBuffer.GetAddressOf(), &stride, &offset);
        m_device_context->IASetIndexBuffer(cur_mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        std::vector<ID3D11ShaderResourceView*> use_textures(5, nullptr);
        static uint32_t frameId = 0;
        for (size_t i = 0; i < cur_mesh.textures.size(); ++i)
        {
            uint32_t texture_slot = 0;
            switch (cur_mesh.textures[i].type)
            {
            case aiTextureType_DIFFUSE:
                texture_slot = 0;
                break;
            case aiTextureType_HEIGHT:
                texture_slot = 1;
                break;
            case aiTextureType_SPECULAR:
                texture_slot = 2;
                break;
            case aiTextureType_SHININESS:
                texture_slot = 3;
                break;
            case aiTextureType_AMBIENT:
                texture_slot = 4;
                break;
            default:
                continue;
            }

            use_textures[texture_slot] = cur_mesh.texResources[i].Get();
        }

        m_device_context->PSSetShaderResources(0, use_textures.size(), use_textures.data());
        m_device_context->DrawIndexed(cur_mesh.indices.size(), 0, 0);
    }
}

void DXSample::LightPass()
{
    m_device_context->VSSetShader(m_shader_light_pass->vertex_shader.Get(), 0, 0);
    m_device_context->PSSetShader(m_shader_light_pass->pixel_shader.Get(), 0, 0);
    m_device_context->PSSetConstantBuffers(0, 1, m_shader_light_pass->uniform_buffer.GetAddressOf());
    m_device_context->IASetInputLayout(m_shader_light_pass->input_layout.Get());

    m_device_context->OMSetRenderTargets(1, m_render_target_view.GetAddressOf(), m_depth_stencil_view.Get());

    float bgColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_device_context->ClearRenderTargetView(m_render_target_view.Get(), bgColor);
    m_device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    for (size_t mesh_id = 0; mesh_id < m_model_square->meshes.size(); ++mesh_id)
    {
        DX11Mesh & cur_mesh = m_model_square->meshes[mesh_id];

        UINT stride = sizeof(cur_mesh.vertices[0]);
        UINT offset = 0;
        m_device_context->IASetVertexBuffers(0, 1, cur_mesh.vertBuffer.GetAddressOf(), &stride, &offset);
        m_device_context->IASetIndexBuffer(cur_mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        std::vector<ID3D11ShaderResourceView*> use_textures =
        {
            m_position_srv.Get(),
            m_normal_srv.Get(),
            m_ambient_srv.Get(),
            m_diffuse_srv.Get(),
            m_specular_srv.Get(),
            m_gloss_srv.Get()
        };
    
        m_device_context->PSSetShaderResources(0, use_textures.size(), use_textures.data());
        m_device_context->DrawIndexed(cur_mesh.indices.size(), 0, 0);
    }
}

void DXSample::OnDestroy()
{
}

void DXSample::OnSizeChanged(int width, int height)
{
    if (width != m_width || height != m_height)
    {
        m_width = width;
        m_height = height;

        m_render_target_view.Reset();

        DXGI_SWAP_CHAIN_DESC desc = {};
        ASSERT_SUCCEEDED(m_swap_chain->GetDesc(&desc));
        ASSERT_SUCCEEDED(m_swap_chain->ResizeBuffers(1, width, height, desc.BufferDesc.Format, desc.Flags));

        CreateRT();
        CreateViewPort();
    }
}

UINT DXSample::GetWidth() const
{
    return m_width;
}

UINT DXSample::GetHeight() const
{
    return m_height;
}

void DXSample::CreateDeviceAndSwapChain()
{    
    //Describe our Buffer
    DXGI_MODE_DESC bufferDesc;
    ZeroMemory(&bufferDesc, sizeof(DXGI_MODE_DESC));
    bufferDesc.Width = m_width;
    bufferDesc.Height = m_height;
    bufferDesc.RefreshRate.Numerator = 60;
    bufferDesc.RefreshRate.Denominator = 1;
    bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    //Describe our SwapChain
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
    swapChainDesc.BufferDesc = bufferDesc;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 1;
    swapChainDesc.OutputWindow = Win32Application::GetHwnd();
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    //Create our SwapChain
    ASSERT_SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &swapChainDesc, &m_swap_chain, &m_device, nullptr, &m_device_context));
}

void DXSample::CreateRT()
{
    //Create our BackBuffer
    ComPtr<ID3D11Texture2D> BackBuffer;
    ASSERT_SUCCEEDED(m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer));

    //Create our Render Target
    ASSERT_SUCCEEDED(m_device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &m_render_target_view));

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

    ASSERT_SUCCEEDED(m_device->CreateTexture2D(&depthStencilDesc, nullptr, &m_depth_stencil_buffer));
    ASSERT_SUCCEEDED(m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), nullptr, &m_depth_stencil_view));
}

void DXSample::CreateViewPort()
{
    ZeroMemory(&m_viewport, sizeof(D3D11_VIEWPORT));
    m_viewport.TopLeftX = 0;
    m_viewport.TopLeftY = 0;
    m_viewport.Width = m_width;
    m_viewport.Height = m_height;
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
}

void DXSample::CreateSampler()
{
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    ASSERT_SUCCEEDED(m_device->CreateSamplerState(&sampDesc, &m_texture_sampler));
}

std::unique_ptr<Model<DX11Mesh>> DXSample::CreateGeometry(const std::string& path)
{
    std::unique_ptr<Model<DX11Mesh>> model = std::make_unique<Model<DX11Mesh>>(path);
     
    for (DX11Mesh & cur_mesh : model->meshes)
    {
        cur_mesh.SetupMesh(m_device, m_device_context);
    }

    for (DX11Mesh & cur_mesh : model->meshes)
    {
        for (size_t i = 0; i < cur_mesh.textures.size(); ++i)
        {
            auto metadata = LoadImageDataFromFile(cur_mesh.textures[i].path);

            ComPtr<ID3D11Texture2D> resource;

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(metadata.width);
            desc.Height = static_cast<UINT>(metadata.height);
            desc.MipLevels = static_cast<UINT>(1);
            desc.ArraySize = static_cast<UINT>(1);
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;

            D3D11_SUBRESOURCE_DATA textureBufferData;
            ZeroMemory(&textureBufferData, sizeof(textureBufferData));
            textureBufferData.pSysMem = metadata.image.data();
            textureBufferData.SysMemPitch = metadata.bytes_per_row; // size of all our triangle vertex data
            textureBufferData.SysMemSlicePitch = metadata.bytes_per_row * metadata.height; // also the size of our triangle vertex data

            ASSERT_SUCCEEDED(m_device->CreateTexture2D(&desc, &textureBufferData, &resource));

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            ASSERT_SUCCEEDED(m_device->CreateShaderResourceView(resource.Get(), &srvDesc, &cur_mesh.texResources[i]));
        }
    }
    return model;
}

void DXSample::CreateRTV(ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv)
{
    D3D11_TEXTURE2D_DESC texture_desc;
    ZeroMemory(&texture_desc, sizeof(texture_desc));
    texture_desc.Width = m_width;
    texture_desc.Height = m_height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> texture;
    ASSERT_SUCCEEDED(m_device->CreateTexture2D(&texture_desc, nullptr, &texture));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    ASSERT_SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), &srvDesc, &srv));

    ASSERT_SUCCEEDED(m_device->CreateRenderTargetView(texture.Get(), nullptr, &rtv));
}

void DXSample::InitGBuffer()
{
    CreateRTV(m_position_rtv, m_position_srv);
    CreateRTV(m_normal_rtv, m_normal_srv);
    CreateRTV(m_ambient_rtv, m_ambient_srv);
    CreateRTV(m_diffuse_rtv, m_diffuse_srv);
    CreateRTV(m_specular_rtv, m_specular_srv);
    CreateRTV(m_gloss_rtv, m_gloss_srv);
}

DXSample::TexInfo DXSample::LoadImageDataFromFile(const std::string & filename)
{
    TexInfo texInfo = {};

    unsigned char *image = SOIL_load_image(filename.c_str(), &texInfo.width, &texInfo.height, 0, SOIL_LOAD_RGBA);

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    texInfo.num_bits_per_pixel = BitsPerPixel(dxgiFormat); // number of bits per pixel
    texInfo.bytes_per_row = (texInfo.width * texInfo.num_bits_per_pixel) / 8; // number of bytes in each row of the image data
    texInfo.image_size = texInfo.bytes_per_row * texInfo.height; // total image size in bytes

    texInfo.image.resize(texInfo.image_size);
    memcpy(texInfo.image.data(), image, texInfo.image_size);
    SOIL_free_image_data(image);

    return texInfo;
}
