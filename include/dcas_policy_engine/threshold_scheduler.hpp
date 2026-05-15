#pragma once

#include "dcas_policy_engine/types.hpp"

namespace dcas {

class ThresholdScheduler {
public:
    Thresholds Schedule(SpeedBand speed_band) const {
        Thresholds thresholds{};
        switch (speed_band) {
            case SpeedBand::LOW:
                thresholds.t_warn_eff_s = 3.0;
                thresholds.t_esc_eff_s = 6.0;
                thresholds.t_absent_eff_s = 10.0;
                break;
            case SpeedBand::MID:
                thresholds.t_warn_eff_s = 2.0;
                thresholds.t_esc_eff_s = 4.0;
                thresholds.t_absent_eff_s = 8.0;
                break;
            case SpeedBand::HIGH:
                thresholds.t_warn_eff_s = 1.5;
                thresholds.t_esc_eff_s = 3.0;
                thresholds.t_absent_eff_s = 6.0;
                break;
        }

        if (thresholds.t_warn_eff_s > 5.0) {
            thresholds.t_warn_eff_s = 5.0;
        }
        thresholds.t_recover_hold_s = 3.0;
        return thresholds;
    }
};

}  // namespace dcas
