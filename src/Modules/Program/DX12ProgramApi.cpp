#include "Program/DX12ProgramApi.h"
#include "Program/BufferLayout.h"
#include <Utilities/State.h>

size_t GenId()
{
    static size_t id = 0;
    return ++id;
}

size_t GetBindingId(size_t program_id, ShaderType shader_type, ResourceType res_type, uint32_t slot)
{
    static std::map<std::tuple<size_t, ShaderType, ResourceType, uint32_t>, size_t> binding_id;
    auto it = binding_id.find({ program_id, shader_type, res_type, slot });
    if (it != binding_id.end())
        return it->second;

    return binding_id.emplace(std::piecewise_construct,
        std::forward_as_tuple(program_id, shader_type, res_type, slot),
        std::forward_as_tuple(GenId())).first->second;
}

DX12ProgramApi::DX12ProgramApi(DX12Context& context)
    : m_context(context)
    , m_program_id(GenId())
{
    auto& state = CurState<bool>::Instance().state;
    if (state["DXIL"])
        _D3DReflect = (decltype(&::D3DReflect))GetProcAddress(LoadLibraryA("d3dcompiler_dxc_bridge.dll"), "D3DReflect");

    m_pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    m_pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc(D3D12_DEFAULT);
    depth_stencil_desc.DepthEnable = true;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depth_stencil_desc.StencilEnable = FALSE;
    m_pso_desc.DepthStencilState = depth_stencil_desc;
}

void DX12ProgramApi::SetMaxEvents(size_t count)
{
    m_context.descriptor_pool->ReqFrameDescription(ResourceType::kCbv, m_num_cbv * count * Context::FrameCount);
    m_context.descriptor_pool->ReqFrameDescription(ResourceType::kSrv, m_num_srv * count * Context::FrameCount);
    m_context.descriptor_pool->ReqFrameDescription(ResourceType::kUav, m_num_uav * count * Context::FrameCount);
    m_context.descriptor_pool->ReqFrameDescription(ResourceType::kSampler, m_num_sampler);
}

void DX12ProgramApi::UseProgram()
{
    m_context.UseProgram(*this);
    m_changed_binding = true;
    m_context.command_list->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SetRootSignature(m_root_signature.Get());
    if (m_current_pso)
        m_context.command_list->SetPipelineState(m_current_pso.Get());
}

void DX12ProgramApi::OnCompileShader(ShaderType type, const ComPtr<ID3DBlob>& blob)
{
    m_blob_map[type] = blob;
    D3D12_SHADER_BYTECODE ShaderBytecode = {};
    ShaderBytecode.BytecodeLength = blob->GetBufferSize();
    ShaderBytecode.pShaderBytecode = blob->GetBufferPointer();
    switch (type)
    {
    case ShaderType::kVertex:
        m_pso_desc.VS = ShaderBytecode;
        break;
    case ShaderType::kPixel:
        m_pso_desc.PS = ShaderBytecode;
        break;
    case ShaderType::kCompute:
        m_compute_pso_desc.CS = ShaderBytecode;
        break;
    case ShaderType::kGeometry:
        m_pso_desc.GS = ShaderBytecode;
        break;
    }

    m_changed_pso_desc = true;
    ParseShaders();
}

void DX12ProgramApi::AttachSRV(ShaderType type, const std::string & name, uint32_t slot, const Resource::Ptr& res)
{
    auto it = m_heap_ranges.find({ type, ResourceType::kSrv, slot });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);
    else if (!res)
        return;

    m_changed_binding = true;

    if (!res)
        return;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(type, ResourceType::kSrv, slot),
        std::forward_as_tuple(CreateSrv(type, name, slot, res)));
}

void DX12ProgramApi::AttachUAV(ShaderType type, const std::string & name, uint32_t slot, const Resource::Ptr& res)
{
    auto it = m_heap_ranges.find({ type, ResourceType::kUav, slot });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);
    else if (!res)
        return;

    m_changed_binding = true;

    if (!res)
        return;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(type, ResourceType::kUav, slot),
        std::forward_as_tuple(CreateUAV(type, name, slot, res)));
}

void DX12ProgramApi::AttachCBV(ShaderType type, uint32_t slot, const ComPtr<ID3D12Resource>& res)
{
    auto it = m_heap_ranges.find({ type, ResourceType::kCbv, slot });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);
    else if (!res)
        return;

    m_changed_binding = true;

    if (!res)
        return;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(type, ResourceType::kCbv, slot),
        std::forward_as_tuple(CreateCBV(type, slot, res)));
}

void DX12ProgramApi::AttachCBuffer(ShaderType type, UINT slot, BufferLayout& buffer)
{
    m_cbv_layout.emplace(std::piecewise_construct,
        std::forward_as_tuple(type, slot),
        std::forward_as_tuple(buffer));
}

void DX12ProgramApi::AttachSampler(ShaderType type, uint32_t slot, const SamplerDesc& desc)
{
    auto it = m_heap_ranges.find({ type, ResourceType::kSampler, slot });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);

    m_changed_binding = true;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(type, ResourceType::kSampler, slot),
        std::forward_as_tuple(CreateSampler(type, slot, desc)));
}

void DX12ProgramApi::AttachRTV(uint32_t slot, const Resource::Ptr& ires)
{
    auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kRtv, slot });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);

    m_changed_om = true;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(ShaderType::kPixel, ResourceType::kRtv, slot),
        std::forward_as_tuple(CreateRTV(slot, ires)));
}

void DX12ProgramApi::AttachDSV(const Resource::Ptr& ires)
{
    auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kDsv, 0 });
    if (it != m_heap_ranges.end())
        m_heap_ranges.erase(it);

    m_changed_om = true;

    m_heap_ranges.emplace(std::piecewise_construct,
        std::forward_as_tuple(ShaderType::kPixel, ResourceType::kDsv, 0),
        std::forward_as_tuple(CreateDSV(ires)));
}

void DX12ProgramApi::ClearRenderTarget(uint32_t slot, const FLOAT ColorRGBA[4])
{
    auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kRtv, slot });
    if (it == m_heap_ranges.end())
        return;
    auto& range = it->second;
    m_context.command_list->ClearRenderTargetView(range.GetCpuHandle(), ColorRGBA, 0, nullptr);
}

void DX12ProgramApi::ClearDepthStencil(UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kDsv, 0 });
    if (it == m_heap_ranges.end())
        return;
    auto& range = it->second;
    m_context.command_list->ClearDepthStencilView(range.GetCpuHandle(), (D3D12_CLEAR_FLAGS)ClearFlags, Depth, Stencil, 0, nullptr);
}

void DX12ProgramApi::SetRasterizeState(const RasterizerDesc& desc)
{
    m_changed_pso_desc = true;
    switch (desc.fill_mode)
    {
    case FillMode::kWireframe:
        m_pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        break;
    case FillMode::kSolid:
        m_pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        break;
    }

    switch (desc.cull_mode)
    {
    case CullMode::kNone:
        m_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    case CullMode::kFront:
        m_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        break;
    case CullMode::kBack:
        m_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        break;
    }

    m_pso_desc.RasterizerState.DepthBias = desc.DepthBias;
}

void DX12ProgramApi::SetBlendState(const BlendDesc& desc)
{
    m_changed_pso_desc = true;
    auto& rt_desc = m_pso_desc.BlendState.RenderTarget[0];

    auto convert = [](Blend type)
    {
        switch (type)
        {
        case Blend::kZero:
            return D3D12_BLEND_ZERO;
        case Blend::kSrcAlpha:
            return D3D12_BLEND_SRC_ALPHA;
        case Blend::kInvSrcAlpha:
            return D3D12_BLEND_INV_SRC_ALPHA;
        }
    };

    auto convert_op = [](BlendOp type)
    {
        switch (type)
        {
        case BlendOp::kAdd:
            return D3D12_BLEND_OP_ADD;
        }
    };

    rt_desc.BlendEnable = desc.blend_enable;
    rt_desc.BlendOp = convert_op(desc.blend_op);
    rt_desc.SrcBlend = convert(desc.blend_src);
    rt_desc.DestBlend = convert(desc.blend_dest);
    rt_desc.BlendOpAlpha = convert_op(desc.blend_op_alpha);
    rt_desc.SrcBlendAlpha = convert(desc.blend_src_alpha);
    rt_desc.DestBlendAlpha = convert(desc.blend_dest_apha);
    rt_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
}

void DX12ProgramApi::SetDepthStencilState(const DepthStencilDesc& desc)
{
    m_pso_desc.DepthStencilState.DepthEnable = desc.depth_enable;
}

DescriptorHeapRange DX12ProgramApi::CreateSrv(ShaderType type, const std::string& name, uint32_t slot, const Resource::Ptr& ires)
{
    auto res = std::static_pointer_cast<DX12Resource>(ires);

    if (type == ShaderType::kPixel)
        m_context.ResourceBarrier(res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    else
        m_context.ResourceBarrier(res, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kSrv, GetBindingId(m_program_id, type, ResourceType::kSrv, slot), res->default_res.Get());

    if (descriptor.exist)
        return descriptor.handle;

    ComPtr<ID3D12ShaderReflection> reflector;
    _D3DReflect(m_blob_map[type]->GetBufferPointer(), m_blob_map[type]->GetBufferSize(), IID_PPV_ARGS(&reflector));

    D3D12_SHADER_INPUT_BIND_DESC res_desc = {};
    ASSERT_SUCCEEDED(reflector->GetResourceBindingDescByName(name.c_str(), &res_desc));

    D3D12_RESOURCE_DESC desc = res->default_res->GetDesc();

    switch (res_desc.Dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = desc.Width / res->stride;
        srv_desc.Buffer.StructureByteStride = res->stride;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        m_context.device->CreateShaderResourceView(res->default_res.Get(), &srv_desc, descriptor.handle.GetCpuHandle());

        break;
    }
    case D3D_SRV_DIMENSION_TEXTURE2D:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        m_context.device->CreateShaderResourceView(res->default_res.Get(), &srv_desc, descriptor.handle.GetCpuHandle());
        break;
    }
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        m_context.device->CreateShaderResourceView(res->default_res.Get(), &srv_desc, descriptor.handle.GetCpuHandle());
        break;
    }
    case D3D_SRV_DIMENSION_TEXTURECUBE:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT; // TODO tex_dec.Format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv_desc.TextureCube.MipLevels = desc.MipLevels;
        m_context.device->CreateShaderResourceView(res->default_res.Get(), &srv_desc, descriptor.handle.GetCpuHandle());
        break;
    }
    default:
        assert(false);
        break;
    }

    return descriptor.handle;
}

DescriptorHeapRange DX12ProgramApi::CreateUAV(ShaderType type, const std::string & name, uint32_t slot, const Resource::Ptr & ires)
{
    auto res = std::static_pointer_cast<DX12Resource>(ires);

    m_context.ResourceBarrier(res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kUav, GetBindingId(m_program_id, type, ResourceType::kUav, slot), res->default_res.Get());

    if (descriptor.exist)
        return descriptor.handle;

    ComPtr<ID3D12ShaderReflection> reflector;
    _D3DReflect(m_blob_map[type]->GetBufferPointer(), m_blob_map[type]->GetBufferSize(), IID_PPV_ARGS(&reflector));

    D3D12_SHADER_INPUT_BIND_DESC res_desc = {};
    ASSERT_SUCCEEDED(reflector->GetResourceBindingDescByName(name.c_str(), &res_desc));

    D3D12_RESOURCE_DESC desc = res->default_res->GetDesc();

    switch (res_desc.Dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = desc.Width / res->stride;
        uav_desc.Buffer.StructureByteStride = res->stride;
        m_context.device->CreateUnorderedAccessView(res->default_res.Get(), nullptr, &uav_desc, descriptor.handle.GetCpuHandle());

        break;
    }
    default:
        assert(false);
        break;
    }

    return descriptor.handle;
}

DescriptorHeapRange DX12ProgramApi::CreateCBV(ShaderType type, uint32_t slot, const ComPtr<ID3D12Resource>& res)
{
    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kCbv, GetBindingId(m_program_id, type, ResourceType::kCbv, slot), res.Get());

    if (descriptor.exist)
        return descriptor.handle;

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
    desc.BufferLocation = res->GetGPUVirtualAddress();
    desc.SizeInBytes = (res->GetDesc().Width + 255) & ~255;

    m_context.device->CreateConstantBufferView(&desc, descriptor.handle.GetCpuHandle());

    return descriptor.handle;
}

ComPtr<ID3D12Resource> DX12ProgramApi::CreateCBuffer(size_t buffer_size)
{
    ComPtr<ID3D12Resource> res;
    ComPtr<ID3D12Resource> buffer;
    buffer_size = (buffer_size + 255) & ~255;
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);
    m_context.device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&res));
    return res;
}

DescriptorHeapRange DX12ProgramApi::CreateSampler(ShaderType type, uint32_t slot, const SamplerDesc& desc)
{
    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kSampler, GetBindingId(m_program_id, type, ResourceType::kSampler, slot), nullptr);

    D3D12_SAMPLER_DESC sampler_desc = {};

    switch (desc.filter)
    {
    case SamplerFilter::kAnisotropic:
        sampler_desc.Filter = D3D12_FILTER_ANISOTROPIC;
        break;
    case SamplerFilter::kMinMagMipLinear:
        sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        break;
    case SamplerFilter::kComparisonMinMagMipLinear:
        sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        break;
    }

    switch (desc.mode)
    {
    case SamplerTextureAddressMode::kWrap:
        sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        break;
    case SamplerTextureAddressMode::kClamp:
        sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        break;
    }

    switch (desc.func)
    {
    case SamplerComparisonFunc::kNever:
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        break;
    case SamplerComparisonFunc::kAlways:
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        break;
    case SamplerComparisonFunc::kLess:
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
        break;
    }

    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_desc.MaxAnisotropy = 1;

    m_context.device->CreateSampler(&sampler_desc, descriptor.handle.GetCpuHandle());

    return descriptor.handle;
}

DescriptorHeapRange DX12ProgramApi::CreateRTV(uint32_t slot, const Resource::Ptr& ires)
{
    auto res = std::static_pointer_cast<DX12Resource>(ires);

    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kRtv, GetBindingId(m_program_id, ShaderType::kPixel, ResourceType::kRtv, slot), res->default_res.Get());

    m_context.ResourceBarrier(res, D3D12_RESOURCE_STATE_RENDER_TARGET);

    if (m_pso_desc.RTVFormats[slot] != res->default_res->GetDesc().Format)
    {
        m_pso_desc.RTVFormats[slot] = res->default_res->GetDesc().Format;
        m_changed_pso_desc = true;
    }

    if (m_pso_desc.SampleDesc.Count != res->default_res->GetDesc().SampleDesc.Count)
    {
        m_pso_desc.SampleDesc = res->default_res->GetDesc().SampleDesc;
        m_changed_pso_desc = true;
    }

    if (descriptor.exist)
        return descriptor.handle;

    m_context.device->CreateRenderTargetView(res->default_res.Get(), nullptr, descriptor.handle.GetCpuHandle());

    return descriptor.handle;
}

DescriptorHeapRange DX12ProgramApi::CreateDSV(const Resource::Ptr& ires)
{
    auto res = std::static_pointer_cast<DX12Resource>(ires);
    auto descriptor = m_context.descriptor_pool->GetDescriptor(ResourceType::kDsv, GetBindingId(m_program_id, ShaderType::kPixel, ResourceType::kDsv, 0), res->default_res.Get());

    m_context.ResourceBarrier(res, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto desc = res->default_res->GetDesc();

    DXGI_FORMAT format = desc.Format;
    if (format == DXGI_FORMAT_R32_TYPELESS)
    {
        format = DXGI_FORMAT_D32_FLOAT;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = format;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_desc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
        if (!descriptor.exist)
            m_context.device->CreateDepthStencilView(res->default_res.Get(), &dsv_desc, descriptor.handle.GetCpuHandle());
    }
    else if (!descriptor.exist)
    {
        m_context.device->CreateDepthStencilView(res->default_res.Get(), nullptr, descriptor.handle.GetCpuHandle());
    }

    if (m_pso_desc.DSVFormat != format)
    {
        m_pso_desc.DSVFormat = format;
        m_changed_pso_desc = true;
    }

    if (m_pso_desc.SampleDesc.Count != res->default_res->GetDesc().SampleDesc.Count)
    {
        m_pso_desc.SampleDesc = res->default_res->GetDesc().SampleDesc;
        m_changed_pso_desc = true;
    }

    return descriptor.handle;
}

void DX12ProgramApi::SetRootSignature(ID3D12RootSignature * pRootSignature)
{
    if (m_blob_map.count(ShaderType::kCompute))
        m_context.command_list->SetComputeRootSignature(pRootSignature);
    else
        m_context.command_list->SetGraphicsRootSignature(pRootSignature);
}

void DX12ProgramApi::SetRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (RootParameterIndex == -1)
        return;
    if (m_blob_map.count(ShaderType::kCompute))
        m_context.command_list->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
    else
        m_context.command_list->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
}

void DX12ProgramApi::SetRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (RootParameterIndex == -1)
        return;
    if (m_blob_map.count(ShaderType::kCompute))
        m_context.command_list->SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
    else
        m_context.command_list->SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
}

std::vector<D3D12_INPUT_ELEMENT_DESC> DX12ProgramApi::GetInputLayout(ComPtr<ID3D12ShaderReflection> reflector)
{
    D3D12_SHADER_DESC shader_desc = {};
    reflector->GetDesc(&shader_desc);

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_layout_desc;
    for (UINT i = 0; i < shader_desc.InputParameters; ++i)
    {
        D3D12_SIGNATURE_PARAMETER_DESC param_desc = {};
        reflector->GetInputParameterDesc(i, &param_desc);

        D3D12_INPUT_ELEMENT_DESC layout = {};
        layout.SemanticName = param_desc.SemanticName;
        layout.SemanticIndex = param_desc.SemanticIndex;
        layout.InputSlot = i;
        layout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        layout.InstanceDataStepRate = 0;

        if (param_desc.Mask == 1)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                layout.Format = DXGI_FORMAT_R32_UINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                layout.Format = DXGI_FORMAT_R32_SINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                layout.Format = DXGI_FORMAT_R32_FLOAT;
        }
        else if (param_desc.Mask <= 3)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                layout.Format = DXGI_FORMAT_R32G32_UINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                layout.Format = DXGI_FORMAT_R32G32_SINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                layout.Format = DXGI_FORMAT_R32G32_FLOAT;
        }
        else if (param_desc.Mask <= 7)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                layout.Format = DXGI_FORMAT_R32G32B32_UINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                layout.Format = DXGI_FORMAT_R32G32B32_SINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                layout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        }
        else if (param_desc.Mask <= 15)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                layout.Format = DXGI_FORMAT_R32G32B32A32_UINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                layout.Format = DXGI_FORMAT_R32G32B32A32_SINT;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                layout.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
        input_layout_desc.push_back(layout);
    }

    return input_layout_desc;
}

void DX12ProgramApi::CreateGraphicsPSO()
{
    if (!m_changed_pso_desc)
        return;
    m_changed_pso_desc = false;

    ComPtr<ID3D12ShaderReflection> reflector;
    _D3DReflect(m_blob_map[ShaderType::kVertex]->GetBufferPointer(), m_blob_map[ShaderType::kVertex]->GetBufferSize(), IID_PPV_ARGS(&reflector));
    auto input_layout_elements = GetInputLayout(reflector);
    D3D12_INPUT_LAYOUT_DESC input_layout = {};
    input_layout.NumElements = input_layout_elements.size();
    input_layout.pInputElementDescs = input_layout_elements.data();

    m_pso_desc.InputLayout = input_layout;
    m_pso_desc.pRootSignature = m_root_signature.Get();
    m_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_pso_desc.SampleMask = UINT_MAX;
    m_pso_desc.NumRenderTargets = m_num_rtv;

    auto it = m_pso.find(m_pso_desc);
    if (it == m_pso.end())
    {
        ASSERT_SUCCEEDED(m_context.device->CreateGraphicsPipelineState(&m_pso_desc, IID_PPV_ARGS(&m_current_pso)));
        m_pso.emplace(std::piecewise_construct,
            std::forward_as_tuple(m_pso_desc),
            std::forward_as_tuple(m_current_pso));
    }
    else
    {
        m_current_pso = it->second;
    }

    m_context.command_list->SetPipelineState(m_current_pso.Get());
}

void DX12ProgramApi::CreateComputePSO()
{
    if (!m_compute_pso)
    {
        m_compute_pso_desc.pRootSignature = m_root_signature.Get();
        ASSERT_SUCCEEDED(m_context.device->CreateComputePipelineState(&m_compute_pso_desc, IID_PPV_ARGS(&m_compute_pso)));
    }
    m_context.command_list->SetPipelineState(m_compute_pso.Get());
    m_compute_pso->SetName(L"m_compute_pso");
}

void DX12ProgramApi::ApplyBindings()
{
    if (m_blob_map.count(ShaderType::kCompute))
        CreateComputePSO();
    else
        CreateGraphicsPSO();

    for (auto &x : m_cbv_layout)
    {
        BufferLayout& buffer = x.second;
        auto& buffer_data = buffer.GetBuffer();
        bool change_buffer = buffer.SyncData();
        change_buffer = change_buffer || !m_cbv_offset[m_context.GetFrameIndex()].count(x.first);
        if (change_buffer && m_cbv_offset[m_context.GetFrameIndex()].count(x.first))
            ++m_cbv_offset[m_context.GetFrameIndex()][x.first];
        if (m_cbv_offset[m_context.GetFrameIndex()][x.first] >= m_cbv_buffer[m_context.GetFrameIndex()][x.first].size())
            m_cbv_buffer[m_context.GetFrameIndex()][x.first].push_back(CreateCBuffer(buffer_data.size()));

        auto& res = m_cbv_buffer[m_context.GetFrameIndex()][x.first][m_cbv_offset[m_context.GetFrameIndex()][x.first]];
        if (change_buffer)
        {
            CD3DX12_RANGE range(0, 0);
            char* cbvGPUAddress = nullptr;
            ASSERT_SUCCEEDED(res->Map(0, &range, reinterpret_cast<void**>(&cbvGPUAddress)));
            memcpy(cbvGPUAddress, buffer_data.data(), buffer_data.size());
            res->Unmap(0, &range);
            m_changed_binding = true;
        }

        if (m_use_cbv_table)
        {
            AttachCBV(std::get<0>(x.first), std::get<1>(x.first), res);
        }
    }

    if (m_changed_om)
        OMSetRenderTargets();

    if (!m_changed_binding)
        return;
    m_changed_binding = false;

    DescriptorHeapRange res_heap = m_context.descriptor_pool->Allocate(ResourceType::kSrv, m_num_cbv + m_num_srv + m_num_uav);
    static DescriptorHeapRange sampler_heap = m_context.descriptor_pool->Allocate(ResourceType::kSampler, m_num_sampler);

    ID3D12DescriptorHeap* descriptor_heaps[2] = {};
    size_t descriptor_count = 0;
    if (res_heap.GetSize())
        descriptor_heaps[descriptor_count++] = res_heap.GetHeap().Get();
    if (sampler_heap.GetSize())
        descriptor_heaps[descriptor_count++] = sampler_heap.GetHeap().Get();
    m_context.command_list->SetDescriptorHeaps(descriptor_count, descriptor_heaps);

    for (auto & x : m_heap_ranges)
    {
        D3D12_DESCRIPTOR_RANGE_TYPE range_type;
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        std::reference_wrapper<DescriptorHeapRange> heap_range = res_heap;
        bool is_rtv_dsv = false;
        switch (std::get<1>(x.first))
        {
        case ResourceType::kSrv:
            range_type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            break;
        case ResourceType::kUav:
            range_type = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            break;
        case ResourceType::kCbv:
            range_type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            break;
        case ResourceType::kSampler:
            range_type = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            heap_range = std::ref(sampler_heap);
            break;
        case ResourceType::kRtv:
        case ResourceType::kDsv:
            is_rtv_dsv = true;
            break;
        }

        if (is_rtv_dsv)
            continue;

        D3D12_CPU_DESCRIPTOR_HANDLE view_handle = x.second.GetCpuHandle();

        D3D12_CPU_DESCRIPTOR_HANDLE binding_handle = heap_range.get().GetCpuHandle(m_binding_layout[{std::get<0>(x.first), range_type}].table.heap_offset + std::get<2>(x.first));

        m_context.device->CopyDescriptors(
            1, &binding_handle, nullptr,
            1, &view_handle, nullptr,
            heap_type);
    }

    for (auto &x : m_binding_layout)
    {
        if (x.second.type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            std::reference_wrapper<DescriptorHeapRange> heap_range = res_heap;
            switch (std::get<1>(x.first))
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                heap_range = std::ref(sampler_heap);
                break;
            }
            if (x.second.root_param_index != -1)
                SetRootDescriptorTable(x.second.root_param_index, heap_range.get().GetGpuHandle(x.second.table.root_param_heap_offset));
        }
        else if (x.second.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
        {
            for (size_t slot = 0; slot < x.second.view.root_param_num; ++slot)
            {
                auto& shader_type = std::get<0>(x.first);
                auto& res = m_cbv_buffer[m_context.GetFrameIndex()][{shader_type, slot}][m_cbv_offset[m_context.GetFrameIndex()][{shader_type, slot}]];
                SetRootConstantBufferView(x.second.root_param_index + slot, res->GetGPUVirtualAddress());
            }
        }
    }
}

void DX12ProgramApi::ParseShaders()
{
    m_num_cbv = 0;
    m_num_srv = 0;
    m_num_uav = 0;
    m_num_rtv = 0;
    m_num_sampler = 0;

    size_t num_resources = 0;
    size_t num_samplers = 0;

    std::vector<D3D12_ROOT_PARAMETER> root_parameters;
    std::deque<std::array<D3D12_DESCRIPTOR_RANGE, 4>> descriptor_table_ranges;
    for (auto& shader_blob : m_blob_map)
    {
        size_t num_cbv = 0;
        size_t num_srv = 0;
        size_t num_uav = 0;
        size_t num_sampler = 0;

        ComPtr<ID3D12ShaderReflection> reflector;
        _D3DReflect(shader_blob.second->GetBufferPointer(), shader_blob.second->GetBufferSize(), IID_PPV_ARGS(&reflector));

        D3D12_SHADER_DESC desc = {};
        reflector->GetDesc(&desc);
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC res_desc = {};
            ASSERT_SUCCEEDED(reflector->GetResourceBindingDesc(i, &res_desc));
            ResourceType res_type;
            switch (res_desc.Type)
            {
            case D3D_SIT_SAMPLER:
                num_sampler = std::max<size_t>(num_sampler, res_desc.BindPoint + res_desc.BindCount);
                res_type = ResourceType::kSampler;
                break;
            case D3D_SIT_CBUFFER:
                num_cbv = std::max<size_t>(num_cbv, res_desc.BindPoint + res_desc.BindCount);
                res_type = ResourceType::kCbv;
                break;
            case D3D_SIT_TBUFFER:
            case D3D_SIT_TEXTURE:
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
                num_srv = std::max<size_t>(num_srv, res_desc.BindPoint + res_desc.BindCount);
                res_type = ResourceType::kSrv;
                break;
            default:
                num_uav = std::max<size_t>(num_uav, res_desc.BindPoint + res_desc.BindCount);
                res_type = ResourceType::kUav;
                break;
            }
        }

        if (shader_blob.first == ShaderType::kPixel)
        {
            for (UINT i = 0; i < desc.OutputParameters; ++i)
            {
                D3D12_SIGNATURE_PARAMETER_DESC param_desc = {};
                reflector->GetOutputParameterDesc(i, &param_desc);
                m_num_rtv = std::max<size_t>(m_num_rtv, param_desc.Register + 1);
            }
        }

        D3D12_SHADER_VISIBILITY visibility;
        switch (shader_blob.first)
        {
        case ShaderType::kVertex:
            visibility = D3D12_SHADER_VISIBILITY_VERTEX;
            break;
        case ShaderType::kPixel:
            visibility = D3D12_SHADER_VISIBILITY_PIXEL;
            break;
        case ShaderType::kCompute:
            visibility = D3D12_SHADER_VISIBILITY_ALL;
            break;
        case ShaderType::kGeometry:
            visibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
            break;
        }

        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SRV}].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SRV}].table.root_param_heap_offset = num_resources;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SRV}].table.heap_offset = num_resources;

        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_UAV}].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_UAV}].table.root_param_heap_offset = num_resources;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_UAV}].table.heap_offset = num_resources + num_srv;

        if (m_use_cbv_table)
        {
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].table.root_param_heap_offset = num_resources;
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].table.heap_offset = num_resources + num_srv + num_uav;
        }
        else
        {
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].type = D3D12_ROOT_PARAMETER_TYPE_CBV;
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].view.root_param_num = num_cbv;
        }

        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}].table.root_param_heap_offset = num_samplers;
        m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}].table.heap_offset = num_samplers;

        if (!num_srv)
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SRV}].root_param_index = -1;
        if (!num_uav)
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_UAV}].root_param_index = -1;
        if (!num_cbv)
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].root_param_index = -1;
        if (!num_sampler)
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}].root_param_index = -1;

        descriptor_table_ranges.emplace_back();
        size_t index = 0;

        if (((num_srv + num_uav) > 0) || (m_use_cbv_table && num_cbv > 0))
        {
            if (num_srv)
            {
                descriptor_table_ranges.back()[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                descriptor_table_ranges.back()[index].NumDescriptors = num_srv;
                descriptor_table_ranges.back()[index].BaseShaderRegister = 0;
                descriptor_table_ranges.back()[index].RegisterSpace = 0;
                descriptor_table_ranges.back()[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ++index;
            }

            if (num_uav)
            {
                descriptor_table_ranges.back()[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                descriptor_table_ranges.back()[index].NumDescriptors = num_uav;
                descriptor_table_ranges.back()[index].BaseShaderRegister = 0;
                descriptor_table_ranges.back()[index].RegisterSpace = 0;
                descriptor_table_ranges.back()[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ++index;
            }

            if (m_use_cbv_table && num_cbv)
            {
                descriptor_table_ranges.back()[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                descriptor_table_ranges.back()[index].NumDescriptors = num_cbv;
                descriptor_table_ranges.back()[index].BaseShaderRegister = 0;
                descriptor_table_ranges.back()[index].RegisterSpace = 0;
                descriptor_table_ranges.back()[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ++index;
            }

            D3D12_ROOT_DESCRIPTOR_TABLE descriptorTableTexture;
            descriptorTableTexture.NumDescriptorRanges = index;
            descriptorTableTexture.pDescriptorRanges = &descriptor_table_ranges.back()[0];

            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SRV}].root_param_index = root_parameters.size();
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_UAV}].root_param_index = root_parameters.size();
            if (m_use_cbv_table)
                m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].root_param_index = root_parameters.size();

            root_parameters.emplace_back();
            root_parameters.back().ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_parameters.back().DescriptorTable = descriptorTableTexture;
            root_parameters.back().ShaderVisibility = visibility;
        }

        if (!m_use_cbv_table && num_cbv > 0)
        {
            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_CBV}].root_param_index = root_parameters.size();

            for (size_t j = 0; j < num_cbv; ++j)
            {
                root_parameters.emplace_back();
                root_parameters.back().ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                root_parameters.back().Descriptor.ShaderRegister = j;
                root_parameters.back().ShaderVisibility = visibility;
            }
        }
        
        if (num_sampler)
        {
            descriptor_table_ranges.back()[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            descriptor_table_ranges.back()[index].NumDescriptors = num_sampler;
            descriptor_table_ranges.back()[index].BaseShaderRegister = 0;
            descriptor_table_ranges.back()[index].RegisterSpace = 0;
            descriptor_table_ranges.back()[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_DESCRIPTOR_TABLE descriptorTableSampler;
            descriptorTableSampler.NumDescriptorRanges = 1;
            descriptorTableSampler.pDescriptorRanges = &descriptor_table_ranges.back()[index];

            m_binding_layout[{shader_blob.first, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}].root_param_index = root_parameters.size();

            root_parameters.emplace_back();
            root_parameters.back().ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_parameters.back().DescriptorTable = descriptorTableSampler;
            root_parameters.back().ShaderVisibility = visibility;
        }

        num_resources += num_cbv + num_srv + num_uav;
        num_samplers += num_sampler;

        m_num_cbv += num_cbv;
        m_num_srv += num_srv;
        m_num_uav += num_uav;
        m_num_sampler += num_sampler;
    }

    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(root_parameters.size(),
        root_parameters.data(),
        0,
        nullptr,
        rootSignatureFlags);

    ID3DBlob* signature;
    ID3DBlob* errorBuff;
    ASSERT_SUCCEEDED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff),
        "%s", (char*)errorBuff->GetBufferPointer());
    ASSERT_SUCCEEDED(m_context.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_root_signature)));
}

void DX12ProgramApi::OnPresent()
{
    m_cbv_offset[m_context.GetFrameIndex()].clear();
}

void DX12ProgramApi::OMSetRenderTargets()
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> om_rtv;
    for (uint32_t slot = 0; slot < m_num_rtv; ++slot)
    {
        auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kRtv, slot });
        om_rtv.emplace_back();
        if (it != m_heap_ranges.end())
            om_rtv.back() = it->second.GetCpuHandle();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE om_dsv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE* om_dsv_ptr = nullptr;
    auto it = m_heap_ranges.find({ ShaderType::kPixel, ResourceType::kDsv, 0 });
    if (it != m_heap_ranges.end())
    {
        om_dsv = it->second.GetCpuHandle();
        om_dsv_ptr = &om_dsv;
    }
    m_context.command_list->OMSetRenderTargets(om_rtv.size(), om_rtv.data(), FALSE, om_dsv_ptr);
}
