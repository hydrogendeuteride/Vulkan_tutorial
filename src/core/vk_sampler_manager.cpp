#include "vk_sampler_manager.h"
#include "vk_device.h"

void SamplerManager::init(DeviceManager *deviceManager)
{
    _deviceManager = deviceManager;

    VkSamplerCreateInfo sampl{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(_deviceManager->device(), &sampl, nullptr, &_defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_deviceManager->device(), &sampl, nullptr, &_defaultSamplerLinear);
}

void SamplerManager::cleanup()
{
    if (!_deviceManager) return;

    if (_defaultSamplerNearest)
    {
        vkDestroySampler(_deviceManager->device(), _defaultSamplerNearest, nullptr);
        _defaultSamplerNearest = VK_NULL_HANDLE;
    }
    if (_defaultSamplerLinear)
    {
        vkDestroySampler(_deviceManager->device(), _defaultSamplerLinear, nullptr);
        _defaultSamplerLinear = VK_NULL_HANDLE;
    }
}

