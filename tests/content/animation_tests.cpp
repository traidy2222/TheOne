#include <theone/animation/pose.hpp>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        std::vector<one::Node> nodes(1);
        one::Channel channel;
        channel.node = 0;
        channel.path = 0;
        channel.step = true;
        channel.times = {0.f, .5f, 1.f};
        channel.values = {{0, 0, 0, 0}, {5, 0, 0, 0}, {10, 0, 0, 0}};
        one::Animation animation;
        animation.duration = 1;
        animation.channels.push_back(channel);
        auto expect = [&](float time, bool loop, float position) {
            auto pose = one::samplePose(nodes, {animation}, time, 0, true, -1, 0, 1, loop);
            if (pose[0][3].x != position)
                throw std::runtime_error("STEP key or playback endpoint changed");
        };
        expect(0, false, 0);
        expect(.25f, false, 0);
        expect(.5f, false, 5);
        expect(.75f, false, 5);
        expect(1, false, 10);
        expect(2, false, 10);
        expect(1, true, 0);
        animation.channels[0].times = {0};
        animation.channels[0].values = {{7, 0, 0, 0}};
        expect(0, false, 7);
        expect(2, false, 7);
        std::cout << "PASS STEP boundaries, final hold, loop wrap and single-key clips\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
