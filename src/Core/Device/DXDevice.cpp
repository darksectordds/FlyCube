#include "Device/DXDevice.h"
#include <Adapter/DXAdapter.h>
#include <Swapchain/DXSwapchain.h>
#include <CommandList/DXCommandList.h>
#include <Shader/DXShader.h>
#include <Fence/DXFence.h>
#include <View/DXView.h>
#include <Program/DXProgram.h>
#include <Pipeline/DXGraphicsPipeline.h>
#include <Pipeline/DXComputePipeline.h>
#include <Pipeline/DXRayTracingPipeline.h>
#include <Framebuffer/DXFramebuffer.h>
#include <Utilities/DXUtility.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <gli/dx.hpp>

D3D12_RESOURCE_STATES ConvertSate(ResourceState state)
{
    switch (state)
    {
    case ResourceState::kUndefined:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::kGenericRead:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case ResourceState::kPresent:
        return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::kClearColor:
    case ResourceState::kRenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::kClearDepth:
    case ResourceState::kDepthTarget:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::kUnorderedAccess:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::kPixelShaderResource:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case ResourceState::kNonPixelShaderResource:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::kCopyDest:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::kCopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::kVertexAndConstantBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::kIndexBuffer:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::kRaytracingAccelerationStructure:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    case ResourceState::kShadingRateSource:
        return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    default:
        assert(false);
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

// RenderDoc UUID {A7AA6116-9C8D-4BBA-9083-B4D816B71B78}
static const GUID IRenderDoc_uuid = {
    0xa7aa6116, 0x9c8d, 0x4bba, {0x90, 0x83, 0xb4, 0xd8, 0x16, 0xb7, 0x1b, 0x78} };

DXDevice::DXDevice(DXAdapter& adapter)
    : m_adapter(adapter)
    , m_cpu_descriptor_pool(*this)
    , m_gpu_descriptor_pool(*this)
{
    ASSERT_SUCCEEDED(D3D12CreateDevice(m_adapter.GetAdapter().Get(), D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&m_device)));
    m_device.As(&m_device5);

    ComPtr<IUnknown> renderdoc;
    m_device->QueryInterface(IRenderDoc_uuid, reinterpret_cast<void**>(renderdoc.GetAddressOf()));
    m_is_under_graphics_debugger = renderdoc || GetModuleHandleW(L"WinPixCaptureReplay.dll");

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 feature_support5 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feature_support5, sizeof(feature_support5))))
    {
        m_is_dxr_supported = feature_support5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
        m_is_render_passes_supported = feature_support5.RenderPassesTier >= D3D12_RENDER_PASS_TIER_0;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 feature_support6 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &feature_support6, sizeof(feature_support6))))
    {
        m_is_variable_rate_shading_supported = feature_support6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
        m_shading_rate_image_tile_size = feature_support6.ShadingRateImageTileSize;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    ASSERT_SUCCEEDED(m_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_command_queue)));

    static const bool debug_enabled = IsDebuggerPresent();
    if (debug_enabled)
    {
        ComPtr<ID3D12InfoQueue> info_queue;
        if (SUCCEEDED(m_device.As(&info_queue)))
        {
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

            D3D12_MESSAGE_SEVERITY severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO,
            };

            D3D12_MESSAGE_ID deny_ids[] =
            {
                D3D12_MESSAGE_ID_COPY_DESCRIPTORS_INVALID_RANGES,
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities = std::size(severities);
            filter.DenyList.pSeverityList = severities;
            filter.DenyList.NumIDs = std::size(deny_ids);
            filter.DenyList.pIDList = deny_ids;
            info_queue->PushStorageFilter(&filter);
        }

        /*ComPtr<ID3D12DebugDevice2> debug_device;
        m_device.As(&debug_device);
        D3D12_DEBUG_FEATURE debug_feature = D3D12_DEBUG_FEATURE_CONSERVATIVE_RESOURCE_STATE_TRACKING;
        debug_device->SetDebugParameter(D3D12_DEBUG_DEVICE_PARAMETER_FEATURE_FLAGS, &debug_feature, sizeof(debug_feature));*/
    }
}

uint32_t DXDevice::GetTextureDataPitchAlignment() const
{
    return D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
}

std::shared_ptr<Swapchain> DXDevice::CreateSwapchain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t frame_count, bool vsync)
{
    return std::make_shared<DXSwapchain>(*this, window, width, height, frame_count, vsync);
}

std::shared_ptr<CommandList> DXDevice::CreateCommandList()
{
    return std::make_shared<DXCommandList>(*this);
}

std::shared_ptr<Fence> DXDevice::CreateFence(uint64_t initial_value)
{
    return std::make_shared<DXFence>(*this, initial_value);
}

std::shared_ptr<Resource> DXDevice::CreateTexture(uint32_t bind_flag, gli::format format, uint32_t sample_count, int width, int height, int depth, int mip_levels)
{
    DXGI_FORMAT dx_format = static_cast<DXGI_FORMAT>(gli::dx().translate(format).DXGIFormat.DDS);
    if (bind_flag & BindFlag::kShaderResource && dx_format == DXGI_FORMAT_D32_FLOAT)
        dx_format = DXGI_FORMAT_R32_TYPELESS;

    std::shared_ptr<DXResource> res = std::make_shared<DXResource>(*this);
    res->resource_type = ResourceType::kTexture;
    res->format = format;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth;
    desc.MipLevels = mip_levels;
    desc.Format = dx_format;

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_check_desc = {};
    ms_check_desc.Format = desc.Format;
    ms_check_desc.SampleCount = sample_count;
    m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms_check_desc, sizeof(ms_check_desc));
    desc.SampleDesc.Count = sample_count;
    desc.SampleDesc.Quality = ms_check_desc.NumQualityLevels - 1;

    if (bind_flag & BindFlag::kRenderTarget)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (bind_flag & BindFlag::kDepthStencil)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (bind_flag & BindFlag::kUnorderedAccess)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ResourceState state = ResourceState::kCopyDest;

    D3D12_CLEAR_VALUE* p_clear_value = nullptr;
    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = dx_format;
    if (bind_flag & BindFlag::kRenderTarget)
    {
        clear_value.Color[0] = 0.0f;
        clear_value.Color[1] = 0.0f;
        clear_value.Color[2] = 0.0f;
        clear_value.Color[3] = 1.0f;
        p_clear_value = &clear_value;
    }
    else if (bind_flag & BindFlag::kDepthStencil)
    {
        clear_value.DepthStencil.Depth = 1.0f;
        clear_value.DepthStencil.Stencil = 0;
        if (dx_format == DXGI_FORMAT_R32_TYPELESS)
            clear_value.Format = DXGI_FORMAT_D32_FLOAT;
        p_clear_value = &clear_value;
    }

    ASSERT_SUCCEEDED(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        ConvertSate(state),
        p_clear_value,
        IID_PPV_ARGS(&res->resource)));
    res->desc = desc;
    res->GetGlobalResourceStateTracker().SetResourceState(state);
    return res;
}

std::shared_ptr<Resource> DXDevice::CreateBuffer(uint32_t bind_flag, uint32_t buffer_size, MemoryType memory_type)
{
    if (buffer_size == 0)
        return {};

    std::shared_ptr<DXResource> res = std::make_shared<DXResource>(*this);

    if (bind_flag & BindFlag::kConstantBuffer)
        buffer_size = (buffer_size + 255) & ~255;

    auto desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);

    ResourceState state = ResourceState::kUndefined;
    res->memory_type = memory_type;
    res->resource_type = ResourceType::kBuffer;

    if (bind_flag & BindFlag::kRenderTarget)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (bind_flag & BindFlag::kDepthStencil)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (bind_flag & BindFlag::kUnorderedAccess)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (bind_flag & BindFlag::kAccelerationStructure)
        state = ResourceState::kRaytracingAccelerationStructure;

    D3D12_HEAP_TYPE heap_type;
    if (memory_type == MemoryType::kDefault)
    {
        heap_type = D3D12_HEAP_TYPE_DEFAULT;
    }
    else if (memory_type == MemoryType::kUpload)
    {
        heap_type = D3D12_HEAP_TYPE_UPLOAD;
        state = ResourceState::kGenericRead;
    }

    m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(heap_type),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        ConvertSate(state),
        nullptr,
        IID_PPV_ARGS(&res->resource));
    res->desc = desc;
    res->GetGlobalResourceStateTracker().SetResourceState(state);
    return res;
}

std::shared_ptr<Resource> DXDevice::CreateSampler(const SamplerDesc& desc)
{
    std::shared_ptr<DXResource> res = std::make_shared<DXResource>(*this);
    D3D12_SAMPLER_DESC& sampler_desc = res->sampler_desc;

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
    sampler_desc.MaxLOD = std::numeric_limits<float>::max();
    sampler_desc.MaxAnisotropy = 1;

    return res;
}

std::shared_ptr<View> DXDevice::CreateView(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc)
{
    return std::make_shared<DXView>(*this, resource, view_desc);
}

std::shared_ptr<Framebuffer> DXDevice::CreateFramebuffer(const std::shared_ptr<Pipeline>& pipeline, uint32_t width, uint32_t height,
                                                         const std::vector<std::shared_ptr<View>>& rtvs, const std::shared_ptr<View>& dsv)
{
    return std::make_shared<DXFramebuffer>(rtvs, dsv);
}

std::shared_ptr<Shader> DXDevice::CompileShader(const ShaderDesc& desc)
{
    return std::make_shared<DXShader>(desc);
}

std::shared_ptr<Program> DXDevice::CreateProgram(const std::vector<std::shared_ptr<Shader>>& shaders)
{
    return std::make_shared<DXProgram>(*this, shaders);
}

std::shared_ptr<Pipeline> DXDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    return std::make_shared<DXGraphicsPipeline>(*this, desc);
}

std::shared_ptr<Pipeline> DXDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    return std::make_shared<DXComputePipeline>(*this, desc);
}

std::shared_ptr<Pipeline> DXDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
{
    return std::make_shared<DXRayTracingPipeline>(*this, desc);
}

D3D12_RAYTRACING_GEOMETRY_DESC FillRaytracingGeometryDesc(const BufferDesc& vertex, const BufferDesc& index)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};

    auto vertex_res = std::static_pointer_cast<DXResource>(vertex.res);
    auto index_res = std::static_pointer_cast<DXResource>(index.res);

    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
    ASSERT(!!vertex_res);

    auto vertex_stride = gli::detail::bits_per_pixel(vertex.format) / 8;
    geometry_desc.Triangles.VertexBuffer.StartAddress = vertex_res->resource->GetGPUVirtualAddress() + vertex.offset * vertex_stride;
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = vertex_stride;
    geometry_desc.Triangles.VertexFormat = static_cast<DXGI_FORMAT>(gli::dx().translate(vertex.format).DXGIFormat.DDS);
    geometry_desc.Triangles.VertexCount = vertex.count;
    if (index_res)
    {
        auto index_stride = gli::detail::bits_per_pixel(index.format) / 8;
        geometry_desc.Triangles.IndexBuffer = index_res->resource->GetGPUVirtualAddress() + index.offset * index_stride;
        geometry_desc.Triangles.IndexFormat = static_cast<DXGI_FORMAT>(gli::dx().translate(index.format).DXGIFormat.DDS);
        geometry_desc.Triangles.IndexCount = index.count;
    }

    return geometry_desc;
}

std::shared_ptr<Resource> DXDevice::CreateAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    std::shared_ptr<DXResource> res = std::static_pointer_cast<DXResource>(CreateBuffer(BindFlag::kUnorderedAccess | BindFlag::kAccelerationStructure, info.ResultDataMaxSizeInBytes, MemoryType::kDefault));
    res->resource_type = inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL ? ResourceType::kBottomLevelAS : ResourceType::kTopLevelAS;
    res->prebuild_info = { info.ScratchDataSizeInBytes };
    return res;
}

std::shared_ptr<Resource> DXDevice::CreateBottomLevelAS(const BufferDesc& vertex, const BufferDesc& index)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = FillRaytracingGeometryDesc(vertex, index);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometry_desc;
    return CreateAccelerationStructure(inputs);
}

std::shared_ptr<Resource> DXDevice::CreateTopLevelAS(uint32_t instance_count)
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = instance_count;
    return CreateAccelerationStructure(inputs);
}

bool DXDevice::IsDxrSupported() const
{
    return m_is_dxr_supported;
}

bool DXDevice::IsVariableRateShadingSupported() const
{
    return m_is_variable_rate_shading_supported;
}

uint32_t DXDevice::GetShadingRateImageTileSize() const
{
    return m_shading_rate_image_tile_size;
}

void DXDevice::Wait(const std::shared_ptr<Fence>& fence, uint64_t value)
{
    decltype(auto) dx_fence = fence->As<DXFence>();
    ASSERT_SUCCEEDED(m_command_queue->Wait(dx_fence.GetFence().Get(), value));
}

void DXDevice::Signal(const std::shared_ptr<Fence>& fence, uint64_t value)
{
    decltype(auto) dx_fence = fence->As<DXFence>();
    ASSERT_SUCCEEDED(m_command_queue->Signal(dx_fence.GetFence().Get(), value));
}

void DXDevice::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& command_lists)
{
    std::vector<ID3D12CommandList*> dx_command_lists;
    for (auto& command_list : command_lists)
    {
        if (!command_list)
            continue;
        decltype(auto) dx_command_list = command_list->As<DXCommandList>();
        dx_command_lists.emplace_back(dx_command_list.GetCommandList().Get());
    }
    if (!dx_command_lists.empty())
        m_command_queue->ExecuteCommandLists(dx_command_lists.size(), dx_command_lists.data());
}

DXAdapter& DXDevice::GetAdapter()
{
    return m_adapter;
}

ComPtr<ID3D12Device> DXDevice::GetDevice()
{
    return m_device;
}

ComPtr<ID3D12CommandQueue> DXDevice::GetCommandQueue()
{
    return m_command_queue;
}

DXCPUDescriptorPool& DXDevice::GetCPUDescriptorPool()
{
    return m_cpu_descriptor_pool;
}

DXGPUDescriptorPool& DXDevice::GetGPUDescriptorPool()
{
    return m_gpu_descriptor_pool;
}

bool DXDevice::IsRenderPassesSupported() const
{
    return m_is_render_passes_supported;
}

bool DXDevice::IsUnderGraphicsDebugger() const
{
    return m_is_under_graphics_debugger;
}
