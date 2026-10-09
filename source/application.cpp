#include "application.hpp"

#include <cmath>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace application {

    namespace {

        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        VkPipeline graphics_pipeline = VK_NULL_HANDLE;

        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation vertex_buffer_allocation = VK_NULL_HANDLE;

        VkBuffer index_buffer = VK_NULL_HANDLE;
        VmaAllocation index_buffer_allocation = VK_NULL_HANDLE;
        uint32_t index_count = 0;

        bool is_perspective = true;
        float fov = 45.0f;
        float ortho_size = 3.0f;

        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);

        bool anim_playing = true;
        float anim_speed = 1.0f;
        float anim_radius = 2.0f;
        float anim_height_amp = 0.5f;
        float anim_time = 0.0f;

        glm::vec4 object_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        std::vector<char> readFile(const std::string& filename) {
            std::ifstream file(filename, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << filename << std::endl;
                return {};
            }
            size_t fileSize = (size_t)file.tellg();
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            file.close();
            return buffer;
        }

        VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
            if (code.empty()) {
                return VK_NULL_HANDLE;
            }
            VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }

        void generateCylinder(float radius, float height, int sectors,
            std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
            vertices.clear();
            indices.clear();

            float halfH = height * 0.5f;
            const float PI = 3.14159265358979323846f;

            // Верхний центр (0)
            glm::vec3 topCenterPos(0.0f, halfH, 0.0f);
            glm::vec3 topCenterColor = glm::clamp(topCenterPos + glm::vec3(0.5f), 0.1f, 1.0f);
            vertices.push_back({ topCenterPos, topCenterColor });

            // Нижний центр (1)
            glm::vec3 botCenterPos(0.0f, -halfH, 0.0f);
            glm::vec3 botCenterColor = glm::clamp(botCenterPos + glm::vec3(0.5f), 0.1f, 1.0f);
            vertices.push_back({ botCenterPos, botCenterColor });

            // Кольца
            for (int i = 0; i < sectors; ++i) {
                float angle = 2.0f * PI * float(i) / float(sectors);
                float x = radius * std::cos(angle);
                float z = radius * std::sin(angle);

                glm::vec3 pTop(x, halfH, z);
                glm::vec3 cTop((x / radius + 1.0f) * 0.5f, 0.8f, (z / radius + 1.0f) * 0.5f);
                vertices.push_back({ pTop, cTop });

                glm::vec3 pBot(x, -halfH, z);
                glm::vec3 cBot((x / radius + 1.0f) * 0.5f, 0.2f, (z / radius + 1.0f) * 0.5f);
                vertices.push_back({ pBot, cBot });
            }

            uint32_t topCenterIdx = 0;
            uint32_t botCenterIdx = 1;

            for (int i = 0; i < sectors; ++i) {
                int next = (i + 1) % sectors;

                uint32_t topCurr = 2 + i * 2;
                uint32_t botCurr = 3 + i * 2;
                uint32_t topNext = 2 + next * 2;
                uint32_t botNext = 3 + next * 2;

                // Верхнее основание
                indices.push_back(topCenterIdx);
                indices.push_back(topNext);
                indices.push_back(topCurr);

                // Нижнее основание
                indices.push_back(botCenterIdx);
                indices.push_back(botCurr);
                indices.push_back(botNext);

                // Бок
                indices.push_back(topCurr);
                indices.push_back(topNext);
                indices.push_back(botCurr);

                indices.push_back(botCurr);
                indices.push_back(topNext);
                indices.push_back(botNext);
            }
        }

    } // namespace

    bool initialize() {
        auto& ctx = graphics::internal::context;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        generateCylinder(0.8f, 1.8f, 50, vertices, indices);
        index_count = static_cast<uint32_t>(indices.size());

        // Vertex buffer
        VkBufferCreateInfo vbInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        vbInfo.size = sizeof(Vertex) * vertices.size();
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo vbAllocInfo{};
        vbAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(ctx.allocator, &vbInfo, &vbAllocInfo,
            &vertex_buffer, &vertex_buffer_allocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        void* data = nullptr;
        vmaMapMemory(ctx.allocator, vertex_buffer_allocation, &data);
        std::memcpy(data, vertices.data(), vbInfo.size);
        vmaUnmapMemory(ctx.allocator, vertex_buffer_allocation);

        // Index buffer
        VkBufferCreateInfo ibInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ibInfo.size = sizeof(uint32_t) * indices.size();
        ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        if (vmaCreateBuffer(ctx.allocator, &ibInfo, &vbAllocInfo,
            &index_buffer, &index_buffer_allocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        vmaMapMemory(ctx.allocator, index_buffer_allocation, &data);
        std::memcpy(data, indices.data(), ibInfo.size);
        vmaUnmapMemory(ctx.allocator, index_buffer_allocation);

        // Push constants
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &pipeline_layout) != VK_SUCCESS) {
            return false;
        }

        // Shaders
        auto vertCode = readFile("shaders/shader.vert.spv");
        auto fragCode = readFile("shaders/shader.frag.spv");

        VkShaderModule vertModule = createShaderModule(ctx.device, vertCode);
        VkShaderModule fragModule = createShaderModule(ctx.device, fragCode);

        if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
            std::cerr << "Failed to load shaders\n";
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

        VkVertexInputBindingDescription bindingDesc{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrDesc[2] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}
        };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = 2;
        vertexInputInfo.pVertexAttributeDescriptions = attrDesc;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE; 
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipeline_layout;
        pipelineInfo.renderPass = ctx.render_pass;

        if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphics_pipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create graphics pipeline\n";
            return false;
        }

        vkDestroyShaderModule(ctx.device, fragModule, nullptr);
        vkDestroyShaderModule(ctx.device, vertModule, nullptr);

        return true;
    }

    void shutdown() {
        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);

        if (graphics_pipeline) vkDestroyPipeline(ctx.device, graphics_pipeline, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);

        if (vertex_buffer) vmaDestroyBuffer(ctx.allocator, vertex_buffer, vertex_buffer_allocation);
        if (index_buffer) vmaDestroyBuffer(ctx.allocator, index_buffer, index_buffer_allocation);
    }

    void update(double time) {
        static double last_time = time;
        float dt = static_cast<float>(time - last_time);
        last_time = time;

        if (anim_playing) {
            anim_time += dt * anim_speed;
        }

        ImGui::Begin("Lab 1 - Variant 8 (Cylinder)");

        if (ImGui::CollapsingHeader("1. Camera & Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
            int persp = is_perspective ? 1 : 0;
            if (ImGui::RadioButton("Perspective", persp == 1)) is_perspective = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Orthographic", persp == 0)) is_perspective = false;

            if (is_perspective) {
                ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f);
            }
            else {
                ImGui::SliderFloat("Ortho Size", &ortho_size, 0.5f, 10.0f);
            }
        }

        if (ImGui::CollapsingHeader("2. Manual Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", glm::value_ptr(position), 0.05f);
            ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 1.0f);
            ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.05f, 0.1f, 10.0f);
        }

        if (ImGui::CollapsingHeader("3. Animation (Complex Trajectory)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button(anim_playing ? "Pause" : "Play")) {
                anim_playing = !anim_playing;
            }
            ImGui::SliderFloat("Speed", &anim_speed, 0.0f, 5.0f);
            ImGui::SliderFloat("Orbit Radius", &anim_radius, 0.0f, 5.0f);
            ImGui::SliderFloat("Vertical Amplitude", &anim_height_amp, 0.0f, 2.0f);
        }

        if (ImGui::CollapsingHeader("4 & 5. Object Color", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit4("Color (multiplier)", glm::value_ptr(object_color));
        }

        ImGui::End();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;

        // 1. Сбрасываем и начинаем запись командного буфера
        vkResetCommandBuffer(fd.command_buffer, 0);

        VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(fd.command_buffer, &beginInfo);

        // 2. Render pass
        VkRenderPassBeginInfo rpInfo{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpInfo.renderPass = ctx.render_pass;
        rpInfo.framebuffer = fd.framebuffer;
        rpInfo.renderArea.extent = ctx.swapchain_extent;

        VkClearValue clearValues[2];
        clearValues[0].color = { {0.1f, 0.1f, 0.15f, 1.0f} };
        clearValues[1].depthStencil = { 1.0f, 0 };
        rpInfo.clearValueCount = 2;
        rpInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(fd.command_buffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        // 3. Рисуем цилиндр
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        VkViewport viewport{ 0.0f, 0.0f,
                            (float)ctx.swapchain_extent.width,
                            (float)ctx.swapchain_extent.height,
                            0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, ctx.swapchain_extent };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

        float aspect = (float)ctx.swapchain_extent.width / (float)ctx.swapchain_extent.height;
        glm::mat4 proj;

        if (is_perspective) {
            proj = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
        }
        else {
            proj = glm::ortho(-ortho_size * aspect, ortho_size * aspect,
                -ortho_size, ortho_size, -100.0f, 100.0f);
        }
        proj[1][1] *= -1.0f;

        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 6.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 animOffset(
            anim_radius * std::cos(anim_time),
            anim_height_amp * std::sin(anim_time * 2.0f),
            anim_radius * std::sin(anim_time)
        );

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position + animOffset);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.y + anim_time * 45.0f), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);

        PushConstants constants;
        constants.mvp = proj * view * model;
        constants.baseColor = object_color;

        vkCmdPushConstants(fd.command_buffer, pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(PushConstants), &constants);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);
        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

        // 4. Закрываем render pass И командный буфер
        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application