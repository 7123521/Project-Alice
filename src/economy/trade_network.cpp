#include "trade_network.hpp"
#include "economy_constants.hpp"
#include "province.hpp"
#include "province_templates.hpp"
#include "system_state.hpp"
#include "economy_trade_routes.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>

namespace trade_network {

/*
We want to have at the very least two connections as the baseline:

Havana to Port-au-Prince
	This connection represents local trade connections which are important to make smaller and less populated nations playable.
New York to London
	This connection represents more global trade connections between industrial powerhouses with a lot of population.

At the start of the game:

Population of Cuba is ~250k male adults
Population of Haiti ~160k male adults
Distance between Havana and Port-au-Prince is roughly 1'300 km

Population of New York State is ~600k male adults
Population of South East England is ~900k male adults
Distance between New York and London is roughly 7'000 km

But actual connection between London and New York is handled via additional multiplier due to them being "regional trade centers".
So we are free to artificially decrease the target distance by some factor to weed out excess trade routes.
We don't want to have too many trade routes: initial trade routes count should be something like 8k for the starting scenario.
We use an arbitrary value of CLASSIFIER people to distinguish "global" nodes which participate in the global scoring from local nodes.
Population of big nodes is "clamped" down to this value during calculations of local connectivity.

Local connectivity is tricky because distances and population numbers between states vary a lot and we can't just assume certain values.
So for every state we want to find LOCALLY good candidates, which means collection of some stats on local distances to potential candidates.

Explanation of global multiplier:
We want to pursue 2 contradictory goals.
- Avoid rampant growth of trade routes with growth of population.
- Increase world connectivity as the world grows.
Additive factor ensures that there is at least some growth which depends on population growth.
Averaging with world_pop_1836 / world_population (a value which decreases over time) reduces this multiplier as the game goes on.
*/

constexpr float world_pop_1836 = 245'494'464.f;
constexpr float CLASSIFIER_BIG = 500'000.f;
constexpr float london_to_ny = 7'000.f / 2.f;
constexpr float havana_to_pac = 1'600.f;
constexpr float london_pop = 600'000.f;
constexpr float ny_pop = 900'000.f;
constexpr float havana_pop = 250'000.f;
constexpr float pac_pop = 160'000.f;
constexpr float CLASSIFIER_SMALL = std::max(pac_pop, havana_pop);

// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (ALICE_EXP_TRADE_OPPORTUNITY=1): create a sea route when a coastal market has a
// real export surplus and another coastal market has a real import deficit in the same commodity,
// and the landed price gap is positive after freight, loss and merchant margin. Upstream route
// creation sees population, distance and naval bases; price differences only affect volume after a
// route exists. This experiment derives the route from the same economic quantities used by the
// volume update. Default 0 keeps upstream route creation unchanged.
static bool const trade_opportunity_routes = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_OPPORTUNITY");
	return s && std::atoi(s) != 0;
}();
static float const trade_opportunity_min_value = []() {
	auto s = std::getenv("ALICE_TRADE_OPPORTUNITY_MIN_VALUE");
	return s ? std::clamp(float(std::atof(s)), 0.f, 1'000'000.f) : 0.f;
}();
// LOCAL EXPERIMENT (ALICE_EXP_TRADE_OPPORTUNITY_SEED=1): the opportunity scan already knows the
// physical surplus/deficit and the commodity that clears it. Seed only the newly created forward
// route with that observed quantity. The forecast is not a settlement: stock, seller matching,
// buyer cash/credit and transport still decide how much actually moves, and normal volume decay
// can reduce it. Default 0 keeps the route volume at zero, as before this experiment.
static bool const trade_opportunity_seed_volume = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_OPPORTUNITY_SEED");
	return s && std::atoi(s) != 0;
}();
// ===== LOCAL EXPERIMENT (ALICE_EXP_ROUTE_CREATION_GAP=1): create routes from a price gap =======
// Upstream never lets a price enter route creation: the committed scoring is population x naval base
// x distance (process_trade_partner_candidate, trade_network.cpp:250-323), land routes are created
// from map adjacency alone (:954) and the major-node pass only looks at population (:819).
// `git -S "net_gap"` over this file has no history and HEAD contains no occurrence of it, so the
// committed engine has never had a price-based creation rule. The fork's opportunity scan does use a
// price gap, but it additionally requires a physical surplus in the origin AND a physical deficit in
// the target for the same commodity (the surplus_mask/deficit_mask intersection at :635), which
// fires rarely: 692 routes in 20 years in PBF_1234_20y. This switch adds a second annual pass that
// keeps the price gap and drops the physical mask - that single removal is the whole experiment.
//
// CRITERION (value 1, "pure gap"):
//   gap(o,t,c) = import_price * loss - export_price * (1 + cut) - distance * transport_price
//   with export_price = p_o / conf_o, import_price = p_t * conf_t, conf clamped to >= 0.5,
//        loss = 1 - economy::trade_goods_lost_per_km * distance,
//        cut = economy::merchant_cut_domestic if both ends share an owner else
//              economy::merchant_cut_foreign,
//        transport_price = the owner market's naval transportation price.
//   Create o -> t when max over c of gap > 0. No quantity is needed because no mask supplies one;
//   ranking is by gap itself. Everything else keeps the existing structure: at most one new route
//   per target market per year (the best_gap array is indexed by target), both directions are
//   created with owner = origin market, and the created volume stays 0 (no seed).
//   No anchor, floor, cap or quota is added: the only change is dropping a precondition.
//
// WHY DEFAULT IS BIT-IDENTICAL: the flag is read once at static initialisation and the whole pass is
// guarded by `if(route_creation_gap != 0)`. With 0 the block is not entered, so no state is read,
// written or reordered and no floating-point operation is executed; every dump stays identical.
//
// PRE-REGISTERED (5 y, seed 1234, baseline PBF_1234_20y @1856): routes +17..33% (600..790 new per
//   year, capped by the one-per-target rule), sea_routes +600..790/year, trade_volume +8..23%,
//   cross-continent share 2.3% -> 3.5..5.5%, grain cross-market CV -10..30% but NOT down to
//   0.25..0.50 (each edge carries only 5..22 units/day; cross edges currently average 5.2),
//   factories +/-5% with no predictable sign, satisfaction +1..3% with the origin side offsetting.
//
// FALSIFICATION: (a) fewer than ~2%/year new routes means the price gap fires as rarely as the
//   masked scan and the price data holds no exploitable arbitrage; (b) routes grow but trade_volume
//   does not means created edges never trade and creation is not the binding constraint; (c) the
//   cross-continent share stays below 3% means the largest gaps are intra-continental; (d) a CV
//   change under 10% means 10k-80k units/day do not move prices and creation cannot fix dispersion;
//   (e) factories moving >15% one way or satisfaction falling >2% means the origin is being drained;
//   (f) any dump differing with the switch off means the equivalence claim is broken.
// =============================================================================================
static int const route_creation_gap = []() {
	auto s = std::getenv("ALICE_EXP_ROUTE_CREATION_GAP");
	if(!s)
		return 0;
	return std::clamp(std::atoi(s), 0, 1);
}();
struct parent_link {
	dcon::market_id leaf;
	dcon::market_id parent;
	float dist;
};

float local_base_trade_distance(sys::state const& state, dcon::state_instance_id origin, dcon::province_id sampled_province, float baseline_population) {
	auto local_base_distance = 20'000.f;
	// gather stats required for local connectivity
	state.world.for_each_state_instance([&](auto sid) {
		if(sid == origin)
			return;
		auto target_capital = state.world.state_instance_get_capital(sid);
		auto distance_approximation = province::direct_distance_km(state, sampled_province, target_capital) + 1.f;
		float score_target = state.world.state_instance_get_demographics(sid, demographics::total);
		if(score_target > baseline_population * 0.75f) {
			local_base_distance = std::min(local_base_distance, distance_approximation);
		}
	});
	return local_base_distance * 2.f;
}

float get_local_average_population(sys::state const& state, dcon::state_instance_id origin, dcon::province_id sampled_province, float baseline_population, float base_distance) {
	auto local_average_population = baseline_population;
	auto local_count = 1.f;
	state.world.for_each_state_instance([&](auto sid) {
		if(sid == origin)
			return;
		auto target_capital = state.world.state_instance_get_capital(sid);
		auto distance_approximation = province::direct_distance_km(state, sampled_province, target_capital) + 1.f;
		float score_target = state.world.state_instance_get_demographics(sid, demographics::total);
		if(distance_approximation < base_distance) {
			local_count += 1.f;
			local_average_population += score_target;
		}
	});
	return std::max(baseline_population, local_average_population / local_count);
}



void recalculate_markets_distance(sys::state& state) {
	state.world.execute_parallel_over_market([&](auto markets) {
		auto sids = state.world.market_get_zone_from_local_market(markets);
		auto population = ve::apply([&](auto sid) {
			return state.world.state_instance_get_demographics(sid, demographics::total);
			}, sids
		);
		auto naval_base = ve::to_float(ve::apply([&](auto sid) {
			return military::state_naval_base_level(state, sid);
			}, sids
		));

		auto railroads = ve::to_float(ve::apply([&](auto sid) {
			return military::state_railroad_level(state, sid);
			}, sids
		));

		auto throughput = 100.f + 8000.f * naval_base + 1000.f * railroads + population / 50.f;

		state.world.market_set_max_throughput(markets, throughput);
	});

	static tagged_vector<fixed_bool_t, dcon::trade_route_id> to_delete;
	to_delete.resize(state.world.trade_route_size());
	std::fill(to_delete.begin(), to_delete.end(), false);

	// distance is calculated in km now
	// speed is accounted for in calculation of transportation supply

	state.world.execute_parallel_over_trade_route([&](auto routes) {
		// recalculate effective distance
		auto markets_0 = state.world.trade_route_get_origin(routes);
		auto markets_1 = state.world.trade_route_get_target(routes);

		auto sids_0 = state.world.market_get_zone_from_local_market(markets_0);
		auto sids_1 = state.world.market_get_zone_from_local_market(markets_1);

		ve::apply([&](auto sid_0, auto sid_1, auto route) {
			if(state.world.trade_route_get_is_sea_route(route)) {
				std::vector<dcon::province_id> path{ };
				dcon::province_id p_prev{ };

				auto coast_0 = province::state_get_coastal_capital(state, sid_0);
				auto coast_1 = province::state_get_coastal_capital(state, sid_1);
				// if no coastal caapital on either one of them, then delete the sea trade route as it dosent make sense to keep
				if(!coast_1 || !coast_0) {
					to_delete[route] = true;
					return;
				}
				auto total_distance_km = sea_trade_route_distance(state, coast_0, coast_1);
				state.world.trade_route_set_distance_km(route, total_distance_km);
			} else {
				std::vector<dcon::province_id> path{ };
				dcon::province_id p_prev{ };

				auto market_0_center = state.world.state_instance_get_capital(sid_0);
				auto market_1_center = state.world.state_instance_get_capital(sid_1);
				path = province::make_land_trade_path(state, market_0_center, market_1_center);

				p_prev = market_0_center;

				auto ps = path.size();
				auto total_distance_km = 0.f;
				auto worst_movement_cost = 0.f;
				auto min_railroad_level = 0.f;

				for(size_t i = 0; i < ps; i++) {
					auto p_current = path[i];
					auto adj = state.world.get_province_adjacency_by_province_pair(p_prev, p_current);
					auto bits = state.world.province_adjacency_get_type(adj);

					float distance = province::distance_km(state, adj);
					float sum_mods =
						state.world.province_get_modifier_values(p_current, sys::provincial_mod_offsets::movement_cost)
						+ state.world.province_get_modifier_values(p_prev, sys::provincial_mod_offsets::movement_cost);

					// no magical speed-up... for now
					float local_terrain_mod = std::max(0.f, sum_mods / 2.f - 1.f);

					// rivers ignore terrain
					if(bits & province::border::river_connection_bit) {
						local_terrain_mod = 0.f;
					}

					float local_distance_km = distance * std::max(0.1f, (1.f + local_terrain_mod));

					total_distance_km += local_distance_km;

					p_prev = p_current;
				}

				state.world.trade_route_set_distance_km(route, total_distance_km);
			}
		}, sids_0, sids_1, routes);
	});
	// delete marked trade routes
	for(auto i = state.world.trade_route_size(); i-- > 0;) {
		dcon::trade_route_id trade_route{ dcon::trade_route_id::value_base_t(i) };
		if(to_delete[trade_route]) {
			state.world.delete_trade_route(trade_route);
		}
	}
}

float sea_trade_route_distance(sys::state const& state, dcon::province_id a, dcon::province_id b) {
	std::vector<dcon::province_id> path{ };
	dcon::province_id p_prev{ };
	path = province::make_sea_trade_route_path(state, a, b);
	auto ps = path.size();
	auto distance_km = 1.f;
	p_prev = a;
	for(int i = int(path.size()) - 1; i >= 0; i--) {
		auto p_current = path[i];
		auto adj = state.world.get_province_adjacency_by_province_pair(p_prev, p_current);
		// sea routes ignore modifiers
		float local_distance = province::distance_km(state, adj);
		distance_km += local_distance;
		p_prev = p_current;
	}
	return distance_km;
}

trade_candidate_information process_trade_partner_candidate(
	sys::state const& state, sea_trade_candidate_data origin, sea_trade_candidate_data target,
	float local_base_distance, float local_base_population, float global_mult
) {
	trade_candidate_information result {};
	if(target.state_instance == origin.state_instance) {
		result.reason = candidate_failure_reason::same_state;
		return result;
	}
	if(!target.coast) {
		result.reason = candidate_failure_reason::not_coastal;
		return result;
	}

	bool route_already_exists = false;
	state.world.market_for_each_trade_route_as_origin(origin.market, [&](auto route) {
		auto route_destination = state.world.trade_route_get_target(route);
		auto route_owner = state.world.trade_route_get_owner(route);
		auto sea_route = state.world.trade_route_get_is_sea_route(route);
		if(route_destination == target.market && route_owner == origin.market && sea_route) {
			route_already_exists = true;
		}
	});
	if(route_already_exists) {
		result.reason = candidate_failure_reason::already_exists;
		return result;
	}

	result.potentially_valid =  true;
	
	float mult = 1.f;
	mult += std::min(origin.naval_base_level, target.naval_base_level) * naval_base_level_to_market_attractiveness;
	auto distance_approximation = province::direct_distance_km(state, origin.coast, target.coast) + 1.f;

	float score_origin = origin.population;
	float score_target = target.population;

	auto approx_big_score_distance = std::min(1.f, london_to_ny / distance_approximation);
	auto approx_small_score_distance = local_base_distance / distance_approximation;

	score_origin *= global_mult;
	score_target *= global_mult;

	auto pop_score_origin_big = std::min(4.f, (score_origin > CLASSIFIER_BIG ? score_origin : 0.f) / london_pop);
	auto pop_score_target_big = std::min(4.f, (score_target > CLASSIFIER_BIG ? score_target : 0.f) / ny_pop);

	auto pop_score_origin_small = std::min(2.f, score_origin / local_base_population);
	auto pop_score_target_small = std::min(2.f, score_target / local_base_population);

	float score_big_approx = (mult * pop_score_origin_big * (0.001f + pop_score_target_big)) * approx_big_score_distance;
	float score_small_approx = (mult * pop_score_origin_small * (0.001f + pop_score_target_small)) * approx_small_score_distance * approx_small_score_distance * approx_small_score_distance;

	result.approx_big_score = score_big_approx;
	result.approx_score = score_small_approx;

	if(!(score_big_approx >= 1.f || score_small_approx >= 1.f)) {
		result.reason = candidate_failure_reason::low_approximate_score;
		return result;
	}

	auto distance_km = sea_trade_route_distance(state, origin.coast, target.coast);
	auto big_score_distance = std::min(1.f, london_to_ny / distance_km);
	auto small_score_distance = local_base_distance / distance_km;
	float score_big = (mult * pop_score_origin_big * (0.001f + pop_score_target_big)) * big_score_distance;
	float score_small = (mult * pop_score_origin_small * (0.001f + pop_score_target_small)) * small_score_distance * small_score_distance * small_score_distance;

	result.big_score = score_big;
	result.score = score_small;

	if(result.big_score <= 1.f && result.score <= 1.f) {
		result.reason = candidate_failure_reason::none;
	}

	return result;
}


// ===== LOCAL EXPERIMENT (ALICE_EXP_ROUTE_EXIT=1): give trade routes an exit ===============
// Upstream removes a trade route only when one of its ends loses its coastal capital
// (trade_network.cpp:172-175), so a route that never clears its first trade stays in the network
// forever. Measured on PBF_1234_20y (routes.csv.zst, 1856-01-01): 966 of 11 570 routes carry zero
// volume (8.35%) - 594/5 418 sea (11.0%), 372/6 152 land (6.0%); every route created after 1836
// (692, all sea) had zero volume on the day it first appeared and 169 of them never traded at all.
//
// (1) WHAT "ZERO VOLUME" MEANS HERE
//     A route is eligible when the sum of volume[route, c] over every commodity except money is
//     exactly 0.0 at the annual check. Not "<= eps": the volume update already clips its result to
//     max(0, ...) (economy_trade_routes.cpp:579-585), so 0 is a value the engine produces on its
//     own, and any positive epsilon would be an artificial floor.
//     The check runs once a year, on the January pass, BEFORE new routes are created (this function
//     is called from system_state.cpp:5423 and, at start-up, from generate_initial_trade_routes:862).
//     Creation happens later in the same call, so a freshly created route can never be retired at
//     birth, and a route retired here has survived at least 365 daily volume updates since it last
//     appeared. That is the answer to "how many days": one full year, observed once.
//     Second condition: no commodity may show positive predicted profit
//     (predict_trade_route_volume_change().profit_score > 0 - the same net_gap test the volume
//     update uses at economy_trade_routes.cpp:1043). Zero volume has two causes: the route is truly
//     dead, or it is blockaded/banned, in which case reset_route zeroes it every day
//     (economy_trade_routes.cpp:1149-1151). Without this condition one war would permanently delete
//     whole sea corridors. Both conditions must hold to retire a route.
//     No quota, no cap, no floor: how many routes go is whatever the state says.
//
// (2) WHAT MUST BE CLEANED WHEN A ROUTE IS DELETED
//     delete_trade_route() (src/gamestate/dcon_generated.hpp:133689-133750) already does every
//     object-level cleanup: it unlinks the route from both markets' route lists
//     (internal_trade_route_set_origin/target), moves the last route into the freed slot copying
//     every property including the volume array (m_volume.copy_value), zeroes the vacated slot and
//     decrements size_used. A route holds a daily rate, not a stock, so no cargo or money is
//     returned.
//     The hazard is not a dangling pointer but ID COMPRESSION: after any deletion, every id above
//     the erased one shifts down by one, so a trade_route_id held across the deletion silently names
//     a different route. This patch therefore (a) collects the ids to retire first and deletes
//     nothing while iterating, and (b) erases from the highest id down, which keeps every id below
//     the erased one stable.
//     Checked and NOT needed: market-level route counts (markets.csv recomputes its `routes` column
//     per dump, headless_run.hpp:882), trade_credit_outstanding (indexed by market, not by route,
//     economy_trade_routes.cpp:258), the seller-share cache and the province banks (indexed by
//     province x commodity, economy.cpp:2452), and the static to_delete buffer inside
//     recalculate_markets_distance (trade_network.cpp:149-151), which is resized on every call and
//     whose call site (system_state.cpp:5424) runs after this one (5423).
//
// (3) WHY SWITCHING IT OFF IS BIT-IDENTICAL
//     The flag is read once during static initialisation. With it off, retire_dead_trade_routes()
//     returns at its first statement and reads or writes no state; the call site still runs, but it
//     calls a function whose body is a single comparison. The patch adds no write, no reordering of
//     existing writes and no floating-point operation on shared state. The only residual effect is
//     a function-local `static bool route_exit_primed` flipping to true on the first call: nothing
//     else reads it and it never enters a branch that changes state, so the simulated state and
//     every dump stay identical to the unpatched binary.
// =========================================================================================
static bool const route_exit = []() {
	auto s = std::getenv("ALICE_EXP_ROUTE_EXIT");
	return s && std::atoi(s) != 0;
}();

static void retire_dead_trade_routes(sys::state& state) {
	if(!route_exit)
		return;
	std::vector<dcon::trade_route_id> retire;
	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		if(!state.world.trade_route_is_valid(route))
			return;
		if(!state.world.trade_route_get_origin(route) || !state.world.trade_route_get_target(route))
			return;
		auto carried = 0.0;
		for(uint32_t k = 1; k < state.world.commodity_size(); ++k) {
			dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
			if(state.world.commodity_get_money_rgo(cid))
				continue;
			carried += double(state.world.trade_route_get_volume(route, cid));
		}
		if(carried > 0.0)
			return;
		bool profitable_somewhere = false;
		for(uint32_t k = 1; k < state.world.commodity_size() && !profitable_somewhere; ++k) {
			dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
			if(state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
				continue;
			auto const prediction = economy::predict_trade_route_volume_change(state, route, cid);
			if(prediction.profit_score > 0.f)
				profitable_somewhere = true;
		}
		if(!profitable_somewhere)
			retire.push_back(route);
	});
	// erase from the highest id down: see (2) above - delete_trade_route() compresses ids
	for(auto i = retire.size(); i-- > 0;) {
		state.world.delete_trade_route(retire[i]);
	}
}

void generate_sea_trade_routes(sys::state& state) {
	// The first call comes from generate_initial_trade_routes, where every route was created moments
	// ago and therefore still has no volume; only retire on later (annual) passes.
	static bool route_exit_primed = false;
	if(route_exit_primed)
		retire_dead_trade_routes(state);
	route_exit_primed = true;
	// buffer for "capitals" of connected regions:
	std::array<dcon::state_instance_id, 4000> capital_of_region = {};
	std::array<float, 10000> population_of_region = { };
	std::vector<sea_trade_candidate_data> candidates { };
	struct market_flow_data {
		std::vector<float> surplus;
		std::vector<float> deficit;
		std::vector<float> price;
		std::vector<float> confidence;
		std::vector<uint64_t> surplus_mask;
		std::vector<uint64_t> deficit_mask;
	};
	struct trade_opportunity {
		bool found = false;
		float value = 0.f;
		float quantity = 0.f;
		float net_gap = 0.f;
		dcon::commodity_id commodity{};
	};
	struct best_trade_route {
		bool found = false;
		sea_trade_candidate_data origin {};
		sea_trade_candidate_data target {};
		dcon::market_id owner {};
		float value = 0.f;
		float quantity = 0.f;
		float net_gap = 0.f;
		dcon::commodity_id commodity{};
	};
	std::vector<market_flow_data> market_flows;
	std::vector<sea_trade_candidate_data> market_candidates;
	std::vector<best_trade_route> best_trade_routes;
	if(trade_opportunity_routes) {
		best_trade_routes.resize(state.world.market_size());
	}

	state.world.for_each_state_instance([&](auto candidate) {
		// auto capital = state.world.state_instance_get_capital(candidate);
		auto capital = province::state_get_coastal_capital(state, candidate);
		if(capital) {
			auto connected_region = state.world.province_get_connected_coast_id(capital);
			auto size = state.world.state_instance_get_demographics(
				candidate, demographics::total
			);

			population_of_region[connected_region] += size;

			if(!capital_of_region[connected_region]) {
				capital_of_region[connected_region] = candidate;
			} else {
				auto current_population = state.world.state_instance_get_demographics(
					capital_of_region[connected_region], demographics::total
				);
				if(size > current_population) {
					capital_of_region[connected_region] = candidate;
				}
			}
		}
	});

	float world_population = 0.f;
	state.world.for_each_nation([&](auto nation) {
		world_population += state.world.nation_get_demographics(nation, demographics::total);
	});
	//state.console_log("WORLD POP : " + std::to_string(world_population));
	const float global_multiplier = (0.1f + 0.9f * world_pop_1836 / world_population) * state.defines.alice_trade_route_score_multiplier;


	state.world.for_each_state_instance([&](auto candidate) {
		sea_trade_candidate_data data { };
		data.coast = province::state_get_coastal_capital(state, candidate);
		data.market = state.world.state_instance_get_market_from_local_market(candidate);
		data.naval_base_level = (float)(military::state_naval_base_level(state, candidate));
		data.owner = state.world.state_instance_get_nation_from_state_ownership(candidate);
		data.population = state.world.state_instance_get_demographics(candidate, demographics::total);
		data.state_instance = candidate;

		if(!data.coast) {
			return;
		}

		data.connected_coastal_region = state.world.province_get_connected_coast_id(data.coast);
		if(data.state_instance == capital_of_region[data.connected_coastal_region]) {
			auto region_population = population_of_region[data.connected_coastal_region];
			data.population = std::max(data.population, region_population * 0.3f);
		}

		candidates.push_back(data);
	});

	for(auto& origin : candidates) {
		auto state_owner_capital = state.world.nation_get_capital(origin.owner);
		auto state_owner_capital_state = state.world.province_get_state_membership(state_owner_capital);
		auto local_base_distance = local_base_trade_distance(state, origin.state_instance, origin.coast, origin.population);
		auto local_average_population = get_local_average_population(state, origin.state_instance, origin.coast, origin.population, local_base_distance);

		for(auto& target : candidates) {
			auto origin_is_regional_capital = capital_of_region[origin.connected_coastal_region] == origin.state_instance;
			auto target_is_regional_capital = capital_of_region[target.connected_coastal_region] == target.state_instance;
			auto origin_is_capital = state_owner_capital_state == origin.state_instance;
			auto target_is_capital = state_owner_capital_state == target.state_instance;

			auto capital_vs_region = origin_is_capital && target_is_regional_capital;
			auto region_vs_capital = target_is_capital && origin_is_regional_capital;
			auto capital_and_connected_region = capital_vs_region || region_vs_capital;

			bool same_owner = target.owner == origin.owner;
			bool different_region = origin.connected_coastal_region != target.connected_coastal_region;
			bool must_connect = same_owner && different_region && capital_and_connected_region;
			auto score = process_trade_partner_candidate(state, origin, target, local_base_distance, local_average_population, global_multiplier);
			must_connect = must_connect || score.score >= 1.f || score.big_score >= 1.f;

			if(must_connect && score.potentially_valid) {
				auto route_owner = origin.market;
				{
					auto new_route = state.world.force_create_trade_route(origin.market, target.market);
					state.world.trade_route_set_is_sea_route(new_route, true);
					state.world.trade_route_set_owner(new_route, route_owner);
				}
				{
					auto opposite_route = state.world.force_create_trade_route(target.market, origin.market);
					state.world.trade_route_set_is_sea_route(opposite_route, true);
					state.world.trade_route_set_owner(opposite_route, route_owner);
				}
			}
		}
	}

	if(trade_opportunity_routes) {
		auto choose_route_owner = [](sea_trade_candidate_data const& a, sea_trade_candidate_data const& b) {
			if(a.naval_base_level != b.naval_base_level)
				return a.naval_base_level > b.naval_base_level ? a.market : b.market;
			return a.population >= b.population ? a.market : b.market;
		};

		std::vector<int32_t> market_candidate_index(state.world.market_size(), -1);
		for(auto const& candidate : candidates) {
			auto const mi = candidate.market.index();
			if(mi < 0 || size_t(mi) >= market_candidate_index.size())
				continue;
			auto const index = market_candidate_index[mi];
			if(index < 0) {
				market_candidate_index[mi] = int32_t(market_candidates.size());
				market_candidates.push_back(candidate);
			} else {
				auto& previous = market_candidates[size_t(index)];
				if(candidate.population > previous.population
					|| (candidate.population == previous.population && candidate.naval_base_level > previous.naval_base_level)) {
					previous = candidate;
				}
			}
		}

		uint32_t const commodity_count = state.world.commodity_size();
		uint32_t const mask_word_count = (commodity_count + 63u) / 64u;
		market_flows.resize(state.world.market_size());
		for(auto& flow : market_flows) {
			flow.surplus.assign(commodity_count, 0.f);
			flow.deficit.assign(commodity_count, 0.f);
			flow.price.assign(commodity_count, 0.f);
			flow.confidence.assign(commodity_count, 1.f);
			flow.surplus_mask.assign(mask_word_count, 0);
			flow.deficit_mask.assign(mask_word_count, 0);
		}

		auto set_mask_bit = [](std::vector<uint64_t>& mask, uint32_t bit) {
			mask[size_t(bit >> 6)] |= uint64_t(1) << (bit & 63u);
		};

		state.world.for_each_market([&](dcon::market_id market) {
			auto& flow = market_flows[market.index()];
			for(uint32_t i = 1; i < commodity_count; ++i) {
				dcon::commodity_id commodity{ dcon::commodity_id::value_base_t(i) };
				if(state.world.commodity_get_money_rgo(commodity) || state.world.commodity_get_is_local(commodity))
					continue;
				if(state.world.commodity_get_rgo_amount(commodity) > 0.f
					&& !state.world.commodity_get_actually_exists_in_nature(commodity)) {
					continue;
				}
				auto supply = state.world.market_get_aggregated_supply_history(market, commodity);
				auto demand = state.world.market_get_aggregated_demand_history(market, commodity);
				auto price = state.world.market_get_price(market, commodity);
				auto confidence = state.world.market_get_price_confidence(market, commodity);
				if(!std::isfinite(supply) || !std::isfinite(demand) || !std::isfinite(price) || !std::isfinite(confidence))
					continue;
				if(!(price > 0.f))
					continue;
				supply = std::max(0.f, supply);
				demand = std::max(0.f, demand);
				flow.price[i] = price;
				flow.confidence[i] = std::max(0.5f, confidence);
				if(supply > demand + 0.0001f) {
					flow.surplus[i] = supply - demand;
					set_mask_bit(flow.surplus_mask, i);
				}
				if(demand > supply + 0.0001f) {
					flow.deficit[i] = demand - supply;
					set_mask_bit(flow.deficit_mask, i);
				}
			}
		});

		auto evaluate_opportunity = [&](sea_trade_candidate_data const& origin, sea_trade_candidate_data const& target, float distance, float transport_price) {
			trade_opportunity best{};
			auto const& origin_flow = market_flows[origin.market.index()];
			auto const& target_flow = market_flows[target.market.index()];
			for(uint32_t word = 0; word < mask_word_count; ++word) {
				auto bits = origin_flow.surplus_mask[word] & target_flow.deficit_mask[word];
				while(bits != 0) {
					auto const bit = uint32_t(std::countr_zero(bits));
					bits &= bits - 1;
					auto const i = word * 64u + bit;
					if(i >= commodity_count)
						continue;

					auto const quantity = std::min(origin_flow.surplus[i], target_flow.deficit[i]);
					if(!(quantity > 0.0001f))
						continue;

					auto const loss = std::max(0.f, 1.f - economy::trade_goods_lost_per_km * distance);
					if(!(loss > 0.f))
						continue;

					auto const export_price = origin_flow.price[i] / origin_flow.confidence[i];
					auto const import_price = target_flow.price[i] * target_flow.confidence[i];
					auto const merchant_cut = origin.owner == target.owner ? economy::merchant_cut_domestic : economy::merchant_cut_foreign;
					auto const net_gap = import_price * loss - export_price * (1.f + merchant_cut) - distance * transport_price;
					if(!(net_gap > 0.f) || !std::isfinite(net_gap))
						continue;

					auto const value = quantity * net_gap;
					if(value > best.value) {
						best.found = true;
						best.value = value;
						best.quantity = quantity;
						best.net_gap = net_gap;
						best.commodity = dcon::commodity_id{ dcon::commodity_id::value_base_t(i) };
					}
				}
			}
			return best;
		};

		auto consider_opportunity = [&](trade_opportunity const& opportunity, sea_trade_candidate_data const& origin, sea_trade_candidate_data const& target, dcon::market_id owner) {
			if(!opportunity.found)
				return;
			auto& best = best_trade_routes[target.market.index()];
			if(opportunity.value > best.value) {
				best.found = true;
				best.origin = origin;
				best.target = target;
				best.owner = owner;
				best.value = opportunity.value;
				best.quantity = opportunity.quantity;
				best.net_gap = opportunity.net_gap;
				best.commodity = opportunity.commodity;
			}
		};

		for(size_t i = 0; i < market_candidates.size(); ++i) {
			auto const& origin = market_candidates[i];
			for(size_t j = i + 1; j < market_candidates.size(); ++j) {
				auto const& target = market_candidates[j];
				if(origin.market == target.market)
					continue;

				auto const owner = choose_route_owner(origin, target);
				auto transport_price = state.world.market_get_naval_transportation_price(owner);
				if(!std::isfinite(transport_price))
					continue;
				transport_price = std::max(0.f, transport_price);

				auto const direct_distance = province::direct_distance_km(state, origin.coast, target.coast) + 1.f;
				if(!(direct_distance > 0.f) || !std::isfinite(direct_distance))
					continue;

				auto const forward_near = evaluate_opportunity(origin, target, direct_distance, transport_price);
				auto const backward_near = evaluate_opportunity(target, origin, direct_distance, transport_price);
				if(!forward_near.found && !backward_near.found)
					continue;

				auto const distance = sea_trade_route_distance(state, origin.coast, target.coast);
				if(!(distance > 0.f) || !std::isfinite(distance))
					continue;

				consider_opportunity(evaluate_opportunity(origin, target, distance, transport_price), origin, target, owner);
				consider_opportunity(evaluate_opportunity(target, origin, distance, transport_price), target, origin, owner);
			}
		}

		for(auto const& best : best_trade_routes) {
			if(!best.found || best.value < trade_opportunity_min_value || best.origin.market == best.target.market)
				continue;

			bool route_exists = false;
			state.world.market_for_each_trade_route_as_origin(best.origin.market, [&](auto route) {
				if(state.world.trade_route_get_target(route) == best.target.market && state.world.trade_route_get_is_sea_route(route))
					route_exists = true;
			});
			state.world.market_for_each_trade_route_as_origin(best.target.market, [&](auto route) {
				if(state.world.trade_route_get_target(route) == best.origin.market && state.world.trade_route_get_is_sea_route(route))
					route_exists = true;
			});
			if(route_exists)
				continue;

			auto new_route = state.world.force_create_trade_route(best.origin.market, best.target.market);
			state.world.trade_route_set_is_sea_route(new_route, true);
			state.world.trade_route_set_owner(new_route, best.owner);
			if(trade_opportunity_seed_volume && best.commodity && !state.world.commodity_get_money_rgo(best.commodity)
				&& std::isfinite(best.quantity) && best.quantity > 0.f) {
				state.world.trade_route_set_volume(new_route, best.commodity, best.quantity);
			}
			auto opposite_route = state.world.force_create_trade_route(best.target.market, best.origin.market);
			state.world.trade_route_set_is_sea_route(opposite_route, true);
			state.world.trade_route_set_owner(opposite_route, best.owner);
		}
	}

	if(route_creation_gap != 0) {
		auto const gap_market_count = uint32_t(state.world.market_size());
		auto const gap_commodity_count = uint32_t(state.world.commodity_size());

		// price and confidence per market x commodity; money-like and local goods are excluded
		std::vector<float> gap_price(size_t(gap_market_count) * size_t(gap_commodity_count), 0.f);
		std::vector<float> gap_confidence(size_t(gap_market_count) * size_t(gap_commodity_count), 1.f);
		state.world.for_each_market([&](dcon::market_id market) {
			auto const base = size_t(market.index()) * size_t(gap_commodity_count);
			for(uint32_t k = 1; k < gap_commodity_count; ++k) {
				dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
				if(state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					continue;
				auto const price = state.world.market_get_price(market, cid);
				if(!(price > 0.f) || !std::isfinite(price))
					continue;
				gap_price[base + k] = price;
				gap_confidence[base + k] = std::max(0.5f, state.world.market_get_price_confidence(market, cid));
			}
		});

		struct gap_candidate {
			dcon::market_id market{};
			dcon::province_id coast{};
			dcon::nation_id owner{};
		};
		std::vector<gap_candidate> coastal;
		state.world.for_each_market([&](dcon::market_id market) {
			auto const sid = state.world.market_get_zone_from_local_market(market);
			if(!sid)
				return;
			auto const coast = province::state_get_coastal_capital(state, sid);
			if(!coast)
				return;
			gap_candidate candidate{};
			candidate.market = market;
			candidate.coast = coast;
			candidate.owner = state.world.state_instance_get_nation_from_state_ownership(sid);
			coastal.push_back(candidate);
		});

		struct gap_best_route {
			bool found = false;
			size_t origin = 0;
			size_t target = 0;
			float value = 0.f;
		};
		if(coastal.size() >= 2) {
			auto sea_route_exists = [&](dcon::market_id a, dcon::market_id b) {
				bool found = false;
				state.world.market_for_each_trade_route_as_origin(a, [&](auto route) {
					if(!found && state.world.trade_route_get_target(route) == b
						&& state.world.trade_route_get_is_sea_route(route))
						found = true;
				});
				return found;
			};
			auto evaluate_gap = [&](gap_candidate const& from, gap_candidate const& to, float distance, float transport_price) {
				trade_opportunity best{};
				auto const from_base = size_t(from.market.index()) * size_t(gap_commodity_count);
				auto const to_base = size_t(to.market.index()) * size_t(gap_commodity_count);
				auto const loss = std::max(0.f, 1.f - economy::trade_goods_lost_per_km * distance);
				if(!(loss > 0.f))
					return best;
				auto const cut = from.owner == to.owner ? economy::merchant_cut_domestic : economy::merchant_cut_foreign;
				for(uint32_t k = 1; k < gap_commodity_count; ++k) {
					auto const price_from = gap_price[from_base + k];
					auto const price_to = gap_price[to_base + k];
					if(!(price_from > 0.f) || !(price_to > 0.f))
						continue;
					auto const export_price = price_from / gap_confidence[from_base + k];
					auto const import_price = price_to * gap_confidence[to_base + k];
					auto const gap = import_price * loss - export_price * (1.f + cut) - distance * transport_price;
					if(std::isfinite(gap) && gap > best.value) {
						best.found = true;
						best.value = gap;
						best.net_gap = gap;
						best.commodity = dcon::commodity_id{ dcon::commodity_id::value_base_t(k) };
					}
				}
				return best;
			};

			// static_cast, not size_t(...): the parenthesised form is a declarator, so the line
			// would be parsed as a function declaration taking size_t (most vexing parse) and every
			// later best_gap[i] becomes a subscript on a function pointer.
			std::vector<gap_best_route> best_gap(static_cast<std::size_t>(gap_market_count));
			for(size_t i = 0; i < coastal.size(); ++i) {
				for(size_t j = i + 1; j < coastal.size(); ++j) {
					auto const& forward_from = coastal[i];
					auto const& forward_to = coastal[j];
					if(forward_from.market == forward_to.market || sea_route_exists(forward_from.market, forward_to.market))
						continue;
					auto const transport_price = std::max(0.f, state.world.market_get_naval_transportation_price(forward_from.market));
					if(!std::isfinite(transport_price))
						continue;
					auto const direct = province::direct_distance_km(state, forward_from.coast, forward_to.coast) + 1.f;
					if(!(direct > 0.f) || !std::isfinite(direct))
						continue;
					// cheap pre-filter on the straight-line distance, as the opportunity scan does
					if(!evaluate_gap(forward_from, forward_to, direct, transport_price).found
						&& !evaluate_gap(forward_to, forward_from, direct, transport_price).found)
						continue;
					auto const distance = sea_trade_route_distance(state, forward_from.coast, forward_to.coast);
					if(!(distance > 0.f) || !std::isfinite(distance))
						continue;
					auto const forward = evaluate_gap(forward_from, forward_to, distance, transport_price);
					if(forward.found) {
						auto& best = best_gap[size_t(forward_to.market.index())];
						if(forward.value > best.value) {
							best.found = true;
							best.origin = i;
							best.target = j;
							best.value = forward.value;
						}
					}
					auto const backward = evaluate_gap(forward_to, forward_from, distance, transport_price);
					if(backward.found) {
						auto& best = best_gap[size_t(forward_from.market.index())];
						if(backward.value > best.value) {
							best.found = true;
							best.origin = j;
							best.target = i;
							best.value = backward.value;
						}
					}
				}
			}
			for(auto const& best : best_gap) {
				if(!best.found)
					continue;
				auto const& from = coastal[best.origin];
				auto const& to = coastal[best.target];
				auto const new_route = state.world.force_create_trade_route(from.market, to.market);
				state.world.trade_route_set_is_sea_route(new_route, true);
				state.world.trade_route_set_owner(new_route, from.market);
				auto const opposite_route = state.world.force_create_trade_route(to.market, from.market);
				state.world.trade_route_set_is_sea_route(opposite_route, true);
				state.world.trade_route_set_owner(opposite_route, from.market);
			}
		}
	}

	std::vector<float> nation_to_max_population = { };
	nation_to_max_population.resize(state.world.nation_size());

	state.world.for_each_state_instance([&](auto candidate) {
		auto capital = province::state_get_coastal_capital(state, candidate);
		if(capital) {
			auto connected_region = state.world.province_get_connected_coast_id(capital);
			auto owner = state.world.province_get_nation_from_province_ownership(capital);
			auto size = population_of_region[connected_region];
			nation_to_max_population[owner.index()] = std::max(nation_to_max_population[owner.index()], size);
		}
	});

	// connect to each other coastal connectivity components:
	std::vector<parent_link> best_parent;
	std::vector<bool> parent_found;
	parent_found.resize(state.world.market_size());

	state.world.for_each_state_instance([&](auto origin) {
		if(!province::state_is_coastal(state, origin))
			return;
		auto origin_market = state.world.state_instance_get_market_from_local_market(origin);
		auto origin_capital = state.world.state_instance_get_capital(origin);
		auto origin_owner = state.world.state_instance_get_nation_from_state_ownership(origin);
		auto origin_connected_region = state.world.province_get_connected_coast_id(origin_capital);
		auto origin_connected_region_population = population_of_region[origin_connected_region];
		if(origin != capital_of_region[origin_connected_region]) {
			return;
		}

		auto origin_coast = province::state_get_coastal_capital(state, origin);

		parent_found[origin_market.index()] = false;

		bool origin_is_major_node = origin_connected_region_population > 0.7f * nation_to_max_population[origin_owner.index()];

		state.world.for_each_state_instance([&](auto target) {
			if(origin == target) {
				return;
			}
			if(!province::state_is_coastal(state, target))
				return;
			auto target_market = state.world.state_instance_get_market_from_local_market(target);
			auto target_capital = state.world.state_instance_get_capital(target);
			auto target_owner = state.world.state_instance_get_nation_from_state_ownership(target);
			auto target_coast = province::state_get_coastal_capital(state, target);
			auto target_connected_region = state.world.province_get_connected_coast_id(target_coast);
			auto target_connected_region_population = population_of_region[target_connected_region];

			if(target != capital_of_region[target_connected_region]) {
				return;
			}

			if(target_owner != origin_owner) {
				return;
			}

			bool route_already_exists = false;
			state.world.market_for_each_trade_route_as_origin(origin_market, [&](auto route) {
				auto target = state.world.trade_route_get_target(route);
				//auto owner = state.world.trade_route_get_owner(route);
				//auto sea_route = state.world.trade_route_get_is_sea_route(route);
				if(target == target_market) {
					route_already_exists = true;
				}
			});
			if(route_already_exists) {
				return;
			}

			bool target_is_major_node = target_connected_region_population > 0.7f * nation_to_max_population[target_owner.index()];

			if(origin_is_major_node && target_is_major_node) {
				auto new_route_to = state.world.force_create_trade_route(origin_market, target_market);
				auto new_route_from = state.world.force_create_trade_route(target_market, origin_market);
				state.world.trade_route_set_is_sea_route(new_route_to, true);
				state.world.trade_route_set_is_sea_route(new_route_from, true);
				state.world.trade_route_set_owner(new_route_to, origin_market);
				state.world.trade_route_set_owner(new_route_from, origin_market);
				return;
			}

			if(origin_is_major_node) {
				best_parent.push_back({
					target_market,
					origin_market,
					province::direct_distance(state, target_capital, origin_capital)
				});
				return;
			}

			if(target_is_major_node) {
				best_parent.push_back({
					origin_market,
					target_market,
					province::direct_distance(state, target_capital, origin_capital)
				});
				return;
			}
		});
	});

	std::sort(best_parent.begin(), best_parent.end(), [&](parent_link& a, parent_link& b) {
		if(a.dist < b.dist) {
			return true;
		}
		if(a.dist > b.dist) {
			return false;
		}

		return (a.leaf.index() < b.leaf.index());
	});

	for(unsigned i = 0; i < best_parent.size(); i++) {
		if(parent_found[best_parent[i].leaf.index()]) {
			continue;
		}

		parent_found[best_parent[i].leaf.index()] = true;

		auto new_route_to = state.world.force_create_trade_route(best_parent[i].leaf, best_parent[i].parent);
		auto new_route_from = state.world.force_create_trade_route(best_parent[i].parent, best_parent[i].leaf);
		state.world.trade_route_set_is_sea_route(new_route_to, true);
		state.world.trade_route_set_is_sea_route(new_route_from, true);
		state.world.trade_route_set_owner(new_route_to, best_parent[i].parent);
		state.world.trade_route_set_owner(new_route_from, best_parent[i].parent);
	}
}

void generate_initial_trade_routes(sys::state& state) {
	// land trade routes
	state.world.for_each_state_instance([&](auto sid) {
		auto owner = state.world.state_instance_get_nation_from_state_ownership(sid);
		auto market = state.world.state_instance_get_market_from_local_market(sid);
		std::set<dcon::state_instance_id::value_base_t> trade_route_candidates;

		province::for_each_province_in_state_instance(state, sid, [&](auto prov) {
			// try to create trade routes to neighbors
			for(auto adj : state.world.province_get_province_adjacency(prov)) {
				if((adj.get_type() & (province::border::impassible_bit | province::border::coastal_bit)) == 0) {
					auto other =
						adj.get_connected_provinces(0) != prov
						? adj.get_connected_provinces(0)
						: adj.get_connected_provinces(1);

					if(!other.get_state_membership())
						continue;
					if(other.get_state_membership() == sid)
						continue;
					if(trade_route_candidates.contains(other.get_state_membership().id.value))
						continue;

					trade_route_candidates.insert(other.get_state_membership().id.value);
				}
			}

			/*
			Create trade routes through lakes.
			Lake coasts have both impassible bit and coastal bit
			*/
			for(auto adj : state.world.province_get_province_adjacency(prov)) {
				auto bits = adj.get_type();
				if((bits & province::border::impassible_bit) == 0) {
					continue;
				}
				if((bits & province::border::coastal_bit) == 0) {
					continue;
				}

				auto through =
					adj.get_connected_provinces(0) != prov
					? adj.get_connected_provinces(0)
					: adj.get_connected_provinces(1);

				auto area = state.map_state.map_data.province_area_km2[province::to_map_id(through)];
				// larger than the caspian sea
				if(area > 500'000.f) {
					continue;
				}

				for(auto adj2 : state.world.province_get_province_adjacency(through)) {
					auto bits2 = adj2.get_type();
					if((bits2 & province::border::impassible_bit) == 0) {
						continue;
					}
					if((bits2 & province::border::coastal_bit) == 0) {
						continue;
					}
					auto other =
						adj2.get_connected_provinces(0) != through
						? adj2.get_connected_provinces(0)
						: adj2.get_connected_provinces(1);
					if(!other.get_state_membership())
						continue;
					if(other.get_state_membership() == sid)
						continue;
					if(trade_route_candidates.contains(other.get_state_membership().id.value))
						continue;

					trade_route_candidates.insert(other.get_state_membership().id.value);
				}
			}
		});

		for(auto candidate_trade_partner_val : trade_route_candidates) {
			auto si = dcon::state_instance_id{ uint16_t(candidate_trade_partner_val - 1) };
			auto target_market = state.world.state_instance_get_market_from_local_market(si);
			auto new_route = state.world.force_create_trade_route(market, target_market);
			auto opposite_route = state.world.force_create_trade_route(target_market, market);
			state.world.trade_route_set_is_sea_route(new_route, false);
			state.world.trade_route_set_is_sea_route(opposite_route, false);
			state.world.trade_route_set_owner(new_route, market);
			state.world.trade_route_set_owner(opposite_route, market);
		}
	});

	generate_sea_trade_routes(state);

	recalculate_markets_distance(state);
}


}
