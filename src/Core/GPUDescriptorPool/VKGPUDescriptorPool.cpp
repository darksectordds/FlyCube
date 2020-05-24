#include "GPUDescriptorPool/VKGPUDescriptorPool.h"
#include <Device/VKDevice.h>

VKGPUDescriptorPool::VKGPUDescriptorPool(VKDevice& device)
    : m_device(device)
{
}

void VKGPUDescriptorPool::ResizeHeap(const std::map<vk::DescriptorType, size_t>& count)
{
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (auto & x : count)
    {
        pool_sizes.emplace_back();
        vk::DescriptorPoolSize& pool_size = pool_sizes.back();
        pool_size.type = x.first;
        pool_size.descriptorCount = x.second;
    }

    // TODO: fix me
    if (count.empty())
    {
        pool_sizes.emplace_back();
        vk::DescriptorPoolSize& pool_size = pool_sizes.back();
        pool_size.type = vk::DescriptorType::eSampler;
        pool_size.descriptorCount = 1;
    }

    vk::DescriptorPoolCreateInfo poolInfo = {};
    poolInfo.poolSizeCount = pool_sizes.size();
    poolInfo.pPoolSizes = pool_sizes.data();
    poolInfo.maxSets = 1;

    // TODO
    if (m_descriptorPool)
        m_descriptorPool.release();

    m_descriptorPool = m_device.GetDevice().createDescriptorPoolUnique(poolInfo);
}

vk::UniqueDescriptorSet VKGPUDescriptorPool::AllocateDescriptorSet(const vk::DescriptorSetLayout& set_layout, const std::map<vk::DescriptorType, size_t>& count)
{
    ResizeHeap(count);

    vk::DescriptorSetAllocateInfo allocInfo = {};
    allocInfo.descriptorPool = m_descriptorPool.get();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &set_layout;

    auto descriptor_sets = m_device.GetDevice().allocateDescriptorSetsUnique(allocInfo);
    return std::move(descriptor_sets.front());
}