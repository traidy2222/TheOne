#include "render/residency_policy.hpp"
#include <iostream>
#include <stdexcept>
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        using one::render::residencyTransitionReady;
        // An oscillating camera briefly asks for less detail every twenty frames.
        unsigned reference = 0, improved = 0;
        for (unsigned age = 0; age < 20; ++age) {
            reference += residencyTransitionReady(0, 2, age, false, true, true);
            improved += residencyTransitionReady(0, 2, age, false, true);
        }
        require(reference > 0 && improved == 0, "Short oscillation queued demotion");
        require(residencyTransitionReady(0, 2, 32, false, true), "Settled demotion blocked");
        require(residencyTransitionReady(2, 0, 8, false, true), "Promotion delayed");
        require(!residencyTransitionReady(2, 0, 7, false, true), "Promotion hysteresis lost");
        require(residencyTransitionReady(0, 2, 0, true, true), "Pressure recovery delayed");
        require(residencyTransitionReady(2, 0, 0, false, false), "Full residency blocked");
        require(!residencyTransitionReady(0, 0, 100, true, true), "Unchanged texture transitioned");
        std::cout << "PASS oscillation, settling, promotion, memory pressure, full residency\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
