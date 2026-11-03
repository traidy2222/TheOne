#include "render/draw_order.hpp"
#include "render/draw_order_cache.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        one::Material opaque, blend;
        blend.blend = true;
        std::vector<one::Draw> draws;
        // Opaque order, reverse depth, equal-depth blend ties, missing material,
        // and pending entries interleaved with both kinds.
        for (unsigned i = 0; i < 9; ++i) {
            one::Draw d;
            d.entity = i;
            d.palette = i + 100;
            d.count = i + 3;
            d.material = (i == 0 || i == 3 || i == 5 || i == 8) ? &blend : &opaque;
            if (i == 7)
                d.material = nullptr;
            draws.push_back(d);
        }
        one::render::DrawOrderCache plan;
        auto source=draws;
        for(int phase=0;phase<7;++phase) {
            auto reference=source,actual=source;
            if(phase>=3)reference[1].material=actual[1].material=&blend;
            auto ready=[&](const auto& d){return phase<4 || d.entity!=3;};
            auto depth=[&](const auto& d){return phase==2?float(9-d.entity):phase>=5?1.f:float(d.entity);};
            one::render::orderDraws(reference,ready,depth);
            bool reused=plan.order(actual,ready,[&](const auto& d,size_t){return depth(d);});
            if(phase==1 && !reused)throw std::runtime_error("Stable blend ordering not reused");
            if(reference.size()!=actual.size())throw std::runtime_error("Cached readiness changed");
            for(size_t i=0;i<reference.size();++i)if(reference[i].entity!=actual[i].entity || reference[i].palette!=actual[i].palette)
                throw std::runtime_error("Cached order differs after depth, material, readiness or tie change");
        }
        unsigned checked = 0, depths = 0;
        one::render::orderDraws(
            draws,
            [&](const auto& d) {
                ++checked;
                return d.entity != 2 && d.entity != 5;
            },
            [&](const auto& d) {
                ++depths;
                return d.entity == 0 ? 1.f : 4.f;
            });
        const std::vector<uint64_t> expected{1, 4, 6, 7, 3, 8, 0};
        if (checked != 9 || depths != 3 || draws.size() != expected.size())
            throw std::runtime_error("Readiness/depth work or retained count changed");
        for (size_t i = 0; i < draws.size(); ++i)
            if (draws[i].entity != expected[i] || draws[i].palette != expected[i] + 100 ||
                draws[i].count != expected[i] + 3)
                throw std::runtime_error("Stable draw ordering or draw payload changed");
        one::render::orderDraws(
            draws, [](const auto&) { return false; },
            [](const auto&) -> float { throw std::runtime_error("Pending depth"); });
        if (!draws.empty())
            throw std::runtime_error("Pending list not empty");
        one::render::orderDraws(draws, [](const auto&) { return true; }, [](const auto&) { return 0.f; });
        std::cout
            << "PASS opaque order, transparent depth/ties, pending filtering and payload preservation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
