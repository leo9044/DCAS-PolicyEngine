#pragma once

#include "dcas_policy_engine/types.hpp"

namespace dcas {

class PerceptionAdapter {
public:
    NormalizedSnapshot Normalize(const PerceptionInput& input) const {
        NormalizedSnapshot snapshot{};
        snapshot.snapshot_valid = true;
        snapshot.is_attentive = input.is_attentive;
        snapshot.reason = input.reason;
        snapshot.input_snapshot_ts_ms = input.is_attentive_ts_ms;
        return snapshot;
    }
};

}  // namespace dcas
