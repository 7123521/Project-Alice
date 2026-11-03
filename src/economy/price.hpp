#pragma once
#include "adaptive_ve.hpp"

#include <cstdlib>

namespace economy {
namespace price_properties {

// EXPERIMENT (ALICE_EXP_PRICE_SYMMETRIC=1): exp_minus_one() is asymmetric -- it returns +1.667 at x=+1
// and only -0.667 at x=-1, so a shortage pushes the price up 2.5x faster than a glut pulls it down.
// Combined with the smoothed supply/demand history this means a market that was ever short keeps the
// scar: measured, regular clothes still rose while world S/D had already recovered to 1.26.
// This switch keeps the upward rate identical and makes the downward one symmetric.
// LOCAL BRANCH DEFAULT (not upstream): enabled unless ALICE_EXP_PRICE_SYMMETRIC=0.
// Verified over three independent scenario rebuilds (same seed, two-pass generation on):
//   regular clothes 714-817x -> 125-129x (-83%), lumber 2,020-2,630x -> 1,450-1,790x (-29%);
//   ranges do not overlap. Ammunition moves slightly the other way (1,030-1,370 -> 1,260-1,550).
// Note this is NOT a price cap: the upward rate is unchanged (+3.33%/day); only the downward branch is
// made symmetric with it instead of 2.5x slower, which is what the truncated Taylor series produced.
inline bool symmetric_price_change() {
	static bool const v = []() {
		auto s = std::getenv("ALICE_EXP_PRICE_SYMMETRIC");
		return !(s && std::atoi(s) == 0);
	}();
	return v;
}

namespace common {

/*
Truncated tailor series.
x \in [-1, 1]
*/
template<typename VALUE>
VALUE exp_minus_one(VALUE x) {
	return (1.f + (1.f / 2.f + 1.f / 6.f * x) * x) * x;
}

template<typename VALUE, float min_price, float speed_multiplier, float additive_smoothing>
VALUE change(VALUE current_price, VALUE supply, VALUE demand) {
	// avoid singularity
	supply = supply + additive_smoothing * 1.05f;
	demand = demand + additive_smoothing;

	auto probability_to_sell = adaptive_ve::min<VALUE>(demand / supply, 1.f); 
	auto probability_to_buy = adaptive_ve::min<VALUE>(supply / demand, 1.f);

	/*
	auto probability_to_keep_price_when_failed_to_buy = probability_to_buy;
	auto probability_to_keep_price_when_failed_to_sell = probability_to_sell;

	auto speed = speed_multiplier + (0.001f * (1.f - speed_multiplier) / (1.f + current_price));

	auto relative_price_change =
		(
			(1.f - probability_to_sell)
			* (1.f - speed)
			* (1.f - probability_to_keep_price_when_failed_to_sell)
			+ (probability_to_sell + (1.f - probability_to_sell) * probability_to_keep_price_when_failed_to_sell - 1.f)
			+
			(1.f - probability_to_buy)
			* (1.f + speed)
			* (1.f - probability_to_keep_price_when_failed_to_buy)
			+ (probability_to_buy + (1.f - probability_to_buy) * probability_to_keep_price_when_failed_to_buy - 1.f)
		);

	return relative_price_change * (current_price + speed_multiplier);
	*/

	auto delta = exp_minus_one(probability_to_sell - probability_to_buy);
	if(symmetric_price_change()) {
		auto x = probability_to_sell - probability_to_buy;
		delta = x * 1.6667f;
	}
	return current_price * delta * speed_multiplier;
}
}

//inline constexpr float relative_speed_limit = 0.05f;

// the only purpose of upper price bound is to prevent float overflow
// min price prevents singularity at zero

namespace commodity {
inline constexpr float min = 0.000000000001f;
inline constexpr float max = 1'000'000'000'000.f;
inline constexpr float epsilon = min * 0.1f;
inline constexpr float speed_multiplier = 0.02f;
inline constexpr float additive_smoothing = 0.0075f;
template<typename VALUE>
VALUE change(VALUE current_price, VALUE supply, VALUE demand) {
	return common::change<VALUE, min, speed_multiplier, additive_smoothing>(current_price, supply, demand);
}
template<typename VALUE>
VALUE change_fast(VALUE current_price, VALUE supply, VALUE demand) {
	return common::change<VALUE, min, speed_multiplier * 200.f, additive_smoothing>(current_price, supply, demand);
}
}
namespace labor {
inline constexpr float min = 0.000000000001f;
inline constexpr float max = 1'000'000'000'000.f;
inline constexpr float epsilon = min * 0.1f;
inline constexpr float speed_multiplier = 0.02f;
inline constexpr float additive_smoothing = 1.f;
template<typename VALUE>
VALUE change(VALUE current_price, VALUE supply, VALUE demand) {
	return common::change<VALUE, min, speed_multiplier, additive_smoothing>(current_price, supply, demand);
}
template<typename VALUE>
VALUE change_fast(VALUE current_price, VALUE supply, VALUE demand) {
	return common::change<VALUE, min, speed_multiplier * 100.f, additive_smoothing>(current_price, supply, demand);
}
}
namespace service {
inline constexpr float min = 0.000000000001f;
inline constexpr float max = 1'000'000'000'000.f;
inline constexpr float epsilon = min * 0.1f;
inline constexpr float speed_multiplier = 0.02f;
inline constexpr float additive_smoothing = 1.f;
template<typename VALUE>
VALUE change(VALUE current_price, VALUE supply, VALUE demand) {
	return common::change<VALUE, min, speed_multiplier, additive_smoothing>(current_price, supply, demand);
}
}


}
}

