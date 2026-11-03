#pragma once

#include "culture_constants.hpp"

namespace economy {

// EXPERIMENT (ALICE_EXP_GOLD_ANCHOR=1): keep the *real* size of the gold money injection constant by
// scaling the gold->cash rate with world real output (value of daily supply at base costs). Without it the
// money stock grows with the gold output while real capacity grows with city population, so the price level
// drifts upward without bound: measured over 50 years, market money x100,000 against factory output x6.
inline bool experiment_gold_anchor() {
	static bool const v = []() {
		auto s = std::getenv("ALICE_EXP_GOLD_ANCHOR");
		return s && std::atoi(s) != 0;
	}();
	return v;
}
// scale applied to the gold->cash conversion; 1.0 until the anchor computes its first reference
// Whether the gold anchor also runs during presimulation. Upstream presimulation deliberately injects
// money (the +2500/+5000 per-province trickle); letting the anchor rescale that changes the START state,
// which makes anchored and unanchored runs incomparable. Default: anchor only after presimulation.
inline bool experiment_gold_anchor_after_presim() {
	static bool const v = []() {
		auto s = std::getenv("ALICE_EXP_GOLD_ANCHOR_PRESIM");
		return !(s && std::atoi(s) != 0);
	}();
	return v;
}

inline float& gold_scale() {
	static float v = 1.f;
	return v;
}
inline float& gold_reference_output() {
	static float v = 0.f;
	return v;
}

// EXPERIMENT (ALICE_EXP_CONSTRUCTION_WEIGHTED=1): weight the AI's factory-type choice by output margin
// instead of picking uniformly at random among the eligible types (default 0 = upstream behaviour).
// Mechanism-level counters, reset by the dump after printing.
inline std::vector<double>& probe_anchor_up() { static std::vector<double> v(256, 0.0); return v; }
inline std::vector<double>& probe_anchor_down() { static std::vector<double> v(256, 0.0); return v; }
inline std::vector<double>& probe_build_count() { static std::vector<double> v(256, 0.0); return v; }
// 1 = weight by output margin (price/base cost); 2 = weight by expected profit rate per construction cost
inline float experiment_construction_weighted() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_CONSTRUCTION_WEIGHTED");
		return s ? float(std::atof(s)) : 0.f;
	}();
	return v;
}

// EXPERIMENT (ALICE_EXP_COST_ANCHOR=k): add a slow pull of the price toward the commodity's base cost
// ("cost" in goods.txt). The upstream price rule has no cost term at all -- it is a pure supply/demand
// integrator -- so at S/D ~ 1 any price is an equilibrium and prices are path dependent (a transient glut
// parks them at the 1e-12 floor forever, a shortage parks them at 100x forever). The author wrote in
// 2024-01-22 that he was "surprised that the price of the derived goods aren't responding to the cost of
// the inputs", so a slow cost term is in line with his expectation. k is the maximum fractional move per
// day (0 disables).
// LOCAL BRANCH DEFAULT: k = 0.01. Measured over 20 years the price level then grows x276
// (median of 5 rebuilds, range 253-682) instead of x525,700 (control); run-to-run spread
// collapses from 5x to 1.4x; affordability ends at 0.598 (vanilla 0.597); real supply is
// 1.49-1.62e5 versus 1.42-1.49e5. Set ALICE_EXP_COST_ANCHOR=0 to restore upstream behaviour.
inline float experiment_cost_anchor() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_COST_ANCHOR");
		return s ? float(std::atof(s)) : 0.01f;
	}();
	return v;
}

// DIAGNOSTIC (ALICE_EXP_STATE_DEMAND=k): scale army and government construction demand (default 1).
inline float experiment_state_demand() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_STATE_DEMAND");
		return s ? float(std::atof(s)) : 1.f;
	}();
	return v;
}

// DIAGNOSTIC (ALICE_EXP_INJECT_GOOD=<commodity name>, ALICE_EXP_INJECT_MULT=k): keep every market's
// stockpile of that commodity at k times its demand. Used only to find which link of the production chain is
// the binding constraint.
inline char const* experiment_inject_good() {
	static char const* const v = []() {
		return std::getenv("ALICE_EXP_INJECT_GOOD");
	}();
	return v;
}
inline float experiment_inject_mult() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_INJECT_MULT");
		return s ? float(std::atof(s)) : 0.f;
	}();
	return v;
}

// LOCAL BRANCH DEFAULT: the factory hiring budget is computed from profit (gross output value
// minus input cost) instead of gross output value. Upstream sizes the budget from output value
// alone, so when input prices rise the budget shrinks even though the factory may still be
// profitable, and employment falls with it. Set ALICE_EXP_FACTORY_BUDGET_PROFIT=0 for upstream.
inline bool experiment_factory_budget_profit() {
	static bool const v = []() {
		auto s = std::getenv("ALICE_EXP_FACTORY_BUDGET_PROFIT");
		return !(s && std::atoi(s) == 0);
	}();
	return v;
}
// ALICE_EXP_ARTISAN_ENTRY=k -> floor applied to the realised sale probability when an artisan decides
// whether to enter a good (0 = upstream behaviour). See the comment in economy_production.cpp.
inline float experiment_artisan_entry() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_ARTISAN_ENTRY");
		return s ? float(std::atof(s)) : 0.f;
	}();
	return v;
}

// LOCAL BRANCH DEFAULT (not upstream): the author's own two-pass route generation plan from 2026-10-08 --
// "one pass which filters away all small nodes and establishes connection between bodies of high mass and a
// second local pass, which establishes connections between bodies of low mass and high mass".
// The n largest coastal markets become hubs, are connected to each other, and every other coastal market is
// attached to its nearest hub. Verified over three independent scenario rebuilds (same seed):
//   regular clothes 1,010x -> 721x (-29%), lumber 3,000x -> 2,480x (-17%); ranges do not overlap.
// Set ALICE_EXP_TRADE_TWOPASS=0 to restore upstream behaviour exactly.
// ALICE_EXP_SHIP_MULT=k -> multiply the transport capacity each existing ship provides (default 1).
inline float experiment_ship_mult() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_SHIP_MULT");
		return s ? float(std::atof(s)) : 1.f;
	}();
	return v;
}

inline int experiment_trade_twopass() {
	static int const v = []() {
		auto s = std::getenv("ALICE_EXP_TRADE_TWOPASS");
		return s ? std::atoi(s) : 30;
	}();
	return v;
}
// LOCAL BRANCH FIX (default on): only count commodities the nation has actually unlocked when building
// the per-pop needs basket. Upstream computes the "is_active" flag but never uses it, so pops were charged
// for radio / telephones / automobiles / aeroplanes from 1836 onwards -- goods with no producers, whose
// prices sit at the ceiling. Set ALICE_EXP_NEEDS_UNLOCKED=0 to restore upstream behaviour.
// EVALUATED AND REJECTED (kept only as a switch): masking the needs basket with the "unlocked commodity"
// flag produced catastrophic results -- every price hit the ceiling and state treasuries exploded
// (clothes 773x -> 1.7e11x). Either the mask polarity is inverted or unlocked_commodities is not yet
// populated when the basket is first built. Default: off (upstream behaviour).
// EXPERIMENT (ALICE_EXP_MIL_SPENDING=n): override the military-spending slider a nation starts with
// (upstream default 60). Used to test whether the payout loop -- the state pays its soldiers the local
// market cost of their whole needs basket, so rising prices raise payouts, which raise demand, which
// raises prices -- is what pins the supply/demand ratio below one and keeps prices climbing.
inline int experiment_military_spending_init() {
	static int const v = []() {
		auto s = std::getenv("ALICE_EXP_MIL_SPENDING");
		return s ? std::atoi(s) : 60;
	}();
	return v;
}
inline bool experiment_needs_unlocked() {
	static bool const v = []() {
		auto s = std::getenv("ALICE_EXP_NEEDS_UNLOCKED");
		return s && std::atoi(s) != 0;
	}();
	return v;
}
// ALICE_EXP_TRADE_TWOPASS_LINKS=n -> how many of the nearest hubs every coastal market is attached to
// in the second pass (default 1, which is what the first version of the two-pass generator did).
inline int experiment_trade_twopass_links() {
	static int const v = []() {
		auto s = std::getenv("ALICE_EXP_TRADE_TWOPASS_LINKS");
		return s ? std::atoi(s) : 1;
	}();
	return v;
}
inline float experiment_artisan_optimism() {
	static float const v = []() {
		auto s = std::getenv("ALICE_EXP_ARTISAN_OPTIMISM");
		return s ? float(std::atof(s)) : 0.f;
	}();
	return v;
}

constexpr inline float factories_per_state_required_city_size = 60'000.f;

constexpr float stockpile_targets_demand_mult = 0.05f; // Stockpile targets demand will be multplied with this amount
constexpr float max_army_required_spend = 1.5f; // Max percentage of current required army supplies the army budget will attempt to buy (1.5 = 150% of current required army supplies)
constexpr float max_navy_required_spend = 1.5f; // Max percentage of current required navy supplies the army budget will attempt to buy (1.5 = 150% of current required navy supplies)
constexpr float max_con_required_spend = 1.5f; // Max percentage of current required military construction supplies the mil. construction budget will attempt to buy (1.5 = 150% of current required mil. construction supplies)


enum class price_estimation : uint8_t {
	theoretical_max,
	capped_by_availability
};

namespace numerical {
namespace commodity_unit {
inline constexpr float epsilon = 0.0001f;
}
namespace employment_unit {
inline constexpr float epsilon = 0.1f;
}
}

inline constexpr float factory_closed_threshold = 0.0001f;
inline constexpr uint32_t price_history_length = 256;
inline constexpr uint32_t gdp_history_length = 128;

// descides the divisor for the army demand from reinforcements. It is set to 28 to spread out the reinforcement demand over 28 days, as reinforce ticks only happen once a month
constexpr inline float unit_reinforcement_demand_divisor = 28.0f;


inline constexpr float merchant_cut_foreign = 0.05f;
inline constexpr float merchant_cut_domestic = 0.001f;
inline constexpr float effect_of_transportation_scale = 0.0005f;
inline constexpr float trade_distance_covered_by_pair_of_workers_per_unit_of_good = 10.f;
// Huge values could cause massive spikes of demand for transportation labor
inline constexpr float invalid_trade_route_distance = 0.01f;
// Assume 1% of goods is destroyed when shipping from London to NY
inline constexpr float trade_goods_lost_per_km = 0.01f / 6000.f;
inline constexpr float trade_base_multiplicative_decay = 0.0002f;
inline constexpr float trade_base_additive_decay = 0.1f;
inline constexpr float min_trade_expansion_multiplier = 0.15f;
inline constexpr float trade_demand_satisfaction_cutoff = 0.7f;

float constexpr inline base_qol = 0.75f;
constexpr inline uint32_t build_factory = issue_rule::pop_build_factory;
constexpr inline uint32_t expand_factory = issue_rule::pop_expand_factory;
constexpr inline uint32_t can_invest = expand_factory | build_factory;

// stockpile related things:
inline constexpr float stockpile_to_supply = 1.f / 365.f / 3.f;
inline constexpr float stockpile_spoilage = 1.f / 36500.f;
inline constexpr float stockpile_expected_spending_per_commodity = 1'000.f;
inline constexpr float market_savings_target = 1'000'000.f;
inline constexpr float trade_transaction_soft_limit = 1'000.f;

// base subsistence
inline constexpr float subsistence_factor = 5.0f;
inline constexpr float subsistence_score_life = 30.0f;
inline constexpr float subsistence_score_total = subsistence_score_life;

// move to defines later
inline constexpr float payouts_spending_multiplier = 10.f;

inline constexpr float investment_pool_investment_per_day = 0.25f;

// greed drives incomes of corresponding pops up
// while making life worse on average
// profit cuts change distribution of incomes
inline constexpr float aristocrats_greed = 0.1f;
inline constexpr float artisans_greed = 0.1f;
inline constexpr float labor_greed_life = 0.1f;
inline constexpr float capitalists_greed = 0.1f;

inline constexpr float government_stockpile_spoilage = 0.001f; // Decay is 10x larger for govt stockpiles compared to market ones for balance, and to have some constant demand when stockpiling

}
