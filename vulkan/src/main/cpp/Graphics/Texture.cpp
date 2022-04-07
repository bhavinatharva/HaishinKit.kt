#include "Kernel.h"
#include "Texture.h"
#include "Util.h"
#include "ImageStorage.h"
#include "ColorSpace.h"
#include <sys/socket.h>
#include <unistd.h>
#include <glm/ext/matrix_transform.hpp>

using namespace Graphics;

Texture::Texture(vk::Extent2D extent, int32_t format) : colorSpace(new ColorSpace()) {
    colorSpace->format = format;
    colorSpace->extent = extent;
}

Texture::~Texture() = default;

PushConstants Texture::GetPushConstants(Kernel &kernel) const {
    auto degrees = 0.f;

    switch (imageOrientation) {
        case UP:
            degrees = 0.f;
            break;
        case DOWN:
            degrees = 180.f;
            break;
        case LEFT:
            degrees = 90.f;
            break;
        case RIGHT:
            degrees = 270.f;
            break;
        case UP_MIRRORED:
            degrees = 0.f;
            break;
        case DOWN_MIRRORED:
            degrees = 180.0;
            break;
        case LEFT_MIRRORED:
            degrees = 90.f;
            break;
        case RIGHT_MIRRORED:
            degrees = 180.f;
            break;
    }

    switch (kernel.GetSurfaceRotation()) {
        case ROTATION_0:
            degrees += 0.f;
            break;
        case ROTATION_90:
            degrees += 90.f;
            break;
        case ROTATION_180:
            degrees += 180.f;
            break;
        case ROTATION_270:
            degrees += 270.f;
            break;
    }

    if (((int) degrees % 180) == 0 &&
        (imageOrientation == LEFT || imageOrientation == LEFT_MIRRORED)) {
        degrees += 180.f;
    }

    return {
            .mvp = glm::scale(glm::mat4(1.f), glm::vec3(1.f, 1.f, 1.f)),
            .preRotate = glm::rotate(glm::mat4(1.f), glm::radians(degrees),
                                     glm::vec3(0.f, 0.f, 1.f)),
    };
}

vk::Viewport Texture::GetViewport(Kernel &kernel) const {
    vk::Viewport viewport = vk::Viewport();

    vk::Extent2D surface = kernel.swapChain.size;
    auto newImageExtent = colorSpace->extent;
    if (surface.width < surface.height) {
        if (colorSpace->extent.height < colorSpace->extent.width) {
            newImageExtent = vk::Extent2D(colorSpace->extent.height, colorSpace->extent.width);
        }
    } else {
        if (colorSpace->extent.width < colorSpace->extent.height) {
            newImageExtent = vk::Extent2D(colorSpace->extent.height, colorSpace->extent.width);
        }
    }

    switch (videoGravity) {
        case RESIZE_ASPECT: {
            const float xRatio = (float) surface.width / (float) newImageExtent.width;
            const float yRatio =
                    (float) surface.height / (float) newImageExtent.height;
            if (yRatio < xRatio) {
                viewport
                        .setX(((float) surface.width - (float) newImageExtent.width * yRatio) / 2)
                        .setY(0)
                        .setWidth((float) surface.width * yRatio)
                        .setHeight((float) newImageExtent.height);
            } else {
                viewport
                        .setX(0)
                        .setY(((float) surface.height - (float) newImageExtent.height * xRatio) / 2)
                        .setWidth((float) surface.width)
                        .setHeight((float) newImageExtent.height * xRatio);
            }
            break;
        }
        case RESIZE_ASPECT_FILL: {
            const float iRatio = (float) surface.width / (float) surface.height;
            const float fRatio = (float) newImageExtent.width / (float) newImageExtent.height;
            if (iRatio < fRatio) {
                viewport
                        .setX(((float) surface.width - (float) surface.height * fRatio) / 2)
                        .setY(0)
                        .setWidth((float) surface.height * fRatio)
                        .setHeight((float) surface.height);
            } else {
                viewport
                        .setX(0)
                        .setY(((float) surface.height - (float) surface.width / fRatio) / 2)
                        .setWidth((float) surface.width)
                        .setHeight((float) surface.width / fRatio);
            }
            break;
        }
        case RESIZE: {
            viewport.setWidth((float) surface.width);
            viewport.setHeight((float) surface.height);
            break;
        }
    }
    return viewport;
}

void Texture::SetImageOrientation(ImageOrientation newImageOrientation) {
    imageOrientation = newImageOrientation;
    invalidateLayout = true;
}

void Texture::SetUp(Kernel &kernel, AHardwareBuffer *buffer) {
    if (!sampler) {
        vk::Filter filter = vk::Filter::eLinear;
        switch (resampleFilter) {
            case LINEAR:
                filter = vk::Filter::eLinear;
                break;
            case NEAREST:
                filter = vk::Filter::eNearest;
                break;
            case CUBIC:
                filter = vk::Filter::eCubicIMG;
                break;
        }

        vk::AndroidHardwareBufferFormatPropertiesANDROID format;
        vk::AndroidHardwareBufferPropertiesANDROID properties;
        properties.pNext = &format;
        kernel.device->getAndroidHardwareBufferPropertiesANDROID(buffer, &properties);
        auto externalFormat = vk::ExternalFormatANDROID().setExternalFormat(format.externalFormat);

        this->externalFormat = format.externalFormat;

        conversion = kernel.device->createSamplerYcbcrConversionUnique(
                vk::SamplerYcbcrConversionCreateInfo()
                        .setPNext(&externalFormat)
                        .setFormat(vk::Format::eUndefined)
                        .setYcbcrModel(format.suggestedYcbcrModel)
                        .setYcbcrRange(format.suggestedYcbcrRange)
                        .setComponents(format.samplerYcbcrConversionComponents)
                        .setXChromaOffset(format.suggestedXChromaOffset)
                        .setYChromaOffset(format.suggestedYChromaOffset)
                        .setChromaFilter(vk::Filter::eNearest)
                        .setForceExplicitReconstruction(false));

        auto samplerCreate = vk::SamplerCreateInfo()
                .setMagFilter(filter)
                .setMinFilter(filter)
                .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                .setMipLodBias(0.0f)
                .setMaxAnisotropy(1)
                .setCompareOp(vk::CompareOp::eNever)
                .setMinLod(0.0f)
                .setMaxLod(0.0f)
                .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
                .setUnnormalizedCoordinates(false);

        if (conversion) {
            auto ycbcrConversionInfo = vk::SamplerYcbcrConversionInfo()
                    .setConversion(
                            conversion.get());
            samplerCreate.setPNext(&ycbcrConversionInfo);
        }

        sampler = kernel.device->createSamplerUnique(samplerCreate);

        std::vector<vk::Sampler> samplers(1);
        samplers[0] = sampler.get();

        kernel.pipeline.SetUp(kernel, samplers);
        kernel.commandBuffer.SetUp(kernel);

        storages.resize(kernel.swapChain.GetImagesCount());
        for (auto &storage : storages) {
            storage.extent = colorSpace->extent;
            storage.format = colorSpace->GetFormat();
            storage.SetExternalFormat(this->externalFormat);
            storage.SetUp(kernel, conversion);
        }
    }
}

void Texture::TearDown(Kernel &kernel) {
}

void Texture::UpdateAt(Kernel &kernel, uint32_t currentFrame, AHardwareBuffer *buffer) {
    storages[currentFrame].Update(kernel, buffer);
    kernel.pipeline.UpdateDescriptorSets(kernel, *this);
    currentImage = currentFrame;
}

void Texture::Layout(Kernel &kernel) {
    /*
    if (!invalidateLayout && !kernel.invalidateSurfaceRotation) {
        return;
    }
     */

    const auto colors = {vk::ClearValue().setColor(
            vk::ClearColorValue().setFloat32({0.f, 0.f, 0.f, 1.f}))};

    const std::vector<vk::Rect2D> scissors = {
            vk::Rect2D().setExtent(kernel.swapChain.size).setOffset({0, 0})
    };

    const std::vector<vk::Viewport> viewports = {GetViewport(kernel)};
    const PushConstants pushConstantsBlock = GetPushConstants(kernel);

    for (auto i = 0; i < kernel.commandBuffer.commandBuffers.size(); ++i) {
        auto &commandBuffer = kernel.commandBuffer.commandBuffers[i].get();
        commandBuffer.begin(
                vk::CommandBufferBeginInfo()
                        .setFlags(vk::CommandBufferUsageFlagBits::eRenderPassContinue));

        commandBuffer.setViewport(0, viewports);
        commandBuffer.setScissor(0, scissors);

        commandBuffer.pushConstants(
                kernel.pipeline.pipelineLayout.get(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                sizeof(PushConstants),
                &pushConstantsBlock);

        commandBuffer.beginRenderPass(
                vk::RenderPassBeginInfo()
                        .setRenderPass(kernel.swapChain.renderPass.get())
                        .setFramebuffer(kernel.commandBuffer.framebuffers[i])
                        .setRenderArea(
                                vk::Rect2D().setOffset({0, 0}).setExtent(kernel.swapChain.size))
                        .setClearValues(colors),
                vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   kernel.pipeline.pipeline.get());

        commandBuffer.bindVertexBuffers(0, kernel.commandBuffer.buffers,
                                        kernel.commandBuffer.offsets);

        commandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                kernel.pipeline.pipelineLayout.get(),
                0,
                1,
                &kernel.pipeline.descriptorSets[0].get(),
                0,
                nullptr);

        commandBuffer.draw(4, 1, 0, 0);
        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    invalidateLayout = false;
    kernel.invalidateSurfaceRotation = false;
}

vk::DescriptorImageInfo Texture::GetDescriptorImageInfo() {
    return storages[currentImage].GetDescriptorImageInfo()
            .setSampler(sampler.get());
}
