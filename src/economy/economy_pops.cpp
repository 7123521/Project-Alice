#include "economy_pops.hpp"
#include "economy_production.hpp"
#include "price.hpp"
#include "province_templates.hpp"
#include "economy_templates.hpp"
#include "demographics.hpp"
#include "money.hpp"
#include "economy_constants.hpp"
#include "economy_templates_pure.hpp"
#include "economy_pops_constants.hpp"
#include "demographics_templates.hpp"
#include "text/text.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>

// LOCAL TRACE (ALICE_TRACE_POP_BUDGET=1): per-day totals of the pops' required basket cost,
// spending, savings and money income, written on the first day of each month. Read-only.
static bool const pop_budget_trace = []() {
	auto env = std::getenv("ALICE_TRACE_POP_BUDGET");
	return env && std::atoi(env) != 0;
}();
static FILE* pop_budget_file = nullptr;
static bool pop_budget_file_init = false;
static double trace_daily_wage_income = 0.0;
static double trace_daily_natural_wage_credit = 0.0;
static double trace_daily_property_income = 0.0;

// LOCAL TRACE (ALICE_TRACE_DEMAND_FORMATION=1): decomposition of the demand registered for one
// probe commodity: base_life x scale_life(market,poptype) x life_mul x life_weight x lf_scale.
static bool const demand_formation_trace = []() {
	auto env = std::getenv("ALICE_TRACE_DEMAND_FORMATION");
	return env && std::atoi(env) != 0;
}();
static FILE* demand_formation_file = nullptr;
static bool demand_formation_file_init = false;

namespace economy {
namespace pops {

static bool const needs_scale_enabled = []() {
	auto e = std::getenv("ALICE_EXP_NEEDS_SCALE");
	return e && std::atoi(e) != 0;
}();
static float const needs_scale_factor = []() {
	auto e = std::getenv("ALICE_NEEDS_SCALE_FACTOR");
	return e ? std::clamp(float(std::atof(e)), 0.0001f, 100.f) : 0.01f;
}();


// LOCAL EXPERIMENT (ALICE_EXP_DIRECT_FACTORY_WAGE=1): credit the direct factory wage bill to the
// factory worker pop type, so producer-bank debit and household credit are the same money.
static bool const direct_factory_wage = []() {
	auto s = std::getenv("ALICE_EXP_DIRECT_FACTORY_WAGE");
	return s && std::atoi(s) != 0;
}();
static bool const financed_wage = []() {
	auto s = std::getenv("ALICE_EXP_FINANCED_WAGE");
	return s && std::atoi(s) != 0;
}();
// LOCAL EXPERIMENT (ALICE_EXP_PROFIT_TO_HOUSEHOLDS=1): include the whole provincial population
// in the rent/rgo/factory token pools, so producer profits/rents reach households, not only
// capitalists/aristocrats. Default 0 keeps upstream.
static bool const profit_to_households = []() {
	auto s = std::getenv("ALICE_EXP_PROFIT_TO_HOUSEHOLDS");
	return s && std::atoi(s) != 0;
}();

static std::vector<float> market_life_spending;
static std::vector<float> market_disposable;
static std::vector<float> pop_income_before;
static std::vector<float> market_income_flow;
static std::vector<float> market_poptype_income_flow;
static int market_poptype_stride = 0;
static std::vector<float> market_poptype_wage;
static int market_poptype_wage_stride = 0;
static std::vector<float> market_poptype_budget;
static int market_poptype_budget_stride = 0;
static bool const les_actual_budget = []() {
	auto s = std::getenv("ALICE_EXP_DEMAND_LES_ACTUAL");
	return s && std::atoi(s) != 0;
}();
static bool const les_income_budget = []() {
	auto s = std::getenv("ALICE_EXP_DEMAND_LES_INCOME");
	return s && std::atoi(s) != 0;
}();
static bool const trace_pop_demand = []() {
	auto s = std::getenv("ALICE_TRACE_POP_DEMAND");
	return s && std::atoi(s) != 0;
}();

float market_life_needs_spending(dcon::market_id m) {
	return m.index() < market_life_spending.size() ? market_life_spending[m.index()] : 0.f;
}
float market_disposable_income(dcon::market_id m) {
	return m.index() < market_disposable.size() ? market_disposable[m.index()] : 0.f;
}

void snapshot_income_start(sys::state& state) {
	pop_income_before.assign(state.world.pop_size(), 0.f);
	state.world.for_each_pop([&](dcon::pop_id p) {
		pop_income_before[p.index()] = state.world.pop_get_savings(p);
	});
}

void finalize_income_flow(sys::state& state) {
	market_income_flow.assign(state.world.market_size(), 0.f);
	market_poptype_stride = int(state.world.pop_type_size());
	market_poptype_income_flow.assign(size_t(state.world.market_size()) * size_t(market_poptype_stride), 0.f);
	state.world.for_each_pop([&](dcon::pop_id p) {
		auto const before = p.index() < pop_income_before.size() ? pop_income_before[p.index()] : 0.f;
		auto const flow = std::max(0.f, state.world.pop_get_savings(p) - before);
		auto const prov = state.world.pop_get_province_from_pop_location(p);
		auto const sid = state.world.province_get_state_membership(prov);
		auto const mid = state.world.state_instance_get_market_from_local_market(sid);
		auto const pt = state.world.pop_get_poptype(p);
		if(mid.index() < market_income_flow.size()) {
			market_income_flow[mid.index()] += flow;
			auto const idx = size_t(mid.index()) * size_t(state.world.pop_type_size()) + size_t(pt.id.index());
			if(idx < market_poptype_income_flow.size())
				market_poptype_income_flow[idx] += flow;
		}
	});
}

float market_poptype_income(dcon::market_id m, dcon::pop_type_id pt) {
	if(market_poptype_stride <= 0)
		return 0.f;
	auto const idx = size_t(m.index()) * size_t(market_poptype_stride) + size_t(pt.index());
	return idx < market_poptype_income_flow.size() ? market_poptype_income_flow[idx] : 0.f;
}

float market_poptype_wage_income(dcon::market_id m, dcon::pop_type_id pt) {
	if(market_poptype_wage_stride <= 0)
		return 0.f;
	auto const idx = size_t(m.index()) * size_t(market_poptype_wage_stride) + size_t(pt.index());
	return idx < market_poptype_wage.size() ? market_poptype_wage[idx] : 0.f;
}

float market_poptype_life_budget(dcon::market_id m, dcon::pop_type_id pt) {
	if(market_poptype_budget_stride <= 0)
		return 0.f;
	auto const idx = size_t(m.index()) * size_t(market_poptype_budget_stride) + size_t(pt.index());
	return idx < market_poptype_budget.size() ? market_poptype_budget[idx] : 0.f;
}

float market_daily_income(dcon::market_id m) {
	return m.index() < market_income_flow.size() ? market_income_flow[m.index()] : 0.f;
}

// LOCAL TRACE (ALICE_TRACE_DEMAND_DECOMP): read-only grain demand decomposition by market and
// pop type on a single probe date. No economic effect.
static bool const demand_decomp_trace = []() {
	auto s = std::getenv("ALICE_TRACE_DEMAND_DECOMP");
	return s && std::atoi(s) != 0;
}();
static std::string const demand_decomp_date = []() {
	auto s = std::getenv("ALICE_DEMAND_PROBE_DATE");
	return s ? std::string(s) : std::string{};
}();
static FILE* demand_decomp_file = nullptr;
static bool demand_decomp_file_init = false;
static dcon::commodity_id demand_decomp_grain{};
static bool demand_decomp_grain_init = false;
static FILE* demand_category_file = nullptr;
static bool demand_category_file_init = false;

static void init_demand_decomp_grain(sys::state const& state) {
	if(demand_decomp_grain_init)
		return;
	demand_decomp_grain_init = true;
	state.world.for_each_commodity([&](dcon::commodity_id c) {
		if(demand_decomp_grain || c == economy::money)
			return;
		auto const name = text::produce_simple_string(state, state.world.commodity_get_name(c));
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
		if(lower.find("grain") != std::string::npos)
			demand_decomp_grain = c;
	});
}


template<typename VALUE, typename POPS>
VALUE investment_rate(const sys::state& state, POPS ids) {
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);
	auto nation_rules = state.world.nation_get_combined_issue_rules(nations);
	auto allows_investment_mask = (nation_rules & can_invest) != 0;
	auto nation_allows_investment = allows_investment_mask;

	auto capitalists_mask = pop_type == state.culture_definitions.capitalists;
	auto middle_class_investors_mask = pop_type == state.culture_definitions.artisans || pop_type == state.culture_definitions.secondary_factory_worker;
	auto farmers_mask = pop_type == state.culture_definitions.farmers;
	auto landowners_mask = pop_type == state.culture_definitions.aristocrat;

	auto invest_ratio_capitalists = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::capitalist_reinvestment);
	auto invest_ratio_landowners = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::aristocrat_reinvestment);
	auto invest_ratio_middle_class = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::middle_class_reinvestment);
	auto invest_ratio_farmers = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::farmers_reinvestment);

	auto investment_ratio =
		adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && capitalists_mask,
			invest_ratio_capitalists + state.defines.alice_invest_capitalist,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && landowners_mask,
			invest_ratio_landowners + state.defines.alice_invest_aristocrat,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && middle_class_investors_mask,
			invest_ratio_middle_class + state.defines.alice_invest_middle_class,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && farmers_mask,
			invest_ratio_farmers + state.defines.alice_invest_farmer,
			0.0f
		);
	return investment_ratio;
}


// handle bank savings
// Note that farmers and middle_class don't do bank savings by default
// - that doens't mean they don't have savings.
// They don't use banks for savings without modifier (from tech, from example).
template<typename VALUE, typename POPS>
VALUE bank_saving_rate(const sys::state& state, POPS ids) {
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);

	auto capitalists_mask = pop_type == state.culture_definitions.capitalists;
	auto middle_class_investors_mask = pop_type == state.culture_definitions.artisans || pop_type == state.culture_definitions.secondary_factory_worker;
	auto farmers_mask = pop_type == state.culture_definitions.farmers;
	auto landowners_mask = pop_type == state.culture_definitions.aristocrat;

	auto bank_saving_ratio_capitalists = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::capitalist_savings);
	auto bank_saving_ratio_landowners = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::aristocrat_savings);
	auto bank_saving_ratio_middle_class = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::middle_class_savings);
	auto bank_saving_ratio_farmers = state.world.nation_get_modifier_values(nations, sys::national_mod_offsets::farmers_savings);

	auto bank_saving_ratio =
		adaptive_ve::select<BOOL_VALUE, VALUE>(
			capitalists_mask,
			bank_saving_ratio_capitalists + state.defines.alice_save_capitalist,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			landowners_mask,
			bank_saving_ratio_landowners + state.defines.alice_save_aristocrat,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			middle_class_investors_mask,
			bank_saving_ratio_middle_class + state.defines.alice_save_middle_class,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			farmers_mask,
			bank_saving_ratio_farmers + state.defines.alice_save_farmer,
			0.0f
		);

	return bank_saving_ratio;
}

template<typename VALUE, typename POPS>
VALUE adjusted_subsistence_score(
	const sys::state& state,
	POPS p
) {
	return state.world.province_get_subsistence_score(p)
		* state.world.province_get_subsistence_employment(p)
		/ (state.world.province_get_demographics(p, demographics::total) + 1.f);
}

template<typename POPS>
auto prepare_pop_budget_templated(
	const sys::state& state, POPS ids
) {
	using VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, float, ve::fp_vector>;
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	vectorized_pops_budget<VALUE> result{ };

	auto pop_size = state.world.pop_get_size(ids);
	// LOCAL FIX (not upstream): spending ratios divide by savings; a negative or -epsilon
	// balance would flip every ratio negative. Clamp the read only.
	auto savings = adaptive_ve::max<VALUE>(0.f, state.world.pop_get_savings(ids));
	// LOCAL DIAGNOSTIC (ALICE_NAN_WATCH=1): negative pop savings make every spending ratio and
	// demand negative; report the first offending pop.
	{
		static bool const pops_nan_watch = []() {
			auto env = std::getenv("ALICE_NAN_WATCH");
			return env && std::atoi(env) != 0;
		}();
		if(pops_nan_watch) {
			ve::apply([&](dcon::pop_id p, float sv, float sz) {
				if(sv < -1e-3f) {
					auto pt = state.world.pop_get_poptype(p);
					std::fprintf(stderr, "NANWATCH negative_savings pop=%u poptype=%u size=%g savings=%g\n",
						p.index(), pt.id.index(), sz, sv);
					std::fflush(stderr);
					std::abort();
				}
			}, ids, savings, pop_size);
		}
	}
	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);
	auto strata = state.world.pop_type_get_strata(pop_type);

	VALUE life_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return state.world.market_get_life_needs_costs(m, pt);
		}, markets, pop_type
	);
	VALUE everyday_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return state.world.market_get_everyday_needs_costs(m, pt);
		}, markets, pop_type
	);
	VALUE luxury_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return state.world.market_get_luxury_needs_costs(m, pt);
		}, markets, pop_type
	);

	if constexpr(std::same_as<POPS, dcon::pop_id>) {
		result.can_use_free_services = state.world.pop_get_is_primary_or_accepted_culture(ids) ? 1.f : 0.f;
	} else {
		result.can_use_free_services = adaptive_ve::select<BOOL_VALUE, VALUE>(state.world.pop_get_is_primary_or_accepted_culture(ids), ve::fp_vector{ 1.f }, ve::fp_vector{ 0.f });
	}

	// we want to focus on life needs first if we are poor AND our satisfaction is low

	VALUE total_cost_needs = 0.00001f + (life_costs + everyday_costs + luxury_costs) * pop_size / state.defines.alice_needs_scaling_factor;
	VALUE is_rich = adaptive_ve::min<VALUE>(
		3.f,
		adaptive_ve::max<VALUE>(0.f, savings - total_cost_needs) / total_cost_needs
	);

	VALUE base_life_costs = (economy::price_properties::commodity::min + life_costs * pop_size / state.defines.alice_needs_scaling_factor);
	VALUE base_everyday_costs = (economy::price_properties::commodity::min + everyday_costs * pop_size / state.defines.alice_needs_scaling_factor);
	VALUE base_luxury_costs = (economy::price_properties::commodity::min + luxury_costs * pop_size / state.defines.alice_needs_scaling_factor);

	VALUE is_poor = adaptive_ve::max<VALUE>(0.01f, 1.f - savings / base_life_costs);
	//VALUE current_life = pop_demographics::get_life_needs(state, ids);
	is_poor = adaptive_ve::min<VALUE>(1.f, adaptive_ve::max<VALUE>(0.f, is_poor));

	VALUE savings_positive = (savings + economy::price_properties::commodity::min);
	VALUE savings_divisor = 1.f / savings_positive;

	// prepare desired spending rate for every category

	VALUE life_satisfaction_per_pound = 1.f / (life_costs + economy::price_properties::commodity::min);
	VALUE everyday_satisfaction_per_pound = 1.f / (everyday_costs + economy::price_properties::commodity::min);
	VALUE luxury_satisfaction_per_pound = 1.f / (luxury_costs + economy::price_properties::commodity::min);


	VALUE life_satisfaction_priority= adaptive_ve::max<VALUE>(0.f, life_satisfaction_per_pound * savings_positive - 0.01f);
	VALUE everyday_satisfaction_priority = adaptive_ve::max<VALUE>(0.f, everyday_satisfaction_per_pound * savings_positive - 0.01f);
	VALUE luxury_satisfaction_priority = adaptive_ve::max<VALUE>(0.f, luxury_satisfaction_per_pound * savings_positive - 0.01f);

	VALUE total_score = life_satisfaction_priority + everyday_satisfaction_priority + luxury_satisfaction_priority + 0.01f;

	VALUE lf_spend = adaptive_ve::min<VALUE>(10.f * base_life_costs * savings_divisor, life_satisfaction_priority / total_score);
	VALUE ev_spend = adaptive_ve::min<VALUE>(10.f * base_everyday_costs * savings_divisor, everyday_satisfaction_priority / total_score);
	VALUE lx_spend = adaptive_ve::min<VALUE>(10.f * base_luxury_costs * savings_divisor, luxury_satisfaction_priority / total_score);

	VALUE life_spending_ratio = lf_spend * (1.f - is_poor) + is_poor;
	VALUE housing_spending_ratio = 0.3f;
	VALUE everyday_spending_ratio = ev_spend * (1.f - is_poor);
	VALUE luxury_spending_ratio = lx_spend * (1.f - is_poor);

	VALUE categories_spending_ratio = 0.0f;
	VALUE education_spending_ratio = (0.2f) * (1.f - is_poor);
	VALUE investment_ratio = adaptive_ve::max<VALUE>(investment_rate<VALUE>(state, ids), 0.0f);
	VALUE banking_ratio = adaptive_ve::max<VALUE>(bank_saving_rate<VALUE>(state, ids), 0.0f);

	if(state.world.consumption_category_size() != 0) {
		life_spending_ratio = 0.f;
		everyday_spending_ratio = 0.f;
		luxury_spending_ratio = 0.f;
		categories_spending_ratio = 0.5f;
	}

	VALUE total_spending_ratio =
		life_spending_ratio
		+ housing_spending_ratio
		+ everyday_spending_ratio
		+ luxury_spending_ratio
		+ education_spending_ratio
		+ investment_ratio
		+ banking_ratio;

	if constexpr(std::same_as<POPS, dcon::pop_id>) {
		total_spending_ratio = total_spending_ratio < 1.f ? 1.f : total_spending_ratio;
	} else {
		total_spending_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(total_spending_ratio < 1.f, ve::fp_vector{ 1.f }, total_spending_ratio);
	}

	// normalize:

	life_spending_ratio = life_spending_ratio / total_spending_ratio;
	housing_spending_ratio = housing_spending_ratio / total_spending_ratio;
	everyday_spending_ratio = everyday_spending_ratio / total_spending_ratio;
	luxury_spending_ratio = luxury_spending_ratio / total_spending_ratio;
	education_spending_ratio = education_spending_ratio / total_spending_ratio;
	investment_ratio = investment_ratio / total_spending_ratio;
	banking_ratio = banking_ratio / total_spending_ratio;

	// set actual budgets

	VALUE spend_on_categories = categories_spending_ratio * savings;
	VALUE spend_on_life_needs = life_spending_ratio * savings;
	VALUE spend_on_housing = housing_spending_ratio * savings;
	VALUE spend_on_everyday_needs = everyday_spending_ratio * savings;
	VALUE spend_on_luxury_needs = luxury_spending_ratio * savings;
	VALUE spend_on_education = education_spending_ratio * savings;
	VALUE spend_on_investments = investment_ratio * savings;
	VALUE spend_on_bank_savings = banking_ratio * savings;

	// upload data to structure

	if(state.world.consumption_category_size() != 0) {
		// all consumption goes first in this model
		state.world.for_each_consumption_category([&](auto cat) {
			auto base_cost = state.world.market_get_cost_per_consumption_category(markets, cat);
			auto cost = base_cost * pop_size / state.defines.alice_needs_scaling_factor;
			auto can_spend = adaptive_ve::min<VALUE>(2.f * cost, spend_on_categories);
			auto depends_on = state.world.consumption_category_get_buy_after(cat).id;
			auto scales_with = state.world.consumption_category_get_scale_with(cat).id;
			auto scale = ve::fp_vector{1.f};

			// avoid spending when we depend on not fulfilled category
			if(depends_on && depends_on.index() < (int)result.per_consumption_category.size()) {
				auto base_scale = result.per_consumption_category[depends_on.index()].satisfied_with_money_ratio;
				base_scale = adaptive_ve::min<VALUE>(base_scale , 1.f);
				if(scales_with) {
					auto weight = state.world.market_get_local_consumption_weights(markets, scales_with.index() + depends_on.index() * state.world.commodity_size());
					// for example, if we don't buy cars for transportation, don't buy fuel
					can_spend = can_spend * base_scale * weight;
					scale = base_scale * weight;
				} else {
					// for example, avoid luxury when starving;
					can_spend = can_spend * base_scale * base_scale;
					scale = base_scale * base_scale;
				}
			}

#ifndef NDEBUG
			ve::apply(
				[](float amount) {
					assert(std::isfinite(amount) && amount >= 0.f);
				}, can_spend
			);
#endif

			result.per_consumption_category.emplace_back(vectorized_budget_position<VALUE> {
				.required = cost,
				.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(cost == 0.f, 10.f, can_spend / cost),
				.satisfied_for_free_ratio = 0.f,
				.spent = can_spend,
				.demand_scale = 1.f
			});
			spend_on_categories = spend_on_categories - can_spend;
			savings = savings - can_spend;
			result.spent_total = result.spent_total + can_spend;
		});
	}

	VALUE satisfaction = state.world.pop_get_satisfaction(ids);

	// ##########
	// life needs
	// ##########

	VALUE subsistence = adjusted_subsistence_score<VALUE, decltype(provs)>(state, provs);
	BOOL_VALUE rgo_worker = state.world.pop_type_get_is_paid_rgo_worker(pop_type);
	subsistence = adaptive_ve::select<BOOL_VALUE, VALUE>(rgo_worker, subsistence, 0.f);
	VALUE available_subsistence = adaptive_ve::min<VALUE>(subsistence_score_life, subsistence);
	subsistence = subsistence - available_subsistence;
	VALUE qol_from_subsistence = available_subsistence / subsistence_score_life;
	VALUE demand_scale_life = satisfaction;//old_life / base_qol;
	result.life_needs.demand_scale = demand_scale_life;// * demand_scale_life + 0.01f;
	result.life_needs.required =
		result.life_needs.demand_scale
		* life_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_life_costs = result.life_needs.required == 0;
	/*
	auto rich_but_life_needs_are_not_satisfied = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_life_costs,
		1.f,
		adaptive_ve::min<VALUE>
			(
				2.f,
				adaptive_ve::max<VALUE>(0.f, spend_on_life_needs - result.life_needs.required * 5.f) / result.life_needs.required
			)
	);
	*/
	result.life_needs.spent = adaptive_ve::min<VALUE>(spend_on_life_needs, result.life_needs.required * (1.f + is_rich));
	result.life_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_life_costs,
		10.f,
		result.life_needs.spent
		/ result.life_needs.required
	);
	// subsistence gives free "level of consumption"
	result.life_needs.satisfied_for_free_ratio = qol_from_subsistence / (1.f + result.life_needs.demand_scale);
	result.spent_total = result.spent_total + result.life_needs.spent;
	savings = savings - result.life_needs.spent;


	// ##############
	// housing
	// ##############
	auto housing_price = state.world.province_get_service_price(provs, services::list::urban_housing);
	auto demand_scale = 1.f;
	result.housing.demand_scale = 1.f;
	result.housing.required = pop_size * housing_price;
	//result.housing.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_housing, result.housing.required));
	result.housing.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(result.housing.required * 1.5f, spend_on_housing));
	result.housing.satisfied_for_free_ratio = 0.f;
	result.housing.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		pop_size == 0.f,
		1.f,
		result.housing.spent / result.housing.required
	);
	result.spent_total = result.spent_total + result.housing.spent;
	savings = savings - result.housing.spent;


	// ##############
	// everyday needs
	// ##############

	auto old_everyday = pop_demographics::get_everyday_needs(state, ids);
	auto demand_scale_everyday = 1.f;//old_everyday / base_qol;
	result.everyday_needs.demand_scale = demand_scale_everyday;// * demand_scale_everyday + 0.01f;
	result.everyday_needs.required =
		result.everyday_needs.demand_scale
		* everyday_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_everyday_costs = result.everyday_needs.required == 0;
	auto rich_but_everyday_needs_are_not_satisfied = 0.f;
	/*
	adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_everyday_costs,
		1.f,
		adaptive_ve::min<VALUE>
		(
			5.f,
			adaptive_ve::max<VALUE>(0.f, spend_on_everyday_needs - result.everyday_needs.required * 5.f) / result.everyday_needs.required
		)
	);
	*/
	result.everyday_needs.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_everyday_needs, result.everyday_needs.required * (1.f + is_rich)));
	result.everyday_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_everyday_costs,
		10.f,
		result.everyday_needs.spent
		/ result.everyday_needs.required
	);
	result.everyday_needs.satisfied_for_free_ratio = 0.f;
	result.spent_total = result.spent_total + result.everyday_needs.spent;
	savings = savings - result.everyday_needs.spent;



	// ############
	// luxury needs
	// ############

	auto old_luxury = pop_demographics::get_luxury_needs(state, ids);
	auto demand_scale_luxury = 1.f;//old_luxury / base_qol;
	result.luxury_needs.demand_scale = demand_scale_luxury;// * demand_scale_luxury + 0.01f;
	result.luxury_needs.required =
		result.luxury_needs.demand_scale
		* luxury_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_luxury_costs = result.luxury_needs.required == 0;
	auto rich_but_luxury_needs_are_not_satisfied = 0.f;
	/*
	adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_luxury_costs,
		1.f,
		adaptive_ve::min<VALUE>
		(
			5.f,
			adaptive_ve::max<VALUE>(0.f, spend_on_luxury_needs - result.luxury_needs.required * 5.f) / result.luxury_needs.required
		)
	);
	*/
	result.luxury_needs.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_luxury_needs, result.luxury_needs.required * (1.f + is_rich)));
	result.luxury_needs.satisfied_for_free_ratio = 0.f;
	result.luxury_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_luxury_costs,
		10.f,
		result.luxury_needs.spent
		/ result.luxury_needs.required
	);
	result.spent_total = result.spent_total + result.luxury_needs.spent;
	savings = savings - result.luxury_needs.spent;



	// #########
	// education
	// #########

	auto education_price = state.world.province_get_service_price(provs, services::list::education);

	auto literacy = pop_demographics::get_literacy(state, ids);
	result.education.demand_scale = literacy * literacy / 0.5f + 0.1f;
	auto required_education = result.education.demand_scale * pop_size;
	result.education.required = required_education * education_price;

	// if education is crazy expensive and impossible to access, we want to spend 0 because it's hopeless

	auto scale_from_being_rich = adaptive_ve::select<BOOL_VALUE, VALUE>(
		required_education == 0.f,
		1.f,
		adaptive_ve::max<VALUE>
		(
			0.f,
			adaptive_ve::min<VALUE>
			(
				10.f,
				adaptive_ve::max<VALUE>(0.f, spend_on_education - result.education.required * 5.f)
				/ result.education.required
			) - 0.1f
		)
	);

	auto education_scale_nation = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, 1.f, 0.f);

	auto personal_desired_spending = result.education.required * scale_from_being_rich;
	auto can_actually_spend = adaptive_ve::min<VALUE>(savings, spend_on_education);
	auto total_personal_spending = adaptive_ve::min<VALUE>(can_actually_spend, personal_desired_spending);
	auto education_scale_private = scale_from_being_rich * adaptive_ve::select<BOOL_VALUE, VALUE>(personal_desired_spending > 0.f, total_personal_spending / personal_desired_spending, 0.f);

	//auto probability_to_get_education_for_free = state.world.province_get_service_satisfaction_for_free(provs, services::list::education);
	auto expected_help_from_nation = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, result.education.required, 0.f);
	auto total_expected_spending = expected_help_from_nation + total_personal_spending;

	auto potentially_free_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(total_expected_spending > 0.f, expected_help_from_nation / total_expected_spending, 0.f);

	//auto supposed_to_spend = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_education, result.education.required * rich_but_uneducated));
	//auto potentially_free_ratio = expected_help_from_nation / adaptive_ve::max<VALUE>(1.f, rich_but_uneducated);
	//auto ratio_of_free_education = decltype(potentially_free_ratio)(0.f);
	//ratio_of_free_education = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, potentially_free_ratio, ratio_of_free_education);

	result.education.satisfied_for_free_ratio = education_scale_nation;
	result.education.spent = total_personal_spending;
	result.education.satisfied_with_money_ratio = education_scale_private;
	result.spent_total = result.spent_total + result.education.spent;
	savings = savings - result.education.spent;



	// ###########
	// investments
	// ###########

	result.investments.required = 0.f;
	result.investments.satisfied_with_money_ratio = 1.f;
	result.investments.satisfied_for_free_ratio = 0.f;
	result.investments.spent = spend_on_investments;
	result.investments.demand_scale = 0.f;
	result.spent_total = result.spent_total + result.investments.spent;
	savings = savings - result.investments.spent;

	// #####
	// banks
	// #####

	result.bank_savings.required = 0.f;
	result.bank_savings.satisfied_with_money_ratio = 1.f;
	result.bank_savings.satisfied_for_free_ratio = 0.f;
	result.bank_savings.spent = spend_on_bank_savings;
	result.bank_savings.demand_scale = 0.f;
	result.spent_total = result.spent_total + result.bank_savings.spent;
	savings = savings - result.bank_savings.spent;

	result.remaining_savings = savings;

	return result;
}


void update_consumption(
	sys::state& state,
	ve::vectorizable_buffer<float, dcon::nation_id>& invention_count,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_life,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_housing,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_everyday,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_luxury,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_education_private,
	ve::vectorizable_buffer<float, dcon::pop_id>& buffer_education_public,
	ve::vectorizable_buffer<float, dcon::pop_id>& demand_life,
	ve::vectorizable_buffer<float, dcon::pop_id>& demand_housing,
	ve::vectorizable_buffer<float, dcon::pop_id>& demand_everyday,
	ve::vectorizable_buffer<float, dcon::pop_id>& demand_luxury,
	ve::vectorizable_buffer<float, dcon::pop_id>& demand_paid_education,
	ve::vectorizable_buffer<float, dcon::pop_id>& subsistence_ratio,
	std::vector<ve::vectorizable_buffer<float, dcon::pop_id>>& demand_consumption_category
) {
	uint32_t total_commodities = state.world.commodity_size();

	if(demand_decomp_trace)
		init_demand_decomp_grain(state);

	state.ui_state.last_tick_investment_pool_change = 0;

	// satisfaction buffers
	// they store how well pops satisfy their needs
	// we store them per pop now
	// because iteration per state
	// and per pop of each state is way too slow
	// we start with filling them with according subsistence values
	// and then attempt to buy the rest

	ve::fp_vector total_spendings{};

	// temporary buffers for actual pop demand
	auto demand_education_public_allowed = state.world.pop_make_vectorizable_float_buffer();

	auto to_bank = state.world.pop_make_vectorizable_float_buffer();
	auto to_investments = state.world.pop_make_vectorizable_float_buffer();
	auto pop_life_spending = state.world.pop_make_vectorizable_float_buffer();
	auto pop_life_budget = state.world.pop_make_vectorizable_float_buffer();
	auto pop_disposable = state.world.pop_make_vectorizable_float_buffer();
	if(les_actual_budget)
		market_life_spending.assign(state.world.market_size(), 0.f);
	if(les_income_budget)
		market_disposable.assign(state.world.market_size(), 0.f);

	static ve::vectorizable_buffer<float, dcon::pop_id> trace_required(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::pop_id> trace_required_life(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::pop_id> trace_spent(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::pop_id> trace_spent_life(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::pop_id> trace_savings(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::pop_id> trace_ratio_life(uint32_t(1));
	static uint32_t trace_pop_count = 1;
	if(pop_budget_trace && state.world.pop_size() > trace_pop_count) {
		trace_required = state.world.pop_make_vectorizable_float_buffer();
		trace_required_life = state.world.pop_make_vectorizable_float_buffer();
		trace_spent = state.world.pop_make_vectorizable_float_buffer();
		trace_spent_life = state.world.pop_make_vectorizable_float_buffer();
		trace_savings = state.world.pop_make_vectorizable_float_buffer();
		trace_ratio_life = state.world.pop_make_vectorizable_float_buffer();
		trace_pop_count = state.world.pop_size();
	}

	state.world.execute_parallel_over_pop([&](auto ids) {
		auto pop_size = state.world.pop_get_size(ids);

		if(les_income_budget)
			pop_disposable.set(ids, state.world.pop_get_savings(ids));

		// get all data into vectors
		vectorized_pops_budget<ve::fp_vector> data = prepare_pop_budget_templated(state, ids);

		if(pop_budget_trace) {
			trace_required.set(ids,
				data.life_needs.required + data.everyday_needs.required + data.luxury_needs.required);
			trace_required_life.set(ids, data.life_needs.required);
			trace_spent.set(ids, data.spent_total);
			trace_spent_life.set(ids, data.life_needs.spent);
			trace_savings.set(ids, state.world.pop_get_savings(ids));
			trace_ratio_life.set(ids, data.life_needs.satisfied_with_money_ratio);
		}

		// "for free" in the context of life/everyday/luxury needs means subsistence/gifts
		// so we apply it directly there, it's not registered as demand
		// "for free" in the context of education means that someone paid for you
		// so it will be registered as demand

		buffer_life.set(ids, data.life_needs.satisfied_with_money_ratio);
		buffer_housing.set(ids, data.housing.satisfied_with_money_ratio);
		buffer_everyday.set(ids, data.everyday_needs.satisfied_with_money_ratio);
		buffer_luxury.set(ids, data.luxury_needs.satisfied_with_money_ratio);
		buffer_education_private.set(ids, data.education.satisfied_with_money_ratio);
		buffer_education_public.set(ids, data.education.satisfied_for_free_ratio);

		subsistence_ratio.set(ids, data.life_needs.satisfied_for_free_ratio);

		auto multiplier = pop_size / state.defines.alice_needs_scaling_factor;

		demand_life.set(ids, multiplier * data.life_needs.demand_scale * data.life_needs.satisfied_with_money_ratio);
		demand_housing.set(ids, pop_size * data.housing.demand_scale * data.housing.satisfied_with_money_ratio);
		demand_everyday.set(ids, multiplier * data.everyday_needs.demand_scale * data.everyday_needs.satisfied_with_money_ratio);
		demand_luxury.set(ids, multiplier * data.luxury_needs.demand_scale * data.luxury_needs.satisfied_with_money_ratio);
		demand_education_public_allowed.set(
			ids,
			pop_size * data.education.demand_scale
			* data.education.satisfied_for_free_ratio
			* data.can_use_free_services
		);
		demand_paid_education.set(ids, pop_size * data.education.demand_scale * data.education.satisfied_with_money_ratio);

		to_bank.set(ids, data.bank_savings.spent);
		to_investments.set(ids, data.investments.spent);
		pop_life_spending.set(ids, data.life_needs.spent);
		pop_life_budget.set(ids, data.life_needs.required);

		state.world.for_each_consumption_category([&](auto cat) {
			auto scale = data.per_consumption_category[cat.index()].demand_scale;
			auto demanded_ratio = data.per_consumption_category[cat.index()].satisfied_with_money_ratio;
#ifndef NDEBUG
			ve::apply(
				[](float amount) {
			assert(std::isfinite(amount) && amount >= 0.f);
				}, multiplier * scale * demanded_ratio
			);
#endif
			demand_consumption_category[cat.index()].set(
				ids,
				multiplier * scale	* demanded_ratio
			);
		});

		// we do save savings here because a part of education is given for free
		// which leads to some part of wealth not being spent most of the time
		state.world.pop_set_savings(ids, ve::max(0.f, data.remaining_savings));
	});

	if(les_actual_budget || les_income_budget || trace_pop_demand) {
		state.world.execute_serial_over_pop([&](auto p) {
			auto provs = state.world.pop_get_province_from_pop_location(p);
			auto sids = state.world.province_get_state_membership(provs);
			auto mids = state.world.state_instance_get_market_from_local_market(sids);
			if(les_actual_budget) {
				auto amounts = pop_life_spending.get(p);
				ve::apply([&](dcon::market_id m, float v) {
					if(m.index() < market_life_spending.size())
						market_life_spending[m.index()] += v;
				}, mids, amounts);
			}
			if(les_income_budget) {
				auto amounts = pop_disposable.get(p);
				ve::apply([&](dcon::market_id m, float v) {
					if(m.index() < market_disposable.size())
						market_disposable[m.index()] += v;
				}, mids, amounts);
			}
			{
				auto pts = state.world.pop_get_poptype(p);
				auto amounts = pop_life_budget.get(p);
				ve::apply([&](dcon::market_id m, dcon::pop_type_id pt, float v) {
					auto const idx = size_t(m.index()) * size_t(state.world.pop_type_size()) + size_t(pt.index());
					if(market_poptype_budget_stride <= 0) {
						market_poptype_budget_stride = int(state.world.pop_type_size());
						market_poptype_budget.assign(size_t(state.world.market_size()) * size_t(market_poptype_budget_stride), 0.f);
					}
					if(idx < market_poptype_budget.size())
						market_poptype_budget[idx] += v;
				}, mids, pts, amounts);
			}
		});
	}

	// services are at province level:

	concurrency::parallel_for(int32_t(0), state.province_definitions.first_sea_province.index(), [&](uint32_t raw_province_id) {
		auto pid = dcon::province_id{ dcon::province_id::value_base_t(raw_province_id) };
		state.world.province_for_each_pop_location(pid, [&](auto location) {
			auto pop = state.world.pop_location_get_pop(location);

			auto demand_allow_public = demand_education_public_allowed.get(pop);
			auto demand_forbid_public = demand_paid_education.get(pop);

			auto old_allow = state.world.province_get_service_demand_allowed_public_supply(pid, services::list::education);
			auto old_forbid = state.world.province_get_service_demand_forbidden_public_supply(pid, services::list::education);

			state.world.province_set_service_demand_allowed_public_supply(pid, services::list::education, old_allow + demand_allow_public);
			state.world.province_set_service_demand_forbidden_public_supply(pid, services::list::education, old_forbid + demand_forbid_public);

			auto housing_old = state.world.province_get_service_demand_forbidden_public_supply(pid, services::list::urban_housing);
			state.world.province_set_service_demand_forbidden_public_supply(pid, services::list::urban_housing, housing_old + demand_housing.get(pop));
		});
	});

	// commodities (actually groups of commodities) are at market level

	concurrency::parallel_for(uint32_t(0), state.world.market_size(), [&](uint32_t raw_market_id) {
		auto m = dcon::market_id{ dcon::market_id::value_base_t(raw_market_id) };
		if(!state.world.market_is_valid(m)) return;
		auto zone = state.world.market_get_zone_from_local_market(m);
		province::for_each_province_in_state_instance(state, zone, [&](auto pid) {
			state.world.province_for_each_pop_location(pid, [&](auto location) {
				auto pop = state.world.pop_location_get_pop(location);
				auto pop_type = state.world.pop_get_poptype(pop);

				auto life = demand_life.get(pop);
				auto everyday = demand_everyday.get(pop);
				auto luxury = demand_luxury.get(pop);

				auto old_life = state.world.market_get_life_needs_scale(m, pop_type);
				auto old_everyday = state.world.market_get_everyday_needs_scale(m, pop_type);
				auto old_luxury = state.world.market_get_luxury_needs_scale(m, pop_type);

				state.world.market_set_life_needs_scale(m, pop_type, old_life + life);
				state.world.market_set_everyday_needs_scale(m, pop_type, old_everyday + everyday);
				state.world.market_set_luxury_needs_scale(m, pop_type, old_luxury + luxury);

				state.world.for_each_consumption_category([&](auto cat){
					auto old = state.world.market_get_demand_per_consumption_category(m, cat);
					auto to_add = demand_consumption_category[cat.index()].get(pop);
#ifndef NDEBUG
					ve::apply(
						[](float amount) {
								assert(std::isfinite(amount) && amount >= 0.f);
						}, old + to_add
					);
#endif
					state.world.market_set_demand_per_consumption_category(m, cat, old + to_add);
				});
			});
		});
	});

	// investments are at national level:

	concurrency::parallel_for(uint32_t(0), state.world.nation_size(), [&](uint32_t raw_nation_id) {
		auto n = dcon::nation_id{ dcon::nation_id::value_base_t(raw_nation_id) };
		if(!state.world.nation_is_valid(n)) return;
		state.world.nation_for_each_province_ownership(n, [&](auto poid) {
			state.world.province_for_each_pop_location(state.world.province_ownership_get_province(poid), [&](auto location) {
				auto pop = state.world.pop_location_get_pop(location);

				auto investment = to_investments.get(pop);
				auto current_inv = state.world.nation_get_private_investment(n);
				state.world.nation_set_private_investment(n, current_inv + investment);

				if(n == state.local_player_nation) {
					state.ui_state.last_tick_investment_pool_change += investment;
				}

				auto banking = to_bank.get(pop);
				auto current_bank = state.world.nation_get_national_bank(n);
				state.world.nation_set_national_bank(n, current_bank + banking);
			});
		});
	});

	// iterate over all (market, pop type, trade good) triples to finalise this calculation and register demand to actual commodities
	state.world.execute_parallel_over_market([&](auto ids) {
		auto states = state.world.market_get_zone_from_local_market(ids);
		auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
		auto invention_factor = state.defines.invention_impact_on_demand * invention_count.get(nations) + 1.f;

		ve::fp_vector life_mul[3] = {
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::poor_life_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::middle_life_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::rich_life_needs) + 1.0f
		};
		ve::fp_vector everyday_mul[3] = {
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::poor_everyday_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::middle_everyday_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::rich_everyday_needs) + 1.0f
		};
		ve::fp_vector luxury_mul[3] = {
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::poor_luxury_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::middle_luxury_needs) + 1.0f,
			state.world.nation_get_modifier_values(
				nations, sys::national_mod_offsets::rich_luxury_needs) + 1.0f,
		};

		for(const auto t : state.world.in_pop_type) {
			auto scale_life = state.world.market_get_life_needs_scale(ids, t);
			auto scale_everyday = state.world.market_get_everyday_needs_scale(ids, t);
			auto scale_luxury = state.world.market_get_luxury_needs_scale(ids, t);

			auto strata = t.get_strata();

			for(uint32_t i = 1; i < total_commodities; ++i) {
				dcon::commodity_id cid{ dcon::commodity_id::value_base_t(i) };

				auto life_weight =
					state.world.market_get_life_needs_weights(ids, cid);
				auto everyday_weight =
					state.world.market_get_everyday_needs_weights(ids, cid);
				auto luxury_weight =
					state.world.market_get_luxury_needs_weights(ids, cid);
				auto const needs_scale = needs_scale_enabled ? needs_scale_factor : 1.f;
				auto base_life =
					state.world.pop_type_get_life_needs(t, cid) * needs_scale;
				auto base_everyday =
					state.world.pop_type_get_everyday_needs(t, cid) * needs_scale;
				auto base_luxury =
					state.world.pop_type_get_luxury_needs(t, cid) * needs_scale;

				auto demand_life =
					base_life
					* scale_life
					* life_mul[strata]
					* life_weight
					* state.defines.alice_lf_needs_scale;
				auto demand_everyday =
					base_everyday
					* scale_everyday
					* everyday_mul[strata]
					* everyday_weight
					* state.defines.alice_ev_needs_scale
					* invention_factor;
				auto demand_luxury =
					base_luxury
					* scale_luxury
					* luxury_mul[strata]
					* luxury_weight
					* state.defines.alice_lx_needs_scale
					* invention_factor;

				if(demand_decomp_trace && cid == demand_decomp_grain) {
					auto const ymd = state.current_date.to_ymd(state.start_date);
					char datebuf[16];
					std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
					if(demand_decomp_date == datebuf) {
						if(!demand_decomp_file_init) {
							demand_decomp_file_init = true;
							auto const path = std::getenv("ALICE_DEMAND_PROBE_FILE");
							if(path) {
								demand_decomp_file = fopen(path, "w");
								if(demand_decomp_file)
									fprintf(demand_decomp_file, "date,market,owner_index,pop_type,strata,scale_life,base_life,life_weight,life_mul,demand_life,scale_everyday,base_everyday,everyday_weight,demand_everyday,scale_luxury,base_luxury,luxury_weight,demand_luxury,market_demand,market_consumption,price\n");
							}
						}
						if(demand_decomp_file) {
							auto const owners = state.world.state_instance_get_nation_from_state_ownership(states);
							auto const market_demand = state.world.market_get_demand(ids, cid);
							auto const market_consumption = state.world.market_get_consumption(ids, cid);
							auto const price = state.world.market_get_price(ids, cid);
							ve::apply([&](dcon::market_id m, dcon::nation_id n, float sl, float bl, float lw, float lm, float dl,
								float se, float be, float ew, float de, float sx, float bx, float xw, float dx,
								float md, float mc, float pr) {
								fprintf(demand_decomp_file, "%s,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
									datebuf, int(m.index()), int(n.index()), int(t.id.index()), int(strata),
									double(sl), double(bl), double(lw), double(lm), double(dl),
									double(se), double(be), double(ew), double(de),
									double(sx), double(bx), double(xw), double(dx),
									double(md), double(mc), double(pr));
							}, ids, owners, scale_life, base_life, life_weight, life_mul[strata], demand_life,
								scale_everyday, base_everyday, everyday_weight, demand_everyday,
								scale_luxury, base_luxury, luxury_weight, demand_luxury,
								market_demand, market_consumption, price);
						}
					}
				}
				register_demand(state, ids, cid, demand_life);
				register_demand(state, ids, cid, demand_everyday);
				register_demand(state, ids, cid, demand_luxury);
			}
		}
		// do the same for new weights
		state.world.for_each_commodity([&](auto cid) {
			ve::fp_vector total = 0.f;
			state.world.for_each_consumption_category([&](auto consumption) {
				auto index = cid.index() + consumption.index() * state.world.commodity_size();
				auto w = state.world.market_get_local_consumption_weights(ids, index);
				auto scale = state.world.market_get_demand_per_consumption_category(ids, consumption);
				auto base_amount = state.world.consumption_category_get_weights(consumption, cid);
#ifndef NDEBUG
				ve::apply(
					[](float amount) {
						assert(std::isfinite(amount) && amount >= 0.f);
					}, scale* w * base_amount
				);
#endif
				total = total + scale * w * base_amount;
				if(demand_decomp_trace && cid == demand_decomp_grain) {
					auto const ymd = state.current_date.to_ymd(state.start_date);
					char datebuf[16];
					std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
					if(demand_decomp_date == datebuf) {
						if(!demand_category_file_init) {
							demand_category_file_init = true;
							auto const path = std::getenv("ALICE_DEMAND_CATEGORY_FILE");
							if(path) {
								demand_category_file = fopen(path, "w");
								if(demand_category_file)
									fprintf(demand_category_file, "date,market,category,scale,local_weight,base_weight,demand\n");
							}
						}
						if(demand_category_file) {
							auto const demand = scale * w * base_amount;
							ve::apply([&](dcon::market_id m, float sc, float lw, float dmd) {
								fprintf(demand_category_file, "%s,%d,%d,%.6g,%.6g,%.6g,%.6g\n",
									datebuf, int(m.index()), int(consumption.index()),
									double(sc), double(lw), double(base_amount), double(dmd));
							}, ids, scale, w, demand);
						}
					}
				}
#ifndef NDEBUG
				ve::apply(
					[](float amount) {
						assert(std::isfinite(amount) && amount >= 0.f);
					}, total
				);
#endif
			});
			register_demand(state, ids, cid, total);
		});
	});

	if(pop_budget_trace) {
		if(!pop_budget_file_init) {
			pop_budget_file_init = true;
			auto const path = std::getenv("ALICE_POP_BUDGET_FILE");
			if(path)
				pop_budget_file = fopen(path, "w");
			if(pop_budget_file)
				fprintf(pop_budget_file, "date,pops,savings,required,required_life,spent,spent_life,ratio_life,wage_income,property_income,ratio_weighted,share_below25,share_below50,share_below100\n");
		}
		auto const ymd = state.current_date.to_ymd(state.start_date);
		if(pop_budget_file && ymd.day == 1) {
			double req = 0.0, req_life = 0.0, spent = 0.0, spent_life = 0.0, savings = 0.0, ratio = 0.0;
			double ratio_w_num = 0.0, ratio_w_den = 0.0;
			double below25 = 0.0, below50 = 0.0, below75 = 0.0, below100 = 0.0;
			uint32_t count = 0;
			state.world.execute_serial_over_pop([&](auto ids) {
				ve::apply([&](float a, float b, float c, float d, float e, float g) {
					req += double(a);
					req_life += double(b);
					spent += double(c);
					spent_life += double(d);
					savings += double(e);
					ratio += double(g);
					++count;
					if(b > 0.f) {
						ratio_w_num += double(b) * double(g);
						ratio_w_den += double(b);
						if(g < 0.25f) below25 += double(b);
						if(g < 0.50f) below50 += double(b);
						if(g < 0.75f) below75 += double(b);
						if(g < 1.00f) below100 += double(b);
					}
				}, trace_required.get(ids), trace_required_life.get(ids), trace_spent.get(ids),
					trace_spent_life.get(ids), trace_savings.get(ids), trace_ratio_life.get(ids));
			});
			char datebuf[16];
			std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
			fprintf(pop_budget_file, "%s,%u,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
				datebuf, count, savings, req, req_life, spent, spent_life,
				count > 0 ? ratio / double(count) : 0.0,
				trace_daily_wage_income, trace_daily_property_income,
				ratio_w_den > 0.0 ? ratio_w_num / ratio_w_den : 0.0,
				ratio_w_den > 0.0 ? below25 / ratio_w_den : 0.0,
				ratio_w_den > 0.0 ? below50 / ratio_w_den : 0.0,
				ratio_w_den > 0.0 ? below100 / ratio_w_den : 0.0);
			fflush(pop_budget_file);
		}
	}

	if(demand_formation_trace) {
		dcon::commodity_id probe{};
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(!probe && text::produce_simple_string(state, state.world.commodity_get_name(c)) == "Grain")
				probe = c;
		});
		if(probe) {
			if(!demand_formation_file_init) {
				demand_formation_file_init = true;
				auto const path = std::getenv("ALICE_DEMAND_FORMATION_FILE");
				if(path)
					demand_formation_file = fopen(path, "w");
				if(demand_formation_file)
					fprintf(demand_formation_file, "date,market,poptype,scale_life,base_life,life_mul,life_weight,lf_scale,demand_g,market_demand,market_supply\n");
			}
			auto const ymd = state.current_date.to_ymd(state.start_date);
			if(demand_formation_file && ymd.day == 1) {
				char datebuf[16];
				std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
				auto const lf_scale = state.defines.alice_lf_needs_scale;
				state.world.for_each_market([&](dcon::market_id m) {
					auto const zone = state.world.market_get_zone_from_local_market(m);
					auto const nation = state.world.state_instance_get_nation_from_state_ownership(zone);
					double const life_mul[3] = {
						nation ? double(state.world.nation_get_modifier_values(nation, sys::national_mod_offsets::poor_life_needs)) + 1.0 : 1.0,
						nation ? double(state.world.nation_get_modifier_values(nation, sys::national_mod_offsets::middle_life_needs)) + 1.0 : 1.0,
						nation ? double(state.world.nation_get_modifier_values(nation, sys::national_mod_offsets::rich_life_needs)) + 1.0 : 1.0
					};
					auto const weight = double(state.world.market_get_life_needs_weights(m, probe));
					auto const total_demand = double(state.world.market_get_demand(m, probe));
					auto const total_supply = double(state.world.market_get_supply(m, probe));
					double computed = 0.0;
					state.world.for_each_pop_type([&](dcon::pop_type_id t) {
						auto const base_life = double(state.world.pop_type_get_life_needs(t, probe));
						if(base_life <= 0.0)
							return;
						auto const strata = state.world.pop_type_get_strata(t);
						auto const scale_life = double(state.world.market_get_life_needs_scale(m, t));
						auto const demand = base_life * scale_life * life_mul[strata] * weight * double(lf_scale);
						computed += demand;
						fprintf(demand_formation_file, "%s,%u,%u,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
							datebuf, unsigned(m.index()), unsigned(t.index()), scale_life, base_life,
							life_mul[strata], weight, double(lf_scale), demand, total_demand, total_supply);
					});
					(void)computed;
				});
				fflush(demand_formation_file);
			}
		}
	}
}

float estimate_artisan_income(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, float size) {
	auto const artisan_type = state.culture_definitions.artisans;
	auto key = demographics::to_key(state, artisan_type);

	if(ptid != artisan_type) {
		return 0.f;
	}

	auto artisan_profit = state.world.province_get_artisan_profit(pid);
	auto current_bank = state.world.province_get_artisan_bank(pid);
	auto total = artisan_profit + current_bank;
	auto dividents = total > 0.f ? total * 0.1f : 0.f;

	auto num_artisans = state.world.province_get_demographics(pid, key);
	auto per_artisan = num_artisans > 0.f ? dividents / num_artisans : 0.f;
	return size * per_artisan;
}

float estimate_artisan_income(sys::state const& state, dcon::pop_id pop) {
	return estimate_artisan_income(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}

void update_income_artisans(sys::state& state) {
	auto const artisan_type = state.culture_definitions.artisans;
	auto key = demographics::to_key(state, artisan_type);

	state.world.execute_parallel_over_province([&](auto pids) {
		auto artisan_profit = state.world.province_get_artisan_profit(pids);
		auto current_bank = state.world.province_get_artisan_bank(pids);
		state.world.province_set_artisan_bank(pids, current_bank + artisan_profit);
		state.world.province_set_artisan_profit(pids, 0.f);

		auto new_bank = state.world.province_get_artisan_bank(pids);
		auto dividents = ve::select(new_bank > 0.f, new_bank * 0.1f, 0.f);
		state.world.province_set_artisan_bank(pids, new_bank - dividents);

		auto num_artisans = state.world.province_get_demographics(pids, key);
		auto per_artisan = ve::select(num_artisans > 0.f, dividents / num_artisans, 0.f);

		ve::apply([&](auto province, auto payment) {
			for(auto pl : state.world.province_get_pop_location(province)) {
				if(artisan_type == pl.get_pop().get_poptype()) {
					pl.get_pop().set_savings(
						pl.get_pop().get_savings()
						+
						pl.get_pop().get_size()
						* payment
					);
#ifndef NDEBUG
					assert(
						std::isfinite(pl.get_pop().get_savings())
						&& pl.get_pop().get_savings() >= 0
					);
#endif
				}
			}
		}, pids, per_artisan);
	});
}


constexpr inline float national_elite_trade_weight_multiplier = 100000.f;

float estimate_local_trade_income(sys::state const& state, dcon::province_id pid, dcon::market_id mid, dcon::pop_type_id ptid, float size) {
	auto sids = state.world.market_get_zone_from_local_market(mid);
	auto nation = state.world.province_get_nation_from_province_control(pid);
	auto const capis_def = state.culture_definitions.capitalists;
	auto capis_key = demographics::to_key(state, capis_def);
	auto elites = state.world.nation_get_demographics(nation, capis_key);
	auto local_population_province = state.world.province_get_demographics(pid, demographics::total);
	auto local_population_market = state.world.state_instance_get_demographics(sids, demographics::total);
	auto total = local_population_market + elites * national_elite_trade_weight_multiplier * local_population_province / (local_population_market + 1.f);

	if(total == 0.f) {
		return 0.f;
	}

	auto balance = state.world.market_get_stockpile(mid, economy::money);
	auto trade_dividents = balance > 0.f ? balance * trade_dividends_rate : 0.f;

	return size / total * trade_dividents;
}

/*
Currently ignores income from national trade.
*/
float estimate_trade_income(sys::state const& state, dcon::pop_id pop) {
	auto provs = state.world.pop_get_province_from_pop_location(pop);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);

	return estimate_local_trade_income(
		state,
		provs,
		markets,
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}

void update_income_non_labor(sys::state& state) {
	/*

	Sum up tokens of eligible pops.
	Sum up cash.
	Set "price" of token
	Distribute cash according to tokens

	Additional national tokens rules:
		COULD BE TOO CPU EXPENSIVE (PROBABLY DOES NOT WORTH IT): Primary/accepted culture: +1 token
	Capital: +1 token
	100'000 city size: +1 token

	1) Rent income:
		Eligible pops:
			Capitalists
			Aristocrats
		Eligible scopes:
			Local

	2) Trade income:
		Group 1
			Note:
				Represents local people who benefit from trade
			Eligible pops:
				For now EVERYONE (because otherwise cash would be accumulated in a random island without eligible pops)
				Later it would be nice to include "important" pops in the national capital
			Eligible scopes:
				Local
		Group 2 (weight: x100)
			Note:
				Represents national elites which conduct trade
			Eligible pops:
				Capitalists
			Eligible scopes:
				Nation

	3) RGO income:
		Eligible pops:
			Capitalists
			Aristocrats
			RGO workers
		Eligible scopes:
			(TODO) Nation
			Local

	4) Factory income:
		Eligible pops:
			Capitalists
		Eligible scopes:
			(TODO) Nation
			Local
	*/

	static ve::vectorizable_buffer<float, dcon::market_id> market_rent_tokens(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> market_trade_tokens(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> market_rgo_tokens(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> market_factory_tokens(uint32_t(1));

	static ve::vectorizable_buffer<float, dcon::market_id> market_rent_money(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> market_rgo_money(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> market_factory_money(uint32_t(1));

	static ve::vectorizable_buffer<float, dcon::nation_id> nation_trade_tokens(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::nation_id> nation_trade_money(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::nation_id> nation_rgo_tokens(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::nation_id> nation_factory_tokens(uint32_t(1));
	{
		static uint32_t old_count = 1;
		auto new_count = state.world.market_size();
		if(new_count > old_count) {
			market_rent_tokens = state.world.market_make_vectorizable_float_buffer();
			market_trade_tokens = state.world.market_make_vectorizable_float_buffer();
			market_rgo_tokens = state.world.market_make_vectorizable_float_buffer();
			market_factory_tokens = state.world.market_make_vectorizable_float_buffer();

			market_rent_money = state.world.market_make_vectorizable_float_buffer();
			market_rgo_money = state.world.market_make_vectorizable_float_buffer();
			market_factory_money = state.world.market_make_vectorizable_float_buffer();

			old_count = new_count;
		}

		static uint32_t old_count_nation = 1;
		auto new_count_nation = state.world.nation_size();

		if(new_count_nation > old_count_nation) {
			nation_trade_tokens = state.world.nation_make_vectorizable_float_buffer();
			nation_trade_money = state.world.nation_make_vectorizable_float_buffer();
			nation_rgo_tokens = state.world.nation_make_vectorizable_float_buffer();
			nation_factory_tokens = state.world.nation_make_vectorizable_float_buffer();
			old_count_nation = new_count_nation;
		}
	}

	/*
	Clear tokens count
	*/


	auto const capis_def = state.culture_definitions.capitalists;
	auto capis_key = demographics::to_key(state, capis_def);

	state.world.execute_serial_over_market([&](auto mid_vector){
		market_rent_tokens.set(mid_vector, 0.f);
		market_trade_tokens.set(mid_vector, 0.f);
		market_rgo_tokens.set(mid_vector, 0.f);
		market_factory_tokens.set(mid_vector, 0.f);

		market_rent_money.set(mid_vector, 0.f);
		market_rgo_money.set(mid_vector, 0.f);
		market_factory_money.set(mid_vector, 0.f);
	});

	state.world.execute_serial_over_nation([&](auto nation_vector){
		nation_trade_money.set(nation_vector, 0.f);
		nation_trade_tokens.set(nation_vector, state.world.nation_get_demographics(nation_vector, capis_key) * national_elite_trade_weight_multiplier);
	});

	/*

	Calculate tokens and available cash for markets
	Give the cash only when pops are of size at least X to avoid issues with demography values being different from the actual values

	*/

	constexpr float min_registered_token_size = 2.f;

	constexpr float expected_share = production_dividends_rate;

	auto const artisan_def = state.culture_definitions.artisans;
	auto artisan_key = demographics::to_key(state, artisan_def);

	auto const clerks_def = state.culture_definitions.secondary_factory_worker;
	auto clerks_key = demographics::to_key(state, clerks_def);


	auto const aristo_def = state.culture_definitions.aristocrat;
	auto aristo_key = demographics::to_key(state, aristo_def);

	auto const bur_def = state.culture_definitions.bureaucrat;
	auto bur_key = demographics::to_key(state, bur_def);

	province::for_each_market_province_parallel_over_market(state, [&](dcon::market_id mid, dcon::state_instance_id sid, dcon::province_id pid){

		// distribute private teaching profits right now
		auto education_funds = state.world.province_get_advanced_province_building_private_savings(pid, advanced_province_buildings::list::schools_and_universities);
		auto eligible_pops = 0.f;
		state.world.province_for_each_pop_location(pid, [&](auto pop_location){
			auto pop = state.world.pop_location_get_pop(pop_location);
			auto size = state.world.pop_get_size(pop);
			auto pt = state.world.pop_get_poptype(pop);

			if(
				pt == state.culture_definitions.secondary_factory_worker
				|| pt == state.culture_definitions.bureaucrat
				|| pt == state.culture_definitions.clergy
			) {
				eligible_pops += size;
			}
		});

		if(eligible_pops > 0.f && education_funds > 0.f) {
			auto per_pop = education_funds / eligible_pops * expected_share;
			state.world.province_for_each_pop_location(pid, [&](auto pop_location){
				auto pop = state.world.pop_location_get_pop(pop_location);
				auto size = state.world.pop_get_size(pop);
				auto pt = state.world.pop_get_poptype(pop);

				if(
					pt == state.culture_definitions.secondary_factory_worker
					|| pt == state.culture_definitions.bureaucrat
					|| pt == state.culture_definitions.clergy
				) {
					auto income = size * per_pop;
					auto current = state.world.pop_get_savings(pop);

					assert(std::isfinite(income));

					state.world.pop_set_savings(pop, current + income);
				}
			});

			state.world.province_set_advanced_province_building_private_savings(pid, advanced_province_buildings::list::schools_and_universities, education_funds * (1.f - expected_share));
		}



		// TOKENS

		auto artisans = state.world.province_get_demographics(pid, artisan_key);
		auto clerks = state.world.province_get_demographics(pid, clerks_key);
		auto capis = state.world.province_get_demographics(pid, capis_key);
		auto nation = state.world.province_get_nation_from_province_control(pid);
		auto national_trade_elites = state.world.nation_get_demographics(nation, capis_key);
		auto aristo = state.world.province_get_demographics(pid, aristo_key);
		auto bur = state.world.province_get_demographics(pid, bur_key);
		auto total_pop = state.world.province_get_demographics(pid, demographics::total);

		auto total_state_pops = state.world.state_instance_get_demographics(sid, demographics::total);
		auto local_province_weight = total_pop / (total_state_pops + 1.f);

		auto rgo_workers = ve::fp_vector{ 0.f };
		state.world.for_each_pop_type([&](dcon::pop_type_id ptid) {
			if(
				state.world.pop_type_get_is_paid_rgo_worker(ptid)
			) {
				rgo_workers = rgo_workers + state.world.province_get_demographics(pid, demographics::to_key(state, ptid));
			}
		});
		
		{
			auto current = market_rent_tokens.get(mid);
			market_rent_tokens.set(mid, current + aristo + capis + (profit_to_households ? total_pop : ve::fp_vector{ 0.f }));
		}

		{
			auto current = market_trade_tokens.get(mid);
			market_trade_tokens.set(mid, current + total_pop + local_province_weight * nation_trade_tokens.get(nation));
		}

		{
			auto capis_share = state.world.province_get_capitalists_share(pid);
			auto aristo_share = state.world.province_get_landowners_share(pid);
			auto current = market_rgo_tokens.get(mid);
			market_rgo_tokens.set(mid, current + capis * capis_share + aristo * aristo_share + rgo_workers * (1.f - capis_share - aristo_share) + (profit_to_households ? total_pop : ve::fp_vector{ 0.f }));
		}

		{
			auto current = market_factory_tokens.get(mid);
			market_factory_tokens.set(mid, current + capis + (profit_to_households ? total_pop : ve::fp_vector{ 0.f }));
		}

		// MONEY

		{
			auto total = market_rent_money.get(mid);
			auto current_money = state.world.province_get_advanced_province_building_private_savings(pid, advanced_province_buildings::list::local_cities_and_towns);
			if (current_money > 0.f)
				market_rent_money.set(mid, total + current_money);
		}

		{
			auto total = market_rgo_money.get(mid);
			auto current_money = state.world.province_get_rgo_bank(pid);
			if(current_money > 0.f)
				market_rgo_money.set(mid, total + current_money);
		}

		{
			auto total = market_factory_money.get(mid);
			auto current_money = state.world.province_get_factory_bank(pid);
			if(current_money > 0.f)
				market_factory_money.set(mid, total + current_money);
		}
	});


	/*
	Calculate money pool for nation
	*/

	province::for_each_nation_controlled_province_parallel_over_nation(state, [&](dcon::nation_id nation, dcon::province_id province) {
		auto states = state.world.province_get_state_membership(province);
		auto market = state.world.state_instance_get_market_from_local_market(states);
		auto valid_market = market != dcon::market_id{ };
		if(
			!valid_market
		) {
			return;
		}
		auto local_population_province = state.world.province_get_demographics(province, demographics::total);
		auto local_population_market = state.world.state_instance_get_demographics(states, demographics::total);
		auto local_province_weight = local_population_province / (local_population_market + 1.f);
		auto national_elites_weight = nation_trade_tokens.get(nation);
		auto local_weight = market_trade_tokens.get(market);

		auto total_money = state.world.market_get_pop_dividends(market);
		auto validated_money = local_weight > min_registered_token_size ? total_money : 0.f;

		auto current = nation_trade_money.get(nation);
		auto to_add = validated_money * local_province_weight * national_elites_weight / (local_weight + 1.f);
		nation_trade_money.set(nation, current + to_add);
	});

	/*

	Now we know the worth of every token and can distribute money in vectorised way
	Guess there are enough calculations to make it parallel, but it requires benchmarking

	*/

	state.world.execute_parallel_over_pop([&](auto pop_vector) {

		auto pop_type = state.world.pop_get_poptype(pop_vector);
		auto size = state.world.pop_get_size(pop_vector);

		auto province = state.world.pop_get_province_from_pop_location(pop_vector);
		auto nation = state.world.province_get_nation_from_province_ownership(province);
		auto capis_share = state.world.province_get_capitalists_share(province);
		auto aristo_share = state.world.province_get_landowners_share(province);
		auto zone = state.world.province_get_state_membership(province);
		auto market = state.world.state_instance_get_market_from_local_market(zone);

		ve::fp_vector total_income = 0.f;

		auto valid_market = market != dcon::market_id{ };

#ifndef NDEBUG
		ve::fp_vector from_rgo = 0.f;
		ve::fp_vector from_rent = 0.f;
		ve::fp_vector from_market = 0.f;
		ve::fp_vector from_market_national = 0.f;
		ve::fp_vector from_factories = 0.f;
#endif // !NDEBUG

		{
			auto candidates = ve::select(valid_market, market_rent_tokens.get(market), 0.f);
			auto total_money = ve::select(valid_market, market_rent_money.get(market), 0.f);
			auto income = ve::select((profit_to_households || pop_type == aristo_def || pop_type == capis_def) && candidates > min_registered_token_size, total_money / candidates * size, 0.f);
#ifndef NDEBUG
			ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, income);
			from_rent = income * expected_share;
#endif // !NDEBUG
			total_income = total_income + income * expected_share;
		}

		{
			auto candidates = ve::select(valid_market, market_trade_tokens.get(market), 0.f);
			auto total_money = ve::select(valid_market, state.world.market_get_pop_dividends(market), 0.f);
			auto income = ve::select(candidates > min_registered_token_size, total_money / candidates * size, 0.f);
#ifndef NDEBUG
			ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, income);
			from_market = income * expected_share;
#endif // !NDEBUG
			total_income = total_income + income;
		}

		{
			auto candidates = ve::select(valid_market, nation_trade_tokens.get(nation), 0.f);
			auto total_money = ve::select(valid_market, nation_trade_money.get(nation), 0.f);
			auto income = ve::select((pop_type == capis_def) && candidates > min_registered_token_size * national_elite_trade_weight_multiplier && size > 0.f, total_money / candidates * size * national_elite_trade_weight_multiplier, 0.f);
#ifndef NDEBUG
			ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, income);
			from_market_national = income * expected_share;
#endif // !NDEBUG

			total_income = total_income + income;
		}

		{
			auto candidates = ve::select(valid_market, market_rgo_tokens.get(market), 0.f);
			auto total_money = ve::select(valid_market, market_rgo_money.get(market), 0.f);
			auto weight = ve::select(profit_to_households, ve::fp_vector{ 1.f },
				ve::select(pop_type == capis_def, capis_share, 0.f)
				+ ve::select(pop_type == aristo_def, aristo_share, 0.f)
				+ ve::select(state.world.pop_type_get_is_paid_rgo_worker(pop_type), 1.f - capis_share - aristo_share, 0.f));
			auto income = ve::select(candidates > min_registered_token_size, total_money / candidates * size * weight, 0.f);
#ifndef NDEBUG
			ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, income);
			from_rgo = income * expected_share;
#endif // !NDEBUG

			total_income = total_income + income * expected_share;
		}

		{
			auto candidates = ve::select(valid_market, market_factory_tokens.get(market), 0.f);
			auto total_money = ve::select(valid_market, market_factory_money.get(market), 0.f);
			auto income = ve::select((profit_to_households || pop_type == capis_def) && candidates > min_registered_token_size && size > 0.f, total_money / candidates * size, 0.f);
#ifndef NDEBUG
			ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, income);
			from_factories = income * expected_share;
#endif // !NDEBUG
			total_income = total_income + income * expected_share;
		}

		auto initial_savings = state.world.pop_get_savings(pop_vector);
#ifndef NDEBUG
		ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, total_income);
		auto total = from_rgo + from_rent + from_market + from_factories + from_market_national;
#endif // !NDEBUG
		state.world.pop_set_savings(pop_vector, initial_savings + total_income);
		if(pop_budget_trace) {
			ve::apply([&](float v) { trace_daily_property_income += double(v); }, total_income);
		}
	});

	/*

	Reduce values if there were any candidates

	*/

	state.world.execute_serial_over_market([&](auto mid_vector){
		auto area = state.world.market_get_zone_from_local_market(mid_vector);
		auto nation = state.world.state_instance_get_nation_from_state_ownership(area);

		auto trade_tokens = market_trade_tokens.get(mid_vector);
		auto global_trade_tokens = nation_trade_tokens.get(nation);

		auto current = state.world.market_get_pop_dividends(mid_vector);
		state.world.market_set_pop_dividends(mid_vector, ve::select(
			(trade_tokens > min_registered_token_size) || (global_trade_tokens > min_registered_token_size),
			0.f,
			current
		));
	});

	state.world.execute_parallel_over_province([&](auto pid_vector){
		auto zone = state.world.province_get_state_membership(pid_vector);
		auto market = state.world.state_instance_get_market_from_local_market(zone);
		auto valid_market = market != dcon::market_id{ };

		{
			auto current_money = state.world.province_get_advanced_province_building_private_savings(pid_vector, advanced_province_buildings::list::local_cities_and_towns);
			state.world.province_set_advanced_province_building_private_savings(
				pid_vector,
				advanced_province_buildings::list::local_cities_and_towns,
				ve::select(valid_market && market_rent_tokens.get(market) > min_registered_token_size && current_money > 0.f, current_money* (1.f - expected_share), current_money)
			);
		}
		{
			auto current_money = state.world.province_get_rgo_bank(pid_vector);
			state.world.province_set_rgo_bank(
				pid_vector,
				ve::select(valid_market && market_rgo_tokens.get(market) > min_registered_token_size && current_money > 0.f, current_money* (1.f - expected_share), current_money)
			);
		}
		{
			auto current_money = state.world.province_get_factory_bank(pid_vector);
			state.world.province_set_factory_bank(
				pid_vector,
				ve::select(valid_market && market_factory_tokens.get(market) > min_registered_token_size && current_money > 0.f, current_money* (1.f - expected_share), current_money)
			);
		}
	});
}


money_from_nation estimate_income_from_nation(sys::state const& state, dcon::pop_id pop) {
	auto capitalists_key = demographics::to_key(state, state.culture_definitions.capitalists);
	auto aristocracy_key = demographics::to_key(state, state.culture_definitions.aristocrat);

	auto prov = state.world.pop_get_province_from_pop_location(pop);
	auto owner = state.world.province_get_nation_from_province_ownership(prov);
	auto population = state.world.nation_get_demographics(owner, demographics::total);
	auto unemployed = population - state.world.nation_get_demographics(owner, demographics::employed);
	auto capitalists = state.world.nation_get_demographics(owner, capitalists_key);
	auto aristocrats = state.world.nation_get_demographics(owner, aristocracy_key);
	auto investors = capitalists + aristocrats;

	auto states = state.world.province_get_state_membership(prov);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto owner_spending = state.world.nation_get_spending_level(owner);

	auto size = state.world.pop_get_size(pop);
	auto adj_size = size / state.defines.alice_needs_scaling_factor;

	auto budget = state.world.nation_get_last_base_budget(owner);

	auto social_budget =
		owner_spending
		* budget
		* float(state.world.nation_get_social_spending(owner))
		/ 100.f;

	auto investment_budget =
		owner_spending
		* budget
		* float(state.world.nation_get_domestic_investment_spending(owner))
		/ 100.f;

	auto const p_level = state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::pension_level);
	auto const unemp_level = state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::unemployment_benefit);

	auto pension_ratio = p_level * population > 0.f ? p_level * population / (p_level * population + unemp_level * unemployed) : 0.f;
	auto unemployment_ratio = unemp_level * unemployed > 0.f ? unemp_level * unemployed / (p_level * population + unemp_level * unemployed) : 0.f;

	auto const pension_per_person =
		pension_ratio
		* social_budget
		/ (population + 1.f);

	auto const benefits_per_person =
		unemployment_ratio
		* social_budget
		/ (unemployed + 1.f);

	auto const payment_per_investor =
		investment_budget
		/ (investors + 1.f);

	auto const m_spending = owner_spending * float(state.world.nation_get_military_spending(owner)) / 100.0f;

	auto types = state.world.pop_get_poptype(pop);

	auto ln_types = state.world.pop_type_get_life_needs_income_type(types);
	auto en_types = state.world.pop_type_get_everyday_needs_income_type(types);
	auto lx_types = state.world.pop_type_get_luxury_needs_income_type(types);

	auto ln_costs = state.world.market_get_life_needs_costs(markets, types);
	auto en_costs = state.world.market_get_everyday_needs_costs(markets, types);
	auto lx_costs = state.world.market_get_luxury_needs_costs(markets, types);

	auto total_costs = ln_costs + en_costs + lx_costs;

	auto is_military_requires_life_needs = ln_types == int32_t(culture::income_type::military);
	auto is_military_requires_everyday_needs = en_types == int32_t(culture::income_type::military);
	auto is_military_requires_luxury_needs = lx_types == int32_t(culture::income_type::military);
	auto is_military = is_military_requires_life_needs || is_military_requires_everyday_needs || is_military_requires_luxury_needs;
	auto is_investor = (types == state.culture_definitions.capitalists) || (types == state.culture_definitions.aristocrat);

	auto mil_pay = 0.f;
	mil_pay += is_military_requires_life_needs ? m_spending * adj_size * ln_costs * payouts_spending_multiplier : 0.0f;
	mil_pay += is_military_requires_everyday_needs ? m_spending * adj_size * en_costs * payouts_spending_multiplier : 0.0f;
	mil_pay += is_military_requires_luxury_needs ? m_spending * adj_size * lx_costs * payouts_spending_multiplier : 0.0f;

	return {
		.pension = pension_per_person * size,
		.unemployment = is_military ? 0.f : benefits_per_person * (size - pop_demographics::get_employment(state, pop)),
		.military = mil_pay,
		.investment = size * price_properties::labor::min * 0.05f + (is_investor ? payment_per_investor * size : 0.f)
	};
}

inline constexpr float investment_divident_rate = 0.001f;

void update_income_national_subsidy(sys::state& state){
	auto capitalists_key = demographics::to_key(state, state.culture_definitions.capitalists);
	auto aristocracy_key = demographics::to_key(state, state.culture_definitions.aristocrat);

	state.world.execute_parallel_over_pop([&](auto ids) {
		auto provs = state.world.pop_get_province_from_pop_location(ids);
		auto owners = state.world.province_get_nation_from_province_ownership(provs);
		auto population = state.world.nation_get_demographics(owners, demographics::total);
		auto unemployed = population - state.world.nation_get_demographics(owners, demographics::employed);
		auto capitalists = state.world.nation_get_demographics(owners, capitalists_key);
		auto aristocrats = state.world.nation_get_demographics(owners, aristocracy_key);
		auto investors = capitalists + aristocrats;

		auto states = state.world.province_get_state_membership(provs);
		auto markets = state.world.state_instance_get_market_from_local_market(states);
		auto owner_spending = state.world.nation_get_spending_level(owners);

		auto pop_savings = state.world.pop_get_savings(ids);

		auto pop_of_type = state.world.pop_get_size(ids);
		auto adj_pop_of_type = pop_of_type / state.defines.alice_needs_scaling_factor;

		auto budget = state.world.nation_get_last_base_budget(owners);

		auto social_budget =
			owner_spending
			* budget
			* ve::to_float(state.world.nation_get_social_spending(owners))
			/ 100.f;


		// LOCAL FIX (not upstream): dividends cannot be paid out of a negative pool. When the
		// national bank holds central-bank credit (a negative balance) the upstream expression
		// pays a negative dividend, which drives pop savings below zero and from there every
		// spending ratio and demand registration becomes negative.
		auto investment_dividents = ve::max(ve::fp_vector{ 0.f },
			state.world.nation_get_private_investment(owners) + state.world.nation_get_national_bank(owners)) * investment_divident_rate;
		auto investment_budget =
			owner_spending
			* budget
			* ve::to_float(state.world.nation_get_domestic_investment_spending(owners))
			/ 100.f;

		auto const p_level = state.world.nation_get_modifier_values(owners, sys::national_mod_offsets::pension_level);
		auto const unemp_level = state.world.nation_get_modifier_values(owners, sys::national_mod_offsets::unemployment_benefit);

		auto pension_ratio = ve::select(p_level * population > 0.f, p_level * population / (p_level * population + unemp_level * unemployed), 0.f);
		auto unemployment_ratio = ve::select(unemp_level * unemployed > 0.f, unemp_level * unemployed / (p_level * population + unemp_level * unemployed), 0.f);

		// +1.f is here to avoid division by zero
		// which should never happen
		// but i want to be extra sure

		auto const pension_per_person =
			pension_ratio
			* social_budget
			/ (population + 1.f);

		auto const benefits_per_person =
			unemployment_ratio
			* social_budget
			/ (unemployed + 1.f);

		auto const payment_per_investor =
			ve::select(
				investors > 0.f, 
				(investment_dividents + investment_budget)
				/ investors,
				0.f
			);

		auto const m_spending = owner_spending * ve::to_float(state.world.nation_get_military_spending(owners)) / 100.0f;

		auto types = state.world.pop_get_poptype(ids);

		auto ln_types = state.world.pop_type_get_life_needs_income_type(types);
		auto en_types = state.world.pop_type_get_everyday_needs_income_type(types);
		auto lx_types = state.world.pop_type_get_luxury_needs_income_type(types);

		auto ln_costs = ve::apply(
				[&](dcon::pop_type_id pt, dcon::market_id n) { return pt ? state.world.market_get_life_needs_costs(n, pt) : 0.0f; },
				types, markets);
		auto en_costs = ve::apply(
				[&](dcon::pop_type_id pt, dcon::market_id n) { return pt ? state.world.market_get_everyday_needs_costs(n, pt) : 0.0f; },
				types, markets);
		auto lx_costs = ve::apply(
				[&](dcon::pop_type_id pt, dcon::market_id n) { return pt ? state.world.market_get_luxury_needs_costs(n, pt) : 0.0f; },
				types, markets);

		auto total_costs = ln_costs + en_costs + lx_costs;

		auto acc_m = ve::select(ln_types == int32_t(culture::income_type::military), m_spending * adj_pop_of_type * ln_costs * payouts_spending_multiplier, 0.0f);


		auto acc_u = pension_per_person * pop_of_type;

		acc_m = acc_m + ve::select(en_types == int32_t(culture::income_type::military), m_spending * adj_pop_of_type * en_costs * payouts_spending_multiplier, 0.0f);

		acc_u = acc_u + ve::select(
			types == state.culture_definitions.capitalists,
			payment_per_investor
			* pop_of_type,
			0.0f
		);
		acc_u = acc_u + ve::select(
			types == state.culture_definitions.aristocrat,
			payment_per_investor
			* pop_of_type,
			0.0f
		);

		acc_m = acc_m + ve::select(lx_types == int32_t(culture::income_type::military), m_spending * adj_pop_of_type * lx_costs * payouts_spending_multiplier, 0.0f);

		auto not_military = !((ln_types == int32_t(culture::income_type::military)) & (en_types == int32_t(culture::income_type::military)) & (lx_types == int32_t(culture::income_type::military)));
		auto employment = pop_demographics::get_employment(state, ids);
		acc_u = acc_u + ve::select(
			not_military,
			benefits_per_person
			* (pop_of_type - employment),
			0.0f
		);

		ve::fp_vector base_income = pop_of_type * price_properties::labor::min * 0.05f;

		state.world.pop_set_savings(ids, state.inflation* state.world.pop_get_savings(ids) + (base_income + (acc_u + acc_m)));
#ifndef NDEBUG
		ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, acc_m);
		ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, acc_u);
#endif
	});

	// remove investment dividents:
	state.world.execute_serial_over_nation([&](auto ids) {
		auto investment = state.world.nation_get_private_investment(ids);
		state.world.nation_set_private_investment(ids, investment * (1.f - investment_divident_rate));
		auto bank = state.world.nation_get_national_bank(ids);
		state.world.nation_set_national_bank(ids, bank * (1.f - investment_divident_rate));
	});
}

std::vector<labor_ratio_wage> estimate_wage(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, bool accepted, float size) {
	float no_education_wage =
		state.world.province_get_labor_price(pid, labor::no_education)
		* state.world.province_get_labor_supply_sold(pid, labor::no_education);
	float basic_education_wage =
		state.world.province_get_labor_price(pid, labor::basic_education)
		* state.world.province_get_labor_supply_sold(pid, labor::basic_education); // craftsmen
	float high_education_wage =
		state.world.province_get_labor_price(pid, labor::high_education)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education); // clerks, clergy and bureaucrats
	float guild_education_wage =
		state.world.province_get_labor_price(pid, labor::guild_education)
		* state.world.province_get_labor_supply_sold(pid, labor::guild_education); // artisans
	float high_education_and_accepted_wage =
		state.world.province_get_labor_price(pid, labor::high_education_and_accepted)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education_and_accepted); // clerks, clergy and bureaucrats of accepted culture

	if(state.world.pop_type_get_is_paid_rgo_worker(ptid)) {
		return { {labor::no_education, 1.f, size * no_education_wage } };
	} else if(state.culture_definitions.primary_factory_worker == ptid) {
		auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_no_education);
		auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_basic_education);
		return {
			{labor::no_education, no_education, no_education * size * no_education_wage },
			{labor::basic_education, basic_education, basic_education * size * basic_education_wage }
		};
	} else if(state.culture_definitions.secondary_factory_worker == ptid || state.culture_definitions.bureaucrat == ptid || state.culture_definitions.clergy == ptid) {
		if(accepted) {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education);
			auto high_education_accepted = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education_accepted);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
				{labor::high_education_and_accepted, high_education_accepted, high_education_accepted * size * high_education_and_accepted_wage }
			};
		} else {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_high_education);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
			};
		}
	}
	return {};
}

std::vector<labor_ratio_wage> estimate_wage(sys::state const& state, dcon::pop_id pop) {
	return estimate_wage(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_is_primary_or_accepted_culture(pop),
		state.world.pop_get_size(pop)
	);
}

float estimate_total_wage(sys::state const& state, dcon::pop_id pop) {
	float total = 0.f;
	auto list = estimate_wage(state, pop);
	for(auto& item : list) {
		total += item.wage;
	}
	return total;
}


float estimate_slave_income(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, float size) {
	float no_education_wage =
		state.world.province_get_labor_price(pid, labor::no_education)
		* state.world.province_get_labor_supply_sold(pid, labor::no_education);
	float rgo_workers_wage =
		state.world.province_get_pop_labor_distribution(pid, pop_labor::rgo_worker_no_education)
		* no_education_wage;
	auto income_from_slaves = 0.f;
	for(auto pl : state.world.province_get_pop_location(pid)) {
		if(pl.get_pop().get_poptype() == state.culture_definitions.slaves) {
			income_from_slaves += pl.get_pop().get_size() * rgo_workers_wage;
		}
	}

	float aristocrats_share = state.world.province_get_landowners_share(pid);
	float num_aristocrat = state.world.province_get_demographics(
		pid,
		demographics::to_key(state, state.culture_definitions.aristocrat)
	);
	if(income_from_slaves >= 0.f && num_aristocrat > 0.f && state.culture_definitions.aristocrat == ptid) {
		return size * income_from_slaves / num_aristocrat;
	} else {
		return 0.f;
	}
}

float estimate_slave_income(sys::state const& state, dcon::pop_id pop) {
	return estimate_slave_income(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}


//local merchants take a cut from most local monetary operations
inline constexpr float local_market_cut_baseline = 0.01f;
float market_cut(sys::state const& state, dcon::market_id market, float no_education_wage) {
	auto modified = local_market_cut_baseline - state.world.market_get_stockpile(market, economy::money) / (no_education_wage + 0.000001f) / 100'000.f;
	return std::clamp(modified, 0.f, 0.1f);
}

void update_income_wages(sys::state& state){

	static auto buffer_rgo_workers_wage = state.world.province_make_vectorizable_float_buffer();
	static auto buffer_primary_workers_wage = state.world.province_make_vectorizable_float_buffer();
	static auto buffer_high_not_accepted_workers_wage = state.world.province_make_vectorizable_float_buffer();
	static auto buffer_high_accepted_workers_wage = state.world.province_make_vectorizable_float_buffer();

	// LOCAL EXPERIMENT (ALICE_EXP_RGO_COST_WAGE=1): province cost-valued RGO wage bill.
	static bool const rgo_cost_wage_enabled = []() {
		auto env = std::getenv("ALICE_EXP_RGO_COST_WAGE");
		return env && std::atoi(env) != 0;
	}();
	static ve::vectorizable_buffer<float, dcon::province_id> rgo_cost_wage_bill_buf(uint32_t(1));
	static uint32_t rgo_cost_wage_provinces = 1;
	if(rgo_cost_wage_enabled) {
		if(state.world.province_size() > rgo_cost_wage_provinces) {
			rgo_cost_wage_bill_buf = state.world.province_make_vectorizable_float_buffer();
			rgo_cost_wage_provinces = state.world.province_size();
		}
		state.world.for_each_province([&](dcon::province_id p) {
			double bill = 0.0;
			state.world.for_each_commodity([&](dcon::commodity_id c) {
				if(c == economy::money)
					return;
				bill += double(economy::rgo_cost_wage(state, c, p));
			});
			rgo_cost_wage_bill_buf.set(p, std::isfinite(bill) ? float(bill) : 0.f);
		});
	}

	// LOCAL EXPERIMENT: province direct factory wage bill (sum over factories).
	static ve::vectorizable_buffer<float, dcon::province_id> direct_factory_wage_bill(uint32_t(1));
	static uint32_t direct_factory_wage_provinces = 1;
	if(direct_factory_wage) {
		if(state.world.province_size() > direct_factory_wage_provinces) {
			direct_factory_wage_bill = state.world.province_make_vectorizable_float_buffer();
			direct_factory_wage_provinces = state.world.province_size();
		}
		state.world.for_each_province([&](dcon::province_id p) {
			double total = 0.0;
			if(financed_wage) {
				total = double(economy::get_financed_wage_bill(state, p));
			} else {
				for(auto fl : state.world.province_get_factory_location(p)) {
					total += double(get_total_wage(state, fl.get_factory()));
				}
			}
			direct_factory_wage_bill.set(p, std::isfinite(total) ? float(total) : 0.f);
		});
	}

	province::ve_parallel_for_each_land_province(state, [&](auto pid) {
		auto no_education_wage =
			state.world.province_get_labor_price(pid, labor::no_education)
			* state.world.province_get_labor_supply_sold(pid, labor::no_education);
		auto basic_education_wage =
			state.world.province_get_labor_price(pid, labor::basic_education)
			* state.world.province_get_labor_supply_sold(pid, labor::basic_education); // craftsmen
		auto high_education_wage =
			state.world.province_get_labor_price(pid, labor::high_education)
			* state.world.province_get_labor_supply_sold(pid, labor::high_education); // clerks, clergy and bureaucrats
		auto guild_education_wage =
			state.world.province_get_labor_price(pid, labor::guild_education)
			* state.world.province_get_labor_supply_sold(pid, labor::guild_education); // artisans
		auto high_education_and_accepted_wage =
			state.world.province_get_labor_price(pid, labor::high_education_and_accepted)
			* state.world.province_get_labor_supply_sold(pid, labor::high_education_and_accepted); // clerks, clergy and bureaucrats of accepted culture

		auto rgo_workers_wage =
			state.world.province_get_pop_labor_distribution(pid, pop_labor::rgo_worker_no_education)
			* no_education_wage;

		buffer_rgo_workers_wage.set(pid, rgo_workers_wage);

		if(rgo_cost_wage_enabled) {
			auto workers_v = ve::fp_vector{ 0.f };
			state.world.for_each_pop_type([&](dcon::pop_type_id pt) {
				if(state.world.pop_type_get_is_paid_rgo_worker(pt))
					workers_v = workers_v + state.world.province_get_demographics(pid, demographics::to_key(state, pt));
			});
			auto const bill_v = rgo_cost_wage_bill_buf.get(pid);
			auto const per_worker = ve::select(workers_v > 0.f, bill_v / ve::max(workers_v, ve::fp_vector{ 1.f }), ve::fp_vector{ 0.f });
			buffer_rgo_workers_wage.set(pid, per_worker);
			rgo_workers_wage = per_worker;
		}

		auto primary_workers_wage =
			state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_no_education)
			* no_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_basic_education)
			* basic_education_wage;

		buffer_primary_workers_wage.set(pid, primary_workers_wage);

		auto high_not_accepted_workers_wage =
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_no_education)
			* no_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_basic_education)
			* basic_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_high_education)
			* high_education_wage;

		buffer_high_not_accepted_workers_wage.set(pid, high_not_accepted_workers_wage);

		auto high_accepted_workers_wage =
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_no_education)
			* no_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_basic_education)
			* basic_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education)
			* high_education_wage
			+
			state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education_accepted)
			* high_education_and_accepted_wage;

		buffer_high_accepted_workers_wage.set(pid, high_accepted_workers_wage);

		if(direct_factory_wage) {
			auto const wage_bill = direct_factory_wage_bill.get(pid);
			auto const craftsmen = state.world.province_get_demographics(pid,
				demographics::to_key(state, state.culture_definitions.primary_factory_worker));
			auto const per_worker = ve::select(craftsmen > 0.f, wage_bill / ve::max(craftsmen, ve::fp_vector{ 1.f }), ve::fp_vector{ 0.f });
			buffer_primary_workers_wage.set(pid, per_worker);
			// the direct bill is fully credited to the primary factory worker pop; avoid double payment
			buffer_high_not_accepted_workers_wage.set(pid, ve::fp_vector{ 0.f });
			buffer_high_accepted_workers_wage.set(pid, ve::fp_vector{ 0.f });
		}

		// RGOS and slaves cashback
		auto profit_from_slaves = ve::apply([&](dcon::province_id province, float earning_per_slave) {
			auto slaves_profit = 0.f;
			float payment_per_aristocrat = 0.f;
			for(auto pl : state.world.province_get_pop_location(province)) {
				if(pl.get_pop().get_poptype() == state.culture_definitions.slaves) {
					slaves_profit += pl.get_pop().get_size() * earning_per_slave;
				}
			}
			return slaves_profit;
		}, pid, rgo_workers_wage);
		auto old_rgo_cash = state.world.province_get_rgo_bank(pid);
		state.world.province_set_rgo_bank(pid, old_rgo_cash + profit_from_slaves);
	});

	{
		static bool const wage_conservation_pops = []() {
			auto e = std::getenv("ALICE_EXP_WAGE_CONSERVATION");
			return e && std::atoi(e) != 0;
		}();
		if(wage_conservation_pops) {
			auto const sc = economy::wage_credit_payout_scale();
			if(std::isfinite(sc) && sc >= 0.f && sc != 1.f) {
				province::ve_for_each_land_province(state, [&](auto ids) {
					buffer_rgo_workers_wage.set(ids, buffer_rgo_workers_wage.get(ids) * sc);
					buffer_primary_workers_wage.set(ids, buffer_primary_workers_wage.get(ids) * sc);
					buffer_high_not_accepted_workers_wage.set(ids, buffer_high_not_accepted_workers_wage.get(ids) * sc);
					buffer_high_accepted_workers_wage.set(ids, buffer_high_accepted_workers_wage.get(ids) * sc);
				});
			}
		}
	}

	auto pop_wage_income = state.world.pop_make_vectorizable_float_buffer();
	state.world.execute_parallel_over_pop([&](auto pops) {
		auto savings = state.world.pop_get_savings(pops);
		auto pop_type = state.world.pop_get_poptype(pops);
		auto size = state.world.pop_get_size(pops);
		auto culture = state.world.pop_get_culture(pops);
		auto accepted = state.world.pop_get_is_primary_or_accepted_culture(pops);
		auto province = state.world.pop_get_province_from_pop_location(pops);

		auto high_education =
			(pop_type == state.culture_definitions.secondary_factory_worker)
			|| (pop_type == state.culture_definitions.bureaucrat)
			|| (pop_type == state.culture_definitions.clergy);

		auto wage_per_person = ve::select(
			state.world.pop_type_get_is_paid_rgo_worker(pop_type),
			buffer_rgo_workers_wage.get(province),
			ve::select(
				pop_type == state.culture_definitions.primary_factory_worker,
				buffer_primary_workers_wage.get(province),
				ve::select(
					accepted && high_education,
					buffer_high_accepted_workers_wage.get(province),
					ve::select(
						high_education,
						buffer_high_not_accepted_workers_wage.get(province),
						ve::fp_vector{0.f}
					)
				)
			)
		);
		auto wage = ve::select(size == 0.f, 0.f, size * wage_per_person);
		pop_wage_income.set(pops, wage);
		state.world.pop_set_savings(pops, wage + savings);
#ifndef NDEBUG
		ve::apply([](float v) { assert(std::isfinite(v) && v >= 0); }, wage + savings);
#endif // !NDEBUG
	});

	{
		double sum = 0.0;
		state.world.execute_serial_over_pop([&](auto ids) {
			ve::apply([&](float v) { sum += double(v); }, pop_wage_income.get(ids));
		});
		trace_daily_wage_income = sum;
		{
			auto const sc = economy::wage_credit_payout_scale();
			trace_daily_natural_wage_credit = (std::isfinite(sc) && sc > 1e-9f) ? (sum / double(sc)) : sum;
		}
	}

	market_poptype_wage_stride = int(state.world.pop_type_size());
	market_poptype_wage.assign(size_t(state.world.market_size()) * size_t(market_poptype_wage_stride), 0.f);
	state.world.execute_serial_over_pop([&](auto p) {
		auto provs = state.world.pop_get_province_from_pop_location(p);
		auto sids = state.world.province_get_state_membership(provs);
		auto mids = state.world.state_instance_get_market_from_local_market(sids);
		auto pts = state.world.pop_get_poptype(p);
		auto amounts = pop_wage_income.get(p);
		ve::apply([&](dcon::market_id m, dcon::pop_type_id pt, float v) {
			auto const idx = size_t(m.index()) * size_t(market_poptype_wage_stride) + size_t(pt.index());
			if(idx < market_poptype_wage.size())
				market_poptype_wage[idx] += v;
		}, mids, pts, amounts);
	});
}

float estimate_next_day_raw_income(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto estimated =
		estimate_artisan_income(state, pop)
		+ estimate_slave_income(state, pop)
		+ estimate_trade_income(state, pop)
		+ estimate_total_wage(state, pop);

	auto from_nation = estimate_income_from_nation(state, pop);

	estimated +=
		from_nation.investment
		+ from_nation.military
		+ from_nation.pension
		+ from_nation.unemployment;

	return estimated;
}

float estimate_next_day_budget_before_taxes(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto current = state.world.pop_get_savings(pop);
	current -= prepare_pop_budget(state, pop).spent_total;

	auto estimated = current
		+ estimate_artisan_income(state, pop)
		+ estimate_slave_income(state, pop)
		+ estimate_trade_income(state, pop)
		+ estimate_total_wage(state, pop);

	auto from_nation = estimate_income_from_nation(state, pop);

	estimated +=
		from_nation.investment
		+ from_nation.military
		+ from_nation.pension
		+ from_nation.unemployment;

	return estimated;
}

float estimate_trade_spending(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto next_day = state.world.pop_get_savings(pop);
	return market_tax * next_day;
}

float estimate_tax_spending(
	sys::state const& state,
	dcon::pop_id pop,
	float tax_rate
) {
	auto next_day = estimate_next_day_raw_income(state, pop);
	return next_day * (1.f - market_tax) * tax_rate;
}

float estimate_pop_demand_internal_life(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.life_needs.demand_scale
		* budget.life_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_lf_needs_scale
		* state.world.pop_type_get_life_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
}
float estimate_pop_demand_internal_everyday(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.everyday_needs.demand_scale
		* budget.everyday_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_ev_needs_scale
		* state.world.pop_type_get_everyday_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor
		* invention_factor;
}
float estimate_pop_demand_internal_luxury(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.luxury_needs.demand_scale
		* budget.luxury_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_lx_needs_scale
		* state.world.pop_type_get_luxury_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor
		* invention_factor;
}

float estimate_pop_demand_internal_category(
	sys::state const& state, dcon::market_id m, dcon::consumption_category_id cat, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget
) {
	auto total = budget.per_consumption_category[cat.index()].demand_scale
		* budget.per_consumption_category[cat.index()].satisfied_with_money_ratio
		* state.world.market_get_local_consumption_weights(m, c.index() + cat.index() * state.world.commodity_size())
		* state.world.consumption_category_get_weights(cat, c);

	auto pop_size = state.world.pop_get_size(pop);
	return total * pop_size / state.defines.alice_needs_scaling_factor;
}
float estimate_pop_spending_category(sys::state const& state, dcon::consumption_category_id cat, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto demand = estimate_pop_demand_internal_category(
		state, market, cat, cid, pop, budget
	);
	auto actually_bought = state.world.market_get_actual_probability_to_buy(market, cid);
	auto cost = state.world.market_get_price(market, cid);
	return demand * actually_bought * cost;
}

float estimate_pop_spending_life(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_life_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_life_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_life(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = state.world.market_get_actual_probability_to_buy(market, cid);
	auto cost = state.world.market_get_price(market, cid);
	return demand * actually_bought * cost;
}

float estimate_pop_spending_everyday(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_everyday_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_everyday_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_everyday(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = state.world.market_get_actual_probability_to_buy(market, cid);
	auto cost = state.world.market_get_price(market, cid);
	return demand * actually_bought * cost;
}

float estimate_pop_spending_luxury(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_luxury_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_luxury_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_luxury(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = state.world.market_get_actual_probability_to_buy(market, cid);
	auto cost = state.world.market_get_price(market, cid);
	return demand * actually_bought * cost;
}

vectorized_pops_budget<float> prepare_pop_budget(const sys::state& state, dcon::pop_id ids) {
	return prepare_pop_budget_templated(state, ids);
}

}

float estimate_pops_consumption(sys::state const& state, dcon::commodity_id c, dcon::province_id p) {
	auto zone = state.world.province_get_state_membership(p);
	auto market = state.world.state_instance_get_market_from_local_market(zone);

	auto satisfaction = state.world.market_get_actual_probability_to_buy(market, c);

	auto nation = state.world.province_get_nation_from_province_ownership(p);

	auto weight_life = state.world.market_get_life_needs_weights(market, c);
	auto weight_everyday = state.world.market_get_everyday_needs_weights(market, c);
	auto weight_luxury = state.world.market_get_luxury_needs_weights(market, c);

	float life_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_life_needs) + 1.0f
	};
	float everyday_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_everyday_needs) + 1.0f
	};
	float luxury_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_luxury_needs) + 1.0f,
	};

	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;

	float total = 0.f;
	state.world.province_for_each_pop_location(p, [&](auto location) {
		dcon::pop_id pop = state.world.pop_location_get_pop(location);

		auto pop_type = state.world.pop_get_poptype(pop);
		auto strata = state.world.pop_type_get_strata(pop_type);

		pops::vectorized_pops_budget<float> budget = pops::prepare_pop_budget(state, pop);

		auto consumption_life = pops::estimate_pop_demand_internal_life(
			state, c, pop, budget, life_mul, weight_life, invention_factor
		);
		auto consumption_everyday = pops::estimate_pop_demand_internal_everyday(
			state, c, pop, budget, everyday_mul, weight_everyday, invention_factor
		);
		auto consumption_luxury = pops::estimate_pop_demand_internal_luxury(
			state, c, pop, budget, luxury_mul, weight_luxury, invention_factor
		);
		state.world.for_each_consumption_category([&](auto cat) {
			auto categories = pops::estimate_pop_demand_internal_category(
				state, market, cat, c, pop, budget
			);
			total = total + categories;
		});

		total += consumption_life + consumption_everyday + consumption_luxury;
	});

	return total * satisfaction;
}
}

namespace economy {
double daily_wage_credit() {
	return ::trace_daily_wage_income;
}
}

namespace economy {
double daily_natural_wage_credit() {
	return ::trace_daily_natural_wage_credit;
}
}
