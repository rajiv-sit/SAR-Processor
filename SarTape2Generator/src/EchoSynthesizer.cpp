#include "sartape2/EchoSynthesizer.hpp"

#include <cmath>
#include <algorithm>
#include <numbers>
#include <stdexcept>

#include "sartape2/RadarModel.hpp"
#include "sartape2/WaveformModel.hpp"

namespace sartape2 {

namespace {
constexpr double kSpeedOfLight = 299792458.0;

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double norm(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

}  // namespace

double RangeDelayEngine::delaySamples(const PlatformState& platform,
                                      const TargetState& target,
                                      const RadarModel& radar) const {
    const Vec3 delta = subtract(platform.position, target.position);
    const double range = norm(delta);
    const double tau = (range <= 0.0) ? 0.0 : (2.0 * range / kSpeedOfLight);
    const double fs = radar.params().samplingRateHz;
    if (fs <= 0.0) {
        return 0.0;
    }
    return tau * fs;
}

float PhaseHistoryEngine::phaseRadians(const PlatformState& platform,
                                       const TargetState& target,
                                       const RadarModel& radar) const {
    const Vec3 delta = subtract(platform.position, target.position);
    const double range = norm(delta);
    const double fc = radar.params().carrierFrequencyHz;
    if (fc <= 0.0) {
        return 0.0f;
    }
    const double phase = 4.0 * std::numbers::pi_v<double> * fc * range / kSpeedOfLight;
    return static_cast<float>(phase);
}

EchoSynthesizer::EchoSynthesizer() = default;

ComplexBuffer EchoSynthesizer::synthesizePulse(const PlatformState& platform,
                                               const SceneModel& scene,
                                               const WaveformModel& waveform,
                                               const RadarModel& radar,
                                               std::size_t receiveSamples) const {
    const ComplexBuffer chirp = waveform.referenceChirp();
    if (chirp.empty()) {
        return {};
    }

    const auto targets = scene.targetsAt(platform.timeSec);
    if (receiveSamples == 0) {
        receiveSamples = chirp.size();
        for (const auto& target : targets) {
            const double delay = rangeDelay_.delaySamples(platform, target, radar);
            const double extra = std::ceil(delay);
            if (!std::isfinite(extra) || extra < 0.0 ||
                extra >= static_cast<double>(ComplexBuffer().max_size() - chirp.size())) {
                throw std::invalid_argument("Target delay exceeds the receive buffer limit");
            }
            receiveSamples =
                std::max(receiveSamples, chirp.size() + static_cast<std::size_t>(extra));
        }
    }
    ComplexBuffer rx(receiveSamples, ComplexSample(0.0f, 0.0f));
    for (const auto& target : targets) {
        const double delay = rangeDelay_.delaySamples(platform, target, radar);
        if (!std::isfinite(delay) || delay < 0.0) {
            throw std::invalid_argument("Target delay must be finite and nonnegative");
        }
        if (delay >= static_cast<double>(rx.size())) {
            continue;
        }
        const std::size_t baseDelay = static_cast<std::size_t>(std::floor(delay));
        const double frac = delay - static_cast<double>(baseDelay);
        const float phase = phaseHistory_.phaseRadians(platform, target, radar);
        const ComplexSample phasor = std::polar(1.0f, phase);
        const float amplitude = static_cast<float>(target.rcs);
        for (std::size_t i = 0; i < chirp.size(); ++i) {
            const std::size_t idx = baseDelay + i;
            if (idx >= rx.size()) {
                break;
            }
            const ComplexSample sample = chirp[i] * phasor * amplitude;
            rx[idx] += sample * static_cast<float>(1.0 - frac);
            if (frac > 0.0 && idx + 1 < rx.size()) {
                rx[idx + 1] += sample * static_cast<float>(frac);
            }
        }
    }

    return rx;
}

}  // namespace sartape2
