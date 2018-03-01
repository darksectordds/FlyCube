#pragma once

#include "Context/Context.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <DXGI1_4.h>
#include <wrl.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <d3d12.h>
#include <d3dx12.h>
#include "Context/DescriptorPool.h"

using namespace Microsoft::WRL;

// {975B960E-21B5-4FD4-8301-53914554BDCA}
static const GUID buffer_stride =
{ 0x975b960e, 0x21b5, 0x4fd4,{ 0x83, 0x1, 0x53, 0x91, 0x45, 0x54, 0xbd, 0xca } };

// {D3B925BB-7647-4A75-8705-45F3ED5A71CB}
static const GUID buffer_bind_flag =
{ 0xd3b925bb, 0x7647, 0x4a75,{ 0x87, 0x5, 0x45, 0xf3, 0xed, 0x5a, 0x71, 0xcb } };

// {7FCACA10-DC33-417D-BEC0-AE4A0B37A76A}
static const GUID resources_state =
{ 0x7fcaca10, 0xdc33, 0x417d,{ 0xbe, 0xc0, 0xae, 0x4a, 0xb, 0x37, 0xa7, 0x6a } };


class DX12ProgramApi;

class DX12Context : public Context
{
public:
    DX12Context(GLFWwindow* window, int width, int height);
    virtual ComPtr<IUnknown> GetBackBuffer() override;
    virtual void Present() override;
    virtual void DrawIndexed(UINT IndexCount) override;

    virtual void Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
    virtual void SetViewport(int width, int height) override;
    virtual void OMSetRenderTargets(std::vector<ComPtr<IUnknown>> rtv, ComPtr<IUnknown> dsv) override;
    virtual void ClearRenderTarget(ComPtr<IUnknown> rtv, const FLOAT ColorRGBA[4]) override;
    virtual void ClearDepthStencil(ComPtr<IUnknown> dsv, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;
    virtual ComPtr<IUnknown> CreateTexture(uint32_t bind_flag, DXGI_FORMAT format, uint32_t msaa_count, int width, int height, int depth, int mip_levels) override;
    virtual ComPtr<IUnknown> CreateShadowRSState() override;
    virtual void RSSetState(ComPtr<IUnknown> state) override;
    virtual std::unique_ptr<ProgramApi> CreateProgram() override;
    virtual ComPtr<IUnknown> CreateBuffer(uint32_t bind_flag, UINT buffer_size, size_t stride, const std::string& name) override;

    virtual void IASetIndexBuffer(ComPtr<IUnknown> res, UINT SizeInBytes, DXGI_FORMAT Format) override;
    virtual void IASetVertexBuffer(UINT slot, ComPtr<IUnknown> res, UINT SizeInBytes, UINT Stride) override;

    virtual void UpdateSubresource(ComPtr<IUnknown> ires, UINT DstSubresource, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;


    virtual void BeginEvent(LPCWSTR Name) override;
    virtual void EndEvent() override;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12Fence> m_fence;
    uint32_t m_fenceValue = 0;
    uint32_t frame_index = 0;
    HANDLE m_fenceEvent;

    void SetState(ComPtr<ID3D12Resource> res, D3D12_RESOURCE_STATES state)
    {
        UINT size = sizeof(state);
        ASSERT_SUCCEEDED(res->SetPrivateData(resources_state, size, &state));
    }

    D3D12_RESOURCE_STATES GetState(ComPtr<ID3D12Resource> res)
    {
        D3D12_RESOURCE_STATES current_state = {};
        UINT size = sizeof(current_state);
        ASSERT_SUCCEEDED(res->GetPrivateData(resources_state, &size, &current_state));
        return current_state;
    }

    void ResourceBarrier(ComPtr<ID3D12Resource> res, D3D12_RESOURCE_STATES state)
    {
        D3D12_RESOURCE_STATES current_state = GetState(res);
        if (current_state != state)
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(res.Get(), current_state, state));
        SetState(res, state);
    }

    static const int frameBufferCount = 2; // number of buffers we want, 2 for double buffering, 3 for tripple buffering
    ComPtr<ID3D12GraphicsCommandList> commandList; // a command list we can record commands into, then execute them to render the frame
    ComPtr<ID3D12CommandAllocator> commandAllocator; // we want enough a
    DX12ProgramApi* current_program = nullptr;

    std::unique_ptr<DescriptorPool> descriptor_pool;
private:
    void WaitForPreviousFrame();
    virtual void ResizeBackBuffer(int width, int height) override;
    ComPtr<IDXGISwapChain3> swap_chain;
};
