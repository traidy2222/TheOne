#include "backend.hpp"
#include "volume_state.hpp"
#include <cmath>
#include <cstdlib>
namespace one {
void Renderer::Impl::retireVolumeBuffers() {
    if (!volumes.gpu)
        return;
    auto old = std::move(volumes.gpu);
    volumes.retiredBytes += old->bytes;
    retire([this, old] {
        volumes.retiredBytes -= old->bytes;
        volumes.stats.allocatedBytes =
            (volumes.gpu ? volumes.gpu->bytes : 0) + volumes.hdrBytes + volumes.retiredBytes;
    });
    volumes.stats.allocatedBytes = volumes.hdrBytes + volumes.retiredBytes;
    volumes.initialize = true;
    for (auto& f : frames)
        f.volumeQueryWritten = false;
}
void Renderer::Impl::volumeBarrier() {
    VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    m.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(frame().command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &m, 0, nullptr, 0, nullptr);
}
void Renderer::Impl::readVolumeDiagnostics(uint32_t slot) {
    auto& v = volumes;
    if (!v.gpu)
        return;
    auto& g = *v.gpu;
    if (frames[slot].volumeQueryWritten) {
        const auto* raw = static_cast<const uint32_t*>(g.diagnostics[slot].mapped);
        auto value = [&](int i) {
            float result;
            std::memcpy(&result, raw + i, 4);
            return result;
        };
        v.stats.fuelKg = value(0);
        v.stats.oxygenKg = value(1);
        v.stats.sootKg = value(2);
        v.stats.productsKg = value(3);
        v.stats.nitrogenKg = value(4);
        v.stats.enthalpyJ = value(5);
        v.stats.maximumKelvin = value(8);
        v.stats.maximumSpeed = value(9);
        v.stats.activeCells = raw[10];
        v.stats.maximumDivergenceError = value(11);
        v.stats.velocityClamps = raw[12];
        v.stats.initialDivergenceError = value(13);
        v.stats.gpuFrame = frames[slot].serial;
        v.stats.burnedFuelKg += value(16);
        v.stats.lostHeatJ += value(17);
        v.stats.boundaryMassKg += value(18);
        v.stats.boundaryEnthalpyJ += value(19);
        for (int a = 0; a < 4; ++a)
            v.stats.boundarySpeciesKg[a] += value(20 + a);
        if (frames[slot].queries) {
            uint64_t q[6]{};
            auto result = vkGetQueryPoolResults(deviceState.device, frames[slot].queries, 6, 6, sizeof(q), q,
                                                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS) {
                uint64_t mask = deviceState.timestampBits >= 64
                                    ? UINT64_MAX
                                    : (uint64_t(1) << deviceState.timestampBits) - 1;
                auto ms = [&](int a) {
                    return double((q[a + 1] - q[a]) & mask) * deviceState.timestampPeriod / 1e6;
                };
                v.stats.gpuSimulationMs = ms(0);
                v.stats.gpuLightingMs = ms(2);
                v.stats.gpuRaymarchMs = ms(4);
            } else if (result != VK_NOT_READY)
                check(result, "volume timestamps");
        }
        frames[slot].volumeQueryWritten = false;
        if (std::getenv("THEONE_VOLUME_PROFILE_PHASES") && frames[slot].queries) {
            uint64_t phases[16]{};
            if (vkGetQueryPoolResults(deviceState.device, frames[slot].queries, 12, 16, sizeof(phases),
                                      phases, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                uint64_t mask = deviceState.timestampBits >= 64
                                    ? UINT64_MAX
                                    : (uint64_t(1) << deviceState.timestampBits) - 1;
                std::cout << "VOLUME_PHASE frame=" << frames[slot].serial;
                const char* names[]{"velocity", "transport",  "forces",      "rhs",
                                    "pressure", "projection", "diagnostics", "occupancy"};
                for (int a = 0; a < 8; ++a)
                    std::cout << ' ' << names[a] << '='
                              << double((phases[a * 2 + 1] - phases[a * 2]) & mask) *
                                     deviceState.timestampPeriod / 1e6;
                std::cout << '\n';
            }
        }
    }
}
void Renderer::Impl::prepareVolumes(uint32_t index, const Camera& camera, bool shadows) {
    if (volumes.active) {
        const auto& d = volumes.scene.domain;
        uint64_t count = uint64_t(d.resolution.x) * d.resolution.y * d.resolution.z;
        uint64_t minimumHdr = uint64_t(swapchainState.extent.width) * swapchainState.extent.height * 20 *
                              swapchainState.images.size();
        uint64_t retained = volumes.retiredBytes +
                            (volumes.gpu && volumes.gpu->resolution != d.resolution ? volumes.gpu->bytes : 0);
        if (count * 172 + minimumHdr + retained > d.memoryBudget)
            throw std::runtime_error(
                "Volume fields, HDR targets and in-flight retirement exceed memory budget");
    }
    createHdrTargets();
    createVolumeBuffers();
    auto& v = volumes;
    auto& g = *v.gpu;
    readVolumeDiagnostics(frameIndex);
    vulkan::VolumeParameters p{};
    const auto& d = v.scene.domain;
    const auto& fuel = v.scene.fuel;
    p.inverseVP = glm::inverse(
        camera.projection(float(swapchainState.extent.width) / swapchainState.extent.height) * camera.view());
    p.eye = glm::vec4(camera.position, float(swapchainState.extent.width) / swapchainState.extent.height);
    p.origin = glm::vec4(d.origin, 0);
    p.extent = glm::vec4(d.extent, 0);
    p.grid = glm::uvec4(g.resolution, v.active ? 1 : 0);
    p.gas = {d.ambientKelvin, d.ambientDensity, d.heatCapacity, d.coolingRate};
    p.gravityMax = glm::vec4(d.gravity, d.maximumVelocity);
    p.windClosed = glm::vec4(d.wind, d.closedBoundary ? 1 : 0);
    p.fuel = {fuel.heatRelease, fuel.oxygenRatio, fuel.sootYield, fuel.ignitionKelvin};
    p.optics = {fuel.absorption, fuel.scattering, fuel.phaseG, fuel.reactionRate};
    p.sunDirection = glm::vec4(v.lighting.sunDirection, d.initialTurbulenceRms);
    p.sunRadiance = glm::vec4(v.lighting.sunRadiance, 0);
    p.skyRadiance = glm::vec4(v.lighting.skyRadiance, d.initialTurbulenceScale);
    if (v.stellarActive) {
        const auto& stars = v.stellarScene;
        p.stellarCounts = {float(stars.stars.size()), float(stars.occluders.size()), 1, float(stars.view)};
        p.sunRadiance = glm::vec4(0);
        p.skyRadiance = glm::vec4(0, 0, 0, d.initialTurbulenceScale);
        for (size_t i = 0; i < stars.stars.size(); ++i) {
            const auto& star = stars.stars[i];
            p.starPositionRadius[i] =
                glm::vec4(glm::vec3((star.positionMetres - stars.originMetres) / stars.metresPerUnit),
                          float(star.radiusMetres / stars.metresPerUnit));
            p.starRadiance[i] = glm::vec4(glm::vec3(stellarBandRadiance(star)), float(star.kelvin));
            const auto& a = star.activity;
            if (a.enabled) {
                p.starActivity[i] = {a.enabled ? 1.f : 0.f,
                                     float(star.radiusMetres / a.granuleDiameterMetres), a.granuleContrast,
                                     a.limbDarkening};
                p.starMotion[i] = {float(stars.timeSeconds / a.granuleLifetimeSeconds),
                                   float(stars.timeSeconds * 6.283185307179586 / a.rotationPeriodSeconds),
                                   a.magneticStrength, float(a.seed)};
                p.starAtmosphere[i] = {float(a.atmosphereSteps),
                                       float(a.baseElectronDensityPerM3 * 6.6524587e-29 * star.radiusMetres),
                                       a.atmosphereRadius,
                                       float(stars.timeSeconds * 50000. / star.radiusMetres)};
                for (uint32_t region = 0; region < 4; ++region) {
                    auto r = stellarActivityRegion(star, region);
                    p.starRegionNormal[i * 4 + region] =
                        glm::vec4(glm::vec3(r.normal), float(r.halfSeparation));
                    p.starRegionTangent[i * 4 + region] = glm::vec4(glm::vec3(r.tangent), float(r.height));
                }
            }
        }
        for (size_t i = 0; i < stars.occluders.size(); ++i) {
            const auto& b = stars.occluders[i];
            p.occultingSpheres[i] =
                glm::vec4(glm::vec3((b.positionMetres - stars.originMetres) / stars.metresPerUnit),
                          float(b.radiusMetres / stars.metresPerUnit));
        }
    }
    if (v.stellarActive && v.stellarScene.plasma) {
        const auto& patch = *v.stellarScene.plasma;
        p.plasmaNormal = glm::vec4(patch.normal, float(patch.emitter + 1));
        p.plasmaTangent = glm::vec4(
            patch.tangent, float(patch.lengthMetres / v.stellarScene.stars[patch.emitter].radiusMetres));
        p.plasmaGrid = {float(patch.resolution.x), float(patch.resolution.y), patch.minimum.x,
                        patch.minimum.y};
        p.plasmaExtent = {patch.extent.x, patch.extent.y, float(patch.halfDepthMetres / patch.lengthMetres),
                          0};
        std::memcpy(g.plasma[frameIndex].mapped, patch.cells.data(), patch.cells.size() * sizeof(glm::vec4));
    }
    p.plasmaExtent.w = camera.roll;
    p.ray = {v.scene.rayStepScale, float(v.scene.maximumRaySteps), v.lighting.exposure, shadows ? 1 : 0};
    p.lightVP = sceneState.cascades.matrices;
    p.splits = sceneState.cascades.splits;
    p.forward = glm::vec4(camera.forward(), 1 / std::abs(camera.projection(1)[1][1]));
    p.initial = {v.active ? v.scene.initialSootKgPerM3 : 0, v.active ? v.scene.initialKelvin : 0,
                 d.vorticityConfinement, fuel.flameSpeed};
    p.counts = {v.active ? uint32_t(v.scene.sources.size()) : 0,
                v.active ? uint32_t(v.scene.obstacles.size()) : 0, v.state, 0};
    std::memcpy(&p.counts.w, &fuel.lowerFuelFraction, 4);
    if (v.active) {
        for (size_t i = 0; i < v.scene.sources.size(); ++i) {
            const auto& s = v.scene.sources[i];
            p.sourcePosition[i] = glm::vec4(s.position, s.radius);
            p.sourceRates[i] = {s.fuelKgPerSecond, s.sootKgPerSecond, s.heatWatts,
                                sourceNormalization(d, s, v.scene.obstacles)};
            p.sourceInterval[i] = {s.startSeconds, s.durationSeconds, 0, 0};
            p.sourceVelocity[i] = glm::vec4(s.velocity, 0);
        }
        for (size_t i = 0; i < v.scene.obstacles.size(); ++i) {
            const auto& b = v.scene.obstacles[i];
            p.solidMin[i] = glm::vec4(b.minimum, 0);
            p.solidMax[i] = glm::vec4(b.maximum, 0);
            p.solidVelocity[i] = glm::vec4(b.velocity, 0);
        }
    }
    std::memcpy(g.parameters[frameIndex].mapped, &p, sizeof(p));
    // Descriptor references are immutable until their owning frame fence completes.
    (void)index;
}
void Renderer::Impl::recordVolumeSimulation(uint32_t index) {
    auto& v = volumes;
    auto& g = *v.gpu;
    auto command = frame().command;
    auto set = g.sets[frameIndex].at(index);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, v.pipelineLayout, 0, 1, &set, 0,
                            nullptr);
    VkMemoryBarrier reset{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reset.srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    reset.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &reset, 0, nullptr, 0, nullptr);
    vkCmdFillBuffer(command, g.diagnostics[frameIndex].buffer, 0, 256, 0);
    vkCmdFillBuffer(command, g.ledger.buffer, 0, VK_WHOLE_SIZE, 0);
    VkMemoryBarrier fill{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    fill.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fill.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &fill, 0, nullptr, 0, nullptr);
    if (frame().queries) {
        vkCmdResetQueryPool(command, frame().queries, 6, 22);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame().queries, 6);
    }
    volumeBarrier();
    uint32_t boundOperation = UINT32_MAX;
    auto dispatch = [&](uint32_t phase, float dt, float time, uint32_t pressure = 0) {
        if (boundOperation != phase) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, v.simulation[phase]);
            boundOperation = phase;
        }
        vulkan::VolumePush push{phase, v.state, pressure, 0, dt, time};
        vkCmdPushConstants(command, v.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                           &push);
        if (phase == 7)
            vkCmdDispatch(command, (g.resolution.x + 7) / 8, (g.resolution.y + 7) / 8,
                          (g.resolution.z + 7) / 8);
        else
            vkCmdDispatch(command, (g.resolution.x + 3) / 4, (g.resolution.y + 3) / 4,
                          (g.resolution.z + 3) / 4);
        volumeBarrier();
    };
    if (v.initialize) {
        v.state = 0;
        v.simulated = 0;
        dispatch(0, 0, 0);
        v.initialize = false;
        v.generation = v.scene.generation;
        v.obstaclesDirty = false;
    }
    if (v.obstaclesDirty) {
        dispatch(13, 0, float(v.simulated));
        v.state = 1 - v.state;
        v.obstaclesDirty = false;
    }
    const auto& d = v.scene.domain;
    auto h = d.extent / glm::vec3(g.resolution);
    double maxDt = std::min(1. / 60., .42 / (double(d.maximumVelocity) * (1. / h.x + 1. / h.y + 1. / h.z)));
    const char* reference = std::getenv("THEONE_VOLUME_REFERENCE_SOLVER");
    bool paired = v.simulation[7] && !(reference && std::strcmp(reference, "1") == 0) &&
                  !std::getenv("THEONE_VOLUME_DISABLE_TILED_PRESSURE");
    uint32_t steps = 0;
    bool profile = std::getenv("THEONE_VOLUME_PROFILE_PHASES") != nullptr;
    auto phaseStamp = [&](uint32_t query, bool end) {
        if (profile && frame().queries && steps == 0)
            vkCmdWriteTimestamp(
                command, end ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                frame().queries, query);
    };
    double target = v.active ? v.scene.timeSeconds : 0;
    while (v.simulated + double(float(maxDt)) <= target + 1e-6 && steps < d.maximumSubsteps) {
        float dt = float(maxDt);
        float time = float(v.simulated);
        phaseStamp(12, false);
        dispatch(1, dt, time);
        phaseStamp(13, true);
        phaseStamp(14, false);
        v.state = 1 - v.state;
        dispatch(2, dt, time);
        phaseStamp(15, true);
        phaseStamp(16, false);
        v.state = 1 - v.state;
        dispatch(3, dt, time);
        phaseStamp(17, true);
        phaseStamp(18, false);
        v.state = 1 - v.state;
        dispatch(4, dt, time);
        phaseStamp(19, true);
        phaseStamp(20, false);
        uint32_t pressure = 0;
        for (uint32_t it = 0; it < d.pressureIterations;) {
            uint32_t count = paired && it + 1 < d.pressureIterations ? 2 : 1;
            dispatch(count == 2 ? 7 : 5, dt, time, pressure);
            it += count;
            pressure = 1 - pressure;
        }
        phaseStamp(21, true);
        phaseStamp(22, false);
        dispatch(6, dt, time, pressure);
        phaseStamp(23, true);
        v.state = 1 - v.state;
        for (const auto& s : v.scene.sources) {
            double active =
                std::max(0., std::min(v.simulated + dt, double(s.startSeconds + s.durationSeconds)) -
                                 std::max(v.simulated, double(s.startSeconds)));
            v.stats.injectedFuelKg += active * s.fuelKgPerSecond;
            v.stats.injectedSootKg += active * s.sootKgPerSecond;
            v.stats.injectedHeatJ += active * s.heatWatts;
        }
        v.simulated += dt;
        ++steps;
    }
    // The whole command sequence is recorded before submission; only this slot's
    // uniform is changed, to the known final ping-pong state sampled by rendering.
    auto* p = static_cast<vulkan::VolumeParameters*>(g.parameters[frameIndex].mapped);
    p->counts.z = v.state;
    auto frameStamp = [&](uint32_t query, bool end) {
        if (profile && frame().queries)
            vkCmdWriteTimestamp(
                command, end ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                frame().queries, query);
    };
    frameStamp(24, false);
    dispatch(9, 0, float(v.simulated));
    frameStamp(25, true);
    frameStamp(26, false);
    dispatch(12, 0, float(v.simulated));
    frameStamp(27, true);
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 7);
    v.stats.generation = v.generation;
    v.stats.simulatedSeconds = v.simulated;
    v.stats.timeDebtSeconds = std::max(0., target - v.simulated);
    v.stats.substeps = steps;
    v.stats.allocatedBytes = g.bytes + v.hdrBytes + v.retiredBytes;
    frame().volumeQueryWritten = true;
}
void Renderer::Impl::recordVolumeLighting(uint32_t index) {
    auto command = frame().command;
    auto set = volumes.gpu->sets[frameIndex].at(index);
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame().queries, 8);
    volumeBarrier();
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, volumes.pipelineLayout, 0, 1, &set, 0,
                            nullptr);
    auto r = volumes.gpu->resolution;
    for (uint32_t phase = 0; phase < 2; ++phase) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, volumes.lightingPipeline[phase]);
        vulkan::VolumePush push{};
        push.phase = phase;
        vkCmdPushConstants(command, volumes.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                           &push);
        vkCmdDispatch(command, (r.x + 3) / 4, (r.y + 3) / 4, (r.z + 3) / 4);
        volumeBarrier();
    }
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 9);
}
VolumeReadback Renderer::Impl::captureVolumeState() {
    if (!volumes.gpu)
        throw std::runtime_error("Render a volume frame before requesting a snapshot");
    auto& g = *volumes.gpu;
    VolumeReadback result;
    result.domain = volumes.scene.domain;
    result.domain.resolution = g.resolution;
    size_t count = size_t(g.resolution.x) * g.resolution.y * g.resolution.z;
    result.cells.resize(count);
    uint64_t bytes = count * sizeof(VolumeCell);
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, buffer, memory);
    VkCommandBuffer command{};
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = deviceState.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(deviceState.device, &allocate, &command), "volume readback command");
    VkFence fence{};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(deviceState.device, &fi, nullptr, &fence), "volume readback fence");
    struct Cleanup {
        VkDevice d;
        VkCommandPool pool;
        VkCommandBuffer command;
        VkFence fence;
        VkBuffer buffer;
        VkDeviceMemory memory;
        ~Cleanup() {
            vkDestroyFence(d, fence, nullptr);
            vkFreeCommandBuffers(d, pool, 1, &command);
            vkDestroyBuffer(d, buffer, nullptr);
            vkFreeMemory(d, memory, nullptr);
        }
    } cleanup{deviceState.device, deviceState.pool, command, fence, buffer, memory};
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "begin volume readback");
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(command, g.cells[volumes.state].buffer, buffer, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
    check(vkEndCommandBuffer(command), "end volume readback");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(deviceState.queue, 1, &submit, fence), "submit volume readback");
    check(vkWaitForFences(deviceState.device, 1, &fence, VK_TRUE, UINT64_MAX),
          "wait diagnostic volume snapshot");
    void* mapped{};
    check(vkMapMemory(deviceState.device, memory, 0, bytes, 0, &mapped), "map volume snapshot");
    std::memcpy(result.cells.data(), mapped, size_t(bytes));
    vkUnmapMemory(deviceState.device, memory);
    if (frames[0].serial < frames[1].serial) {
        readVolumeDiagnostics(0);
        readVolumeDiagnostics(1);
    } else {
        readVolumeDiagnostics(1);
        readVolumeDiagnostics(0);
    }
    result.stats = volumes.stats;
    result.stats.fuelKg = result.stats.oxygenKg = result.stats.sootKg = result.stats.productsKg =
        result.stats.nitrogenKg = result.stats.enthalpyJ = 0;
    result.stats.maximumKelvin = 0;
    auto h = result.domain.extent / glm::vec3(g.resolution);
    double dv = double(h.x) * h.y * h.z;
    double sum[6]{};
    for (const auto& c : result.cells) {
        for (int a = 0; a < 4; ++a)
            sum[a] += double(c.species[a]) * dv;
        sum[4] += double(c.thermal.x) * dv;
        sum[5] += double(c.thermal.y) * dv;
        float rho = c.species.x + c.species.y + c.species.z + c.species.w + c.thermal.x;
        if (rho > 1e-6)
            result.stats.maximumKelvin =
                std::max(result.stats.maximumKelvin, c.thermal.y / (rho * result.domain.heatCapacity));
    }
    result.stats.fuelKg = float(sum[0]);
    result.stats.oxygenKg = float(sum[1]);
    result.stats.sootKg = float(sum[2]);
    result.stats.productsKg = float(sum[3]);
    result.stats.nitrogenKg = float(sum[4]);
    result.stats.enthalpyJ = float(sum[5]);
    return result;
}
} // namespace one
