#include "Shader/SpirvShader.h"
#include "Shader/SpirvCompiler.h"
#include <spirv_hlsl.hpp>

SpirvShader::SpirvShader(const ShaderDesc& desc)
    : m_type(desc.type)
{
    SpirvOption option = {};
    option.auto_map_bindings = true;
    option.hlsl_iomap = true;
    option.invert_y = true;
    if (desc.type == ShaderType::kLibrary)
        option.use_dxc = true;
    option.resource_set_binding = static_cast<uint32_t>(desc.type);
    m_blob = SpirvCompile(desc, option);
}

std::vector<VertexInputDesc> SpirvShader::GetInputLayout() const
{
    spirv_cross::CompilerHLSL compiler(m_blob);
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    std::vector<VertexInputDesc> input_layout_desc;
    for (auto& resource : resources.stage_inputs)
    {
        decltype(auto) layout = input_layout_desc.emplace_back();
        layout.slot = compiler.get_decoration(resource.id, spv::DecorationLocation);
        layout.semantic_name = compiler.get_decoration_string(resource.id, spv::DecorationHlslSemanticGOOGLE);
        decltype(auto) type = compiler.get_type(resource.base_type_id);

        if (type.basetype == spirv_cross::SPIRType::Float)
        {
            if (type.vecsize == 1)
                layout.format = gli::format::FORMAT_R32_SFLOAT_PACK32;
            else if (type.vecsize == 2)
                layout.format = gli::format::FORMAT_RG32_SFLOAT_PACK32;
            else if (type.vecsize == 3)
                layout.format = gli::format::FORMAT_RGB32_SFLOAT_PACK32;
            else if (type.vecsize == 4)
                layout.format = gli::format::FORMAT_RGBA32_SFLOAT_PACK32;
        }
        else if (type.basetype == spirv_cross::SPIRType::UInt)
        {
            if (type.vecsize == 1)
                layout.format = gli::format::FORMAT_R32_UINT_PACK32;
            else if (type.vecsize == 2)
                layout.format = gli::format::FORMAT_RG32_UINT_PACK32;
            else if (type.vecsize == 3)
                layout.format = gli::format::FORMAT_RGB32_UINT_PACK32;
            else if (type.vecsize == 4)
                layout.format = gli::format::FORMAT_RGBA32_UINT_PACK32;
        }
        else if (type.basetype == spirv_cross::SPIRType::Int)
        {
            if (type.vecsize == 1)
                layout.format = gli::format::FORMAT_R32_SINT_PACK32;
            else if (type.vecsize == 2)
                layout.format = gli::format::FORMAT_RG32_SINT_PACK32;
            else if (type.vecsize == 3)
                layout.format = gli::format::FORMAT_RGB32_SINT_PACK32;
            else if (type.vecsize == 4)
                layout.format = gli::format::FORMAT_RGBA32_SINT_PACK32;
        }
    }
    return input_layout_desc;
}

ResourceBindingDesc SpirvShader::GetResourceBindingDesc(const std::string& name) const
{
    return {};
}

ShaderType SpirvShader::GetType() const
{
    return m_type;
}

const std::vector<uint32_t>& SpirvShader::GetBlob() const
{
    return m_blob;
}
