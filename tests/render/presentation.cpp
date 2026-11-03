#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
#include <iostream>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<char> capture() {
    std::ifstream file("capture.ppm", std::ios::binary);
    require(bool(file), "Missing capture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("Focused FIFO presentation", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        require(window, SDL_GetError());
        struct Window {
            SDL_Window* value;
            ~Window() { SDL_DestroyWindow(value); }
        } ownedWindow{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplSDL3_InitForVulkan(window);
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        one::Renderer renderer;
        renderer.initialize(window, true);
        one::Camera camera;
        uint64_t previous = 0;
        auto render = [&](bool readback = false) {
            SDL_PumpEvents();
            require(renderer.waitForFrame(), "Input boundary unavailable");
            require(renderer.waitForFrame(), "Repeated input boundary changed readiness");
            require(renderer.beginFrame(), "No presentation frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::GetBackgroundDrawList()->AddRectFilled({100, 100}, {600, 440}, IM_COL32(255, 64, 32, 255));
            ImGui::Render();
            require(renderer.render({}, {}, camera, 0, readback, false), "Presentation failed");
            const auto& stats = renderer.frameStats();
            require(stats.frame > previous, "Frame serial did not advance");
            previous = stats.frame;
            require(!stats.presentationPacing || renderer.vsyncEnabled(), "Pacing applied to uncapped mode");
            require(renderer.assetMemoryStats().pooledStagingBytes <= 4 * 1024 * 1024,
                    "Staging budget changed");
        };
        auto settle = [&] {
            for (int i = 0; i < 8; ++i)
                render();
            render(true);
            const auto& timing = renderer.frameStats();
            std::cout << "SWAPCHAIN generation=" << timing.swapchainGeneration
                      << " rebuild=" << timing.swapchainRebuildMs << " idle=" << timing.swapchainIdleMs
                      << " targets=" << timing.swapchainTargetsMs << '\n';
        };
        settle();
        auto reference = capture();
        require(renderer.vsyncEnabled(), "Expected FIFO");
        auto generation = renderer.frameStats().swapchainGeneration;
        renderer.resize();
        settle();
        require(renderer.frameStats().swapchainGeneration == generation, "Same-size event rebuilt swapchain");
        require(reference == capture(), "Same-size resize image mismatch");
        renderer.setFramesInFlight(1);
        settle();
        require(reference == capture(), "One-slot image mismatch");
        renderer.setFramesInFlight(2);
        renderer.setVsync(false);
        settle();
        require(reference == capture(), "Uncapped image mismatch");
        renderer.setVsync(true);
        settle();
        require(reference == capture(), "FIFO recreation image mismatch");
        require(SDL_SetWindowSize(window, 800, 600), SDL_GetError());
        renderer.resize();
        settle();
        require(SDL_SetWindowSize(window, 640, 480), SDL_GetError());
        renderer.resize();
        settle();
        require(reference == capture(), "Resize recovery image mismatch");
        generation = renderer.frameStats().swapchainGeneration;
        require(SDL_MinimizeWindow(window), SDL_GetError());
        SDL_SyncWindow(window);
        SDL_PumpEvents();
        require(!renderer.waitForFrame(), "Minimized window entered presentation wait");
        require(SDL_RestoreWindow(window), SDL_GetError());
        SDL_SyncWindow(window);
        renderer.resize();
        settle();
        require(renderer.frameStats().swapchainGeneration == generation, "Same-size restore rebuilt swapchain");
        require(reference == capture(), "Restore image mismatch");
        renderer.clearAssets();
        settle();
        require(reference == capture(), "Asset reset image mismatch");
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : reference)
            hash = (hash ^ byte) * 1099511628211ull;
        std::cout << "presentation_checksum=" << hash << '\n';
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation failed");
        std::cout << "PASS FIFO/uncapped, one/two slots, resize, reset, image parity and shutdown\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
