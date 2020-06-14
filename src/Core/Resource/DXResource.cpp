#include "Resource/DXResource.h"
#include <Device/DXDevice.h>
#include <Utilities/FileUtility.h>
#include <d3dx12.h>

DXResource::DXResource(DXDevice& device)
    : m_device(device)
{
}

uint64_t DXResource::GetWidth() const
{
    return desc.Width;
}

uint32_t DXResource::GetHeight() const
{
    return desc.Height;
}

uint16_t DXResource::GetLayerCount() const
{
    return desc.DepthOrArraySize;
}

uint16_t DXResource::GetLevelCount() const
{
    return desc.MipLevels;
}

uint32_t DXResource::GetSampleCount() const
{
    return desc.SampleDesc.Count;
}

uint64_t DXResource::GetAccelerationStructureHandle() const
{
    return resource->GetGPUVirtualAddress();
}

void DXResource::SetName(const std::string& name)
{
    resource->SetName(utf8_to_wstring(name).c_str());
}

uint8_t* DXResource::Map()
{
    CD3DX12_RANGE range(0, 0);
    uint8_t* dst_data = nullptr;
    ASSERT_SUCCEEDED(resource->Map(0, &range, reinterpret_cast<void**>(&dst_data)));
    return dst_data;
}

void DXResource::Unmap()
{
    CD3DX12_RANGE range(0, 0);
    resource->Unmap(0, &range);
}
