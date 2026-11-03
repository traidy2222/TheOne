#include "render/spatial_regions.hpp"
#include "render/vulkan/indexed_binding.hpp"
#include "render/vulkan/presentation.hpp"
#include "render/vulkan/asset_owner_state.hpp"
#include <theone/assets/model.hpp>
#include <iostream>
#include <stdexcept>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<one::Bounds> bounds(320);
        for (size_t i = 0; i < bounds.size(); ++i) {
            glm::vec3 center(float(i % 20) - 10, float(i / 20) - 8, .5f);
            bounds[i].include(center - glm::vec3(.1f));
            bounds[i].include(center + glm::vec3(.1f));
        }
        bounds[5] = {}; // Unknown bounds must never disappear in a rejected parent.
        one::render::SpatialRegions regions;
        auto compare = [&] {
            regions.update(bounds);
            one::Frustum frustum(glm::mat4(1));
            std::vector<bool> seen(bounds.size());
            size_t tests = 0, rejected = 0;
            regions.query(bounds, frustum, [&](uint32_t i) { seen[i] = true; }, tests, rejected);
            for (size_t i = 0; i < bounds.size(); ++i)
                require(seen[i] == frustum.intersects(bounds[i], glm::mat4(1)),
                        "Region culling differs from brute force");
            require(tests && rejected, "Region fixture did not exercise rejection");
        };
        compare();
        compare();
        require(regions.refittedNodes() == 0, "Static hierarchy refitted");
        bounds[0].minimum.x -= .05f;
        compare();
        require(regions.refittedNodes() > 0 && regions.refittedNodes() < 10,
                "Single edit refitted whole tree");
        std::reverse(bounds.begin(), bounds.end());
        compare();
        bounds[0] = {{-.1f, -.1f, .4f}, {.1f, .1f, .6f}};
        compare();
        bounds.resize(161);
        compare();
        bounds.assign(320, {{-.1f, -.1f, .4f}, {.1f, .1f, .6f}});
        regions.update(bounds);
        size_t containedTests = 0, containedRejects = 0, containedVisible = 0;
        regions.query(
            bounds, one::Frustum(glm::mat4(1)), [&](uint32_t) { ++containedVisible; }, containedTests,
            containedRejects);
        require(containedVisible == bounds.size() && containedTests == 1,
                "Contained region did not use bulk acceptance");
        bounds.clear();
        regions.update(bounds);
        const std::array<VkPresentModeKHR, 2> modes{VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
        require(one::vulkan::choosePresentMode(modes, true) == VK_PRESENT_MODE_FIFO_KHR,
                "FIFO default changed");
        require(one::vulkan::choosePresentMode(modes, false) == VK_PRESENT_MODE_MAILBOX_KHR,
                "Uncapped fallback wrong");
        const std::array<VkPresentModeKHR, 1> fifo{VK_PRESENT_MODE_FIFO_KHR};
        require(one::vulkan::choosePresentMode(fifo, false) == VK_PRESENT_MODE_FIFO_KHR,
                "Unsupported mode selected");
        for (uint64_t vertex : {0ull, 4ull, 108ull, 688ull, 123456ull, uint64_t(INT32_MAX) * 104 + 104}) {
            auto binding = one::vulkan::indexedBinding(vertex, 904, 104);
            require(binding.vertex + uint64_t(binding.baseVertex) * 104 == vertex &&
                        binding.index + uint64_t(binding.firstIndex) * 4 == 904,
                    "Rebased indexed draw address changed");
        }
        one::vulkan::AssetOwners owners;
        auto a = std::make_shared<one::Model>(), b = std::make_shared<one::Model>();
        std::vector<one::Draw> scene(200), dynamic(1);
        for (auto& d : scene)
            d.owner = a;
        dynamic[0].owner = a;
        owners.update(scene, dynamic);
        require(owners.unique.size() == 1, "Owners not deduplicated");
        dynamic[0].owner = b;
        owners.update(scene, dynamic);
        require(owners.unique.size() == 2, "Same-sized owner replacement missed");
        for (auto& d : scene)
            d.owner = b;
        owners.update(scene, dynamic);
        require(owners.unique.size() == 1 && owners.unique[0] == b, "Old owner retained");
        owners.update({}, {});
        require(owners.unique.empty(), "Empty scene retained owners");
        std::cout << "PASS spatial refit/parity, unknown bounds, binding offsets, presentation fallback, "
                     "owner changes\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
