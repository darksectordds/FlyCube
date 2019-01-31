#include "VKViewCreater.h"
#include <Utilities/State.h>
#include <Texture/DXGIFormatHelper.h>
#include <memory>

VKViewCreater::VKViewCreater(VKContext& context, const IShaderBlobProvider& shader_provider)
    : m_context(context)
    , m_shader_provider(shader_provider)
{
}

VKView::Ptr VKViewCreater::CreateView()
{
    return std::make_shared<VKView>();
}

void VKViewCreater::OnLinkProgram()
{
    auto shader_types = m_shader_provider.GetShaderTypes();
    for (auto & shader_type : shader_types)
    {
        auto spirv = m_shader_provider.GetBlobByType(shader_type);
        assert(spirv.size % 4 == 0);
        std::vector<uint32_t> spirv32((uint32_t*)spirv.data, ((uint32_t*)spirv.data) + spirv.size / 4);

        ParseShader(shader_type, spirv32);
    }
}

void VKViewCreater::ParseShader(ShaderType shader_type, const std::vector<uint32_t>& spirv_binary)
{
    m_shader_ref.emplace(shader_type, spirv_binary);
    spirv_cross::CompilerHLSL& compiler = m_shader_ref.find(shader_type)->second.compiler;
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    auto generate_bindings = [&](const std::vector<spirv_cross::Resource>& resources, VkDescriptorType res_type)
    {
        for (auto& res : resources)
        {
            auto& info = m_shader_ref.find(shader_type)->second.resources[res.name];
            info.res = res;
            auto &type = compiler.get_type(res.base_type_id);

            if (type.basetype == spirv_cross::SPIRType::BaseType::Image && type.image.dim == spv::Dim::DimBuffer)
            {
                if (res_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                {
                    res_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                }
                else if (res_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                {
                    res_type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                }
            }

            info.descriptor_type = res_type;
        }
    };

    generate_bindings(resources.uniform_buffers, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    generate_bindings(resources.separate_images, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    generate_bindings(resources.separate_samplers, VK_DESCRIPTOR_TYPE_SAMPLER);
    generate_bindings(resources.storage_buffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    generate_bindings(resources.storage_images, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}

VKView::Ptr VKViewCreater::GetEmptyDescriptor(ResourceType res_type)
{
    static std::map<ResourceType, VKView::Ptr> empty_handles;
    auto it = empty_handles.find(res_type);
    if (it == empty_handles.end())
        it = empty_handles.emplace(res_type, CreateView()).first;
    return std::static_pointer_cast<VKView>(it->second);
}

VKView::Ptr VKViewCreater::GetView(uint32_t m_program_id, ShaderType shader_type, ResourceType res_type, uint32_t slot, const ViewDesc& view_desc, const std::string& name, const Resource::Ptr& ires)
{
    if (!ires)
        return GetEmptyDescriptor(res_type);

    VKResource& res = static_cast<VKResource&>(*ires);

    View::Ptr& iview = ires->GetView({m_program_id, shader_type, res_type, slot}, view_desc);
    if (iview)
        return std::static_pointer_cast<VKView>(iview);

    VKView::Ptr view = CreateView();
    iview = view;

    switch (res_type)
    {
    case ResourceType::kSrv:
    case ResourceType::kUav:
        CreateSrv(shader_type, name, slot, view_desc, res, *view);
        break;
    case ResourceType::kRtv:
    case ResourceType::kDsv:
        CreateRTV(slot, view_desc, res, *view);
        break;
    }

    return view;
}

void VKViewCreater::CreateSrv(ShaderType type, const std::string& name, uint32_t slot, const ViewDesc& view_desc, const VKResource& res, VKView& handle)
{
    ShaderRef& shader_ref = m_shader_ref.find(type)->second;
    auto& ref_res = shader_ref.resources[name];
    auto& res_type = shader_ref.compiler.get_type(ref_res.res.type_id);
    auto& dim = res_type.image.dim;

    if (res_type.basetype == spirv_cross::SPIRType::BaseType::Struct)
        return;

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = res.image.res;
    view_info.format = res.image.format;
    view_info.subresourceRange.aspectMask = m_context.GetAspectFlags(view_info.format);
    view_info.subresourceRange.baseMipLevel = view_desc.level;
    if (view_desc.count == -1)
        view_info.subresourceRange.levelCount = res.image.level_count - view_info.subresourceRange.baseMipLevel;
    else
        view_info.subresourceRange.levelCount = view_desc.count;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = res.image.array_layers;

    switch (dim)
    {
    case spv::Dim::Dim1D:
    {
        if (res_type.image.arrayed)
            view_info.viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        else
            view_info.viewType = VK_IMAGE_VIEW_TYPE_1D;
        break;
    }
    case spv::Dim::Dim2D:
    {
        if (res_type.image.arrayed)
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        else
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;
    }
    case spv::Dim::Dim3D:
    {
        view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    }
    case spv::Dim::DimCube:
    {
        if (res_type.image.arrayed)
            view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        else
            view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    }   
    default:
    {
        ASSERT(false);
        break;
    }
    }
    ASSERT_SUCCEEDED(vkCreateImageView(m_context.m_device, &view_info, nullptr, &handle.srv));
}

void VKViewCreater::CreateRTV(uint32_t slot, const ViewDesc& view_desc, const VKResource& res, VKView& handle)
{
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = res.image.res;
    view_info.format = res.image.format;
    if (res.image.array_layers > 1)
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    else
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange.aspectMask = m_context.GetAspectFlags(view_info.format);
    view_info.subresourceRange.baseMipLevel = view_desc.level;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = res.image.array_layers;

    ASSERT_SUCCEEDED(vkCreateImageView(m_context.m_device, &view_info, nullptr, &handle.om));
}