#pragma once
#include "adaptive_ve.hpp"

namespace economy {
namespace price_properties {

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
	return current_price * delta * speed_multiplier;
}

// ===== LOCAL FORK (not upstream) =====
// Positive-domain variant of change(): the relative step is applied through exp(), so a large
// negative supply/demand signal can never make the next price negative. The caller supplies the
// damping divisor that upstream applies to the fast presimulation step.
template<typename VALUE>
VALUE change_exp_runtime(VALUE current_price, VALUE supply, VALUE demand, float speed_multiplier, float damping, float additive_smoothing) {
	supply = supply + additive_smoothing * 1.05f;
	demand = demand + additive_smoothing;

	auto probability_to_sell = adaptive_ve::min<VALUE>(demand / supply, 1.f);
	auto probability_to_buy = adaptive_ve::min<VALUE>(supply / demand, 1.f);
	auto delta = exp_minus_one(probability_to_sell - probability_to_buy);
	auto exponent = delta * (speed_multiplier / damping);
	return current_price * adaptive_ve::exp(exponent);
}

template<typename VALUE, float min_price, float speed_multiplier, float additive_smoothing>
VALUE change_exp(VALUE current_price, VALUE supply, VALUE demand, float damping) {
	return change_exp_runtime<VALUE>(current_price, supply, demand, speed_multiplier, damping, additive_smoothing);
}

// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (ALICE_EXP_PRICE_BALANCE=1): make the price step multiplicative in log space
// and normalize the imbalance by the observed market scale. The scale is max(current flow,
// historical flow, one unit/day); a residual flow far below a normal market's size therefore has
// a correspondingly small price effect instead of being amplified by additive smoothing. Both zero
// flows produce no price movement. The price remains positive without a floor or cap.
template<typename VALUE>
VALUE change_log_balance(VALUE current_price, VALUE supply, VALUE demand, VALUE market_scale, float speed_multiplier) {
	auto const epsilon = adaptive_ve::constant_value<VALUE>(1.f);
	auto const scale = adaptive_ve::max(market_scale, epsilon);
	auto const imbalance = (demand - supply) / scale;
	auto const bounded = adaptive_ve::max(adaptive_ve::constant_value<VALUE>(-1.f), adaptive_ve::min(adaptive_ve::constant_value<VALUE>(1.f), imbalance));
	return current_price * adaptive_ve::exp(bounded * speed_multiplier);
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
template<typename VALUE>
VALUE change_exp(VALUE current_price, VALUE supply, VALUE demand, float damping) {
	return common::change_exp<VALUE, min, speed_multiplier * 200.f, additive_smoothing>(current_price, supply, demand, damping);
}
template<typename VALUE>
VALUE change_exp_scaled(VALUE current_price, VALUE supply, VALUE demand, float speed_scale, float damping) {
	return common::change_exp_runtime<VALUE>(current_price, supply, demand, speed_multiplier * speed_scale, damping, additive_smoothing);
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

