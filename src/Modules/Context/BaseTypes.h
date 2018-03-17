#pragma once

#include <cstdint>

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
    INT DepthBias = 0;
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
    bool blend_enable;
    Blend blend_src;
    Blend blend_dest;
    BlendOp blend_op;
    Blend blend_src_alpha;
    Blend blend_dest_apha;
    BlendOp blend_op_alpha;
};

struct DepthStencilDesc
{
    bool depth_enable;
};
