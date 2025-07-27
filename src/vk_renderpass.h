#pragma once
#include <vk_types.h>
#include <vector>
#include <memory>
#include <functional>

class VulkanEngine;

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void init(VulkanEngine *engine) = 0;

    virtual void cleanup() = 0;

    virtual void execute(VkCommandBuffer cmd) = 0;

    virtual const char *getName() const = 0;
};

class RenderPassManager
{
public:
    void init(VulkanEngine *engine);

    void cleanup();

    void addPass(std::unique_ptr<IRenderPass> pass);

    void executeAll(VkCommandBuffer cmd);

    template<typename T>
    T *getPass()
    {
        for (auto &pass: _passes)
        {
            if (T *typedPass = dynamic_cast<T *>(pass.get()))
            {
                return typedPass;
            }
        }
        return nullptr;
    }

private:
    VulkanEngine *_engine = nullptr;
    std::vector<std::unique_ptr<IRenderPass> > _passes;
};
