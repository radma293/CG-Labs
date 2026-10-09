#pragma once

#include "graphics_internal.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace application {

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
    };

    struct PushConstants {
        glm::mat4 mvp;
        glm::vec4 baseColor;
    };

    bool initialize();
    void shutdown();

    void update(double time);
    void render(const graphics::internal::FrameData& fd);

} // namespace application