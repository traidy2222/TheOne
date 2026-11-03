#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <content.hpp>
#include <unordered_map>
#include <functional>

#include <theone/render/renderer.hpp>
using Renderer = one::Renderer;
