#pragma once

#include <cstdint>
#include <tuple>

enum class SamplerFilter
{
    kAnisotropic,
    kMinMagMipLinear,
    kComparisonMinMagMipLinear,
};

enum class SamplerTextureAddressMode
{
    kWrap,
    kClamp
};

enum class SamplerComparisonFunc
{
    kNever,
    kAlways,
    kLess
};

struct SamplerDesc
{
    SamplerFilter filter;
    SamplerTextureAddressMode mode;
    SamplerComparisonFunc func;
};

enum class ResourceType
{
    kSrv,
    kUav,
    kCbv,
    kSampler,
    kRtv,
    kDsv
};

struct ViewId
{
    size_t value;
    bool operator< (const ViewId& oth) const
    {
        return value < oth.value;
    }
};

enum BindFlag
{
    kRtv = 1 << 1,
    kDsv = 1 << 2,
    kSrv = 1 << 3,
    kUav = 1 << 4,
    kCbv = 1 << 5,
    kIbv = 1 << 6,
    kVbv = 1 << 7,
    kSampler = 1 << 8,
};

enum class FillMode
{
    kWireframe,
    kSolid
};

enum class CullMode
{
    kNone,
    kFront,
    kBack,
};

struct RasterizerDesc
{
    FillMode fill_mode;
    CullMode cull_mode;
    int32_t DepthBias = 0;
};


enum class Blend
{
    kZero,
    kSrcAlpha,
    kInvSrcAlpha,
};

enum class BlendOp
{
    kAdd,
};

struct BlendDesc
{
    bool blend_enable = false;
    Blend blend_src;
    Blend blend_dest;
    BlendOp blend_op;
    Blend blend_src_alpha;
    Blend blend_dest_apha;
    BlendOp blend_op_alpha;
};


enum class DepthComparison
{
    kLess,
    kLessEqual
};

struct DepthStencilDesc
{
    bool depth_enable;
    DepthComparison func = DepthComparison::kLess;
};

enum class ShaderType
{
    kVertex,
    kPixel,
    kCompute,
    kGeometry
};

using BindKey = std::tuple<size_t /*program_id*/, ShaderType /*shader_type*/, ResourceType /*res_type*/, uint32_t /*slot*/, ViewId /*view_id*/>;
