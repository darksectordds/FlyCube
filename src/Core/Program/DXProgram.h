#pragma once
#include "Program/Program.h"
#include <Shader/Shader.h>
#include <set>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
using namespace Microsoft::WRL;

class DXDevice;
class DXShader;
class DXGPUDescriptorPoolRange;

struct BindingLayout
{
    D3D12_ROOT_PARAMETER_TYPE type;
    uint32_t root_param_index;
    union
    {
        struct
        {
            size_t root_param_heap_offset;
            size_t heap_offset;
        } table;

        struct
        {
            size_t root_param_num;
        } view;
    };
};

class DXProgram : public Program
{
public:
    DXProgram(DXDevice& device, const std::vector<std::shared_ptr<Shader>>& shaders);
    std::shared_ptr<BindingSet> CreateBindingSet(const std::vector<BindingDesc>& bindings) override;

    const std::vector<std::shared_ptr<DXShader>>& GetShaders() const;
    const ComPtr<ID3D12RootSignature>& GetRootSignature() const;

private:
    size_t HeapSizeByType(D3D12_DESCRIPTOR_HEAP_TYPE type);

    DXDevice& m_device;
    std::vector<std::shared_ptr<DXShader>> m_shaders;

    uint32_t m_num_cbv = 0;
    uint32_t m_num_srv = 0;
    uint32_t m_num_uav = 0;
    uint32_t m_num_rtv = 0;
    uint32_t m_num_sampler = 0;
    ComPtr<ID3D12RootSignature> m_root_signature;

    std::map<BindingDesc, size_t> m_bindings_id;
    std::vector<BindingDesc> m_bindings;

    std::map<std::tuple<ShaderType, D3D12_DESCRIPTOR_RANGE_TYPE>, BindingLayout> m_binding_layout;
    std::map<std::set<size_t>, DXGPUDescriptorPoolRange> m_heap_cache;
    std::map<BindKey, size_t> m_bind_to_slot;
    bool m_is_compute = false;
    using BindingsByHeap = std::map<D3D12_DESCRIPTOR_HEAP_TYPE, std::set<size_t>>;
    std::map<BindingsByHeap, std::shared_ptr<BindingSet>> m_binding_set_cache;
};