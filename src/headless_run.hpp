#pragma once
// -headless -years N: runs N game years with no speed cap, prints seconds per game year, and with -dump DIR
// writes monthly CSVs (nations.csv, prices.csv, provinces.csv) on the 1st of each month.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// PATCHED: 上游新增的 -dump/-years 代码依赖下面两个头文件，但本头文件未包含，
// 单独编译 entry_point_nix.cpp 时会报 price_slow / execute_pending_commands 未声明。
#include "economy/economy.hpp"
#include "economy/economy_stats.hpp"
#include "economy/economy_production.hpp"
#include "economy/construction.hpp"
#include "economy/economy_constants.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "provinces/province.hpp"
#include "gamestate/commands.hpp"

namespace headless {

inline FILE* factory_trace_csv = nullptr;
inline FILE* construction_trace_csv = nullptr;
inline FILE* factory_input_trace_csv = nullptr;
inline FILE* seller_share_trace_csv = nullptr;

inline std::string ymd_str(sys::year_month_day d) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, int(d.month), int(d.day));
	return std::string(buf);
}
inline std::string ymd_of(sys::state& state) {
	return ymd_str(state.current_date.to_ymd(state.start_date));
}

inline std::string tag_of(sys::state& state, dcon::nation_id n) {
	if(!n)
		return "";
	return nations::int_to_tag(
			state.world.national_identity_get_identifying_int(state.world.nation_get_identity_from_identity_holder(n)));
}

inline dcon::commodity_id find_good(sys::state& state, char const* name) {
	dcon::commodity_id found{};
	state.world.for_each_commodity([&](dcon::commodity_id c) {
		if(!found && text::produce_simple_string(state, state.world.commodity_get_name(c)) == name)
			found = c;
	});
	return found;
}

inline std::string csv_safe(std::string s) {
	for(auto& c : s) if(c == ',' || c == '\n' || c == '\r') c = ' ';
	return s;
}

inline void dump_construction_trace(sys::state& state, std::string const& date) {
	if(!construction_trace_csv)
		return;
	for(auto c : state.world.in_factory_construction) {
		auto const owner = state.world.factory_construction_get_nation(c);
		auto const province = state.world.factory_construction_get_province(c);
		auto const type = state.world.factory_construction_get_type(c);
		auto const good = state.world.factory_type_get_output(type);
		fprintf(construction_trace_csv,
			"%s,%d,%s,%s,%s,%d,%d,%.6g\n",
			date.c_str(), c.id.index(), tag_of(state, owner).c_str(),
			text::produce_simple_string(state, state.world.province_get_name(province)).c_str(),
			text::produce_simple_string(state, state.world.commodity_get_name(good)).c_str(),
			state.world.factory_construction_get_is_upgrade(c) ? 1 : 0,
			state.world.factory_construction_get_is_pop_project(c) ? 1 : 0,
			double(economy::construction_progress(state, c.id)));
	}
	fflush(construction_trace_csv);
}

inline void dump_seller_share_trace(sys::state& state, std::string const& date) {
	if(!seller_share_trace_csv)
		return;
	state.world.for_each_market([&](dcon::market_id m) {
		auto const sid = state.world.market_get_zone_from_local_market(m);
		if(!sid)
			return;
		auto const owner = state.world.state_instance_get_nation_from_state_ownership(sid);
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(c == economy::money)
				return;
			auto const b = economy::explain_output(state, c, sid);
			auto const total = b.rgo + b.guild + b.factories;
			fprintf(seller_share_trace_csv,
				"%s,%d,%s,%s,%.6g,%.6g,%.6g,%.6g\n",
				date.c_str(), m.index(), tag_of(state, owner).c_str(),
				text::produce_simple_string(state, state.world.commodity_get_name(c)).c_str(),
				double(b.rgo), double(b.guild), double(b.factories), double(total));
		});
	});
	fflush(seller_share_trace_csv);
}

inline void dump_factory_input_trace(sys::state& state, std::string const& date) {
	if(!factory_input_trace_csv)
		return;
	state.world.for_each_factory([&](dcon::factory_id f) {
		auto const location = state.world.factory_get_province_from_factory_location(f);
		auto const owner = state.world.province_get_nation_from_province_ownership(location);
		auto const zone = state.world.province_get_state_membership(location);
		auto const market = state.world.state_instance_get_market_from_local_market(zone);
		auto const ftid = state.world.factory_get_building_type(f);
		auto const output_good = state.world.factory_type_get_output(ftid);
		auto const& inputs = state.world.factory_type_get_inputs(ftid);
		for(uint32_t j = 0; j < economy::commodity_set::set_size; ++j) {
			auto const input_good = inputs.commodity_type[j];
			if(!input_good)
				break;
			fprintf(factory_input_trace_csv,
				"%s,%d,%s,%s,%s,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
				date.c_str(), f.index(), tag_of(state, owner).c_str(),
				text::produce_simple_string(state, state.world.province_get_name(location)).c_str(),
				text::produce_simple_string(state, state.world.commodity_get_name(output_good)).c_str(),
				text::produce_simple_string(state, state.world.commodity_get_name(input_good)).c_str(),
				double(inputs.commodity_amounts[j]), double(state.world.market_get_price(market, input_good)),
				double(state.world.market_get_actual_probability_to_buy(market, input_good)),
				double(state.world.market_get_expected_probability_to_buy(market, input_good)),
				double(state.world.market_get_supply(market, input_good)),
				double(state.world.market_get_demand(market, input_good)),
				double(state.world.market_get_stockpile(market, input_good)));
		}
	});
	fflush(factory_input_trace_csv);
}

inline void dump_factory_trace(sys::state& state, std::string const& date) {
	if(!factory_trace_csv)
		return;
	state.world.for_each_factory([&](dcon::factory_id f) {
		auto const location = state.world.factory_get_province_from_factory_location(f);
		auto const owner = state.world.province_get_nation_from_province_ownership(location);
		auto const zone = state.world.province_get_state_membership(location);
		auto const market = state.world.state_instance_get_market_from_local_market(zone);
		auto const ftid = state.world.factory_get_building_type(f);
		auto const good = state.world.factory_type_get_output(ftid);
		auto const d = economy::diagnose_factory(state, f);
		fprintf(factory_trace_csv,
			"%s,%d,%s,%d,%s,%d,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), f.index(), tag_of(state, owner).c_str(),
			location.index(), 
			text::produce_simple_string(state, state.world.province_get_name(location)).c_str(),
			market.index(),
			text::produce_simple_string(state, state.world.commodity_get_name(good)).c_str(),
			double(state.world.factory_get_size(f)), double(d.base_workforce), double(d.hiring_budget), double(d.affordable_employment),
			double(d.desired_employment), double(d.construction_cost_per_capacity),
			double(d.output_per_worker), double(d.output_price), double(d.input_cost_per_worker), double(d.wage_per_worker),
			double(d.profit), double(d.profit_per_capacity), double(d.actual_output_per_capacity), double(d.actual_probability_to_sell),
			double(d.employment_ratio), double(d.labor_satisfaction), double(d.hiring_sat), double(d.hiring_multiplier),
			double(d.min_expected_input), double(d.min_available_input), double(d.subsidy), double(d.tokens), double(d.expand_priority));
	});
	fflush(factory_trace_csv);
}

// ===== M10 OBSERVATION LAYER (2026-11-12) =====
// Read-only measurement columns for summary.csv. Spec: dod-econ/model/design-measurement-export-2026-11-12.md.
// Rules this block must keep (design L1/L2/L3, §6.2, §E8):
//   * reads dcon getters only; never calls a set_*/make_*/resize_*, never writes sys::state;
//   * adds no engine state: everything lives in stack doubles and function-local vectors;
//   * does not touch the existing accumulation loops in dump_month, so the 49 inherited columns keep
//     their exact floating-point evaluation order (L3/R3). Every input this needs is passed in;
//   * the Paasche block below is a re-implementation isomorphic to the dead branch of
//     economy::refresh_price_level (economy.cpp:991-1005). Do NOT replace it with a call to that
//     function: it writes the static price_level_now buffer, and economy.cpp:10792-10814 multiplies
//     that buffer into the labour price when ALICE_EXP_PRICE_INCOME_INDEX=1. A call from the export
//     path would silently rewrite wages from the observation layer. Keep the two copies in sync by
//     hand if economy.cpp:991-1005 ever changes.
struct summary_measurement {
	// columns 50-55: totals and price level
	double gdp_nominal = 0; // 50: sum_m sum_c sup*price over finite sup>0 cells (market prices, not medians)
	double gdp_engine_registered = 0; // 51: sum_m market.gdp -- engine accumulator, TODAY's registered supply at commodity median price. Reconciliation aid, not a GDP series.
	double price_level_paasche = 0; // 52: sum sup*price / sum sup*cost over the engine's priced filter set. 1.0 is defined at load-time cost constants, NOT on the first exported row: presimulate() already ran 730 days.
	double gdp_deflated = 0; // 53: gdp_nominal / price_level_paasche == sum sup*cost, i.e. gdp_real minus zero-priced supply. Reads as a cross-check column, not a second gdp_real.
	double gdp_in_baskets = 0; // 54: gdp_nominal / basket_cost_now -- one day of output in worker baskets
	double price_coverage_share = 0; // 55: paasche numerator / gdp_nominal. ~1.0 means nothing was dropped as unpriced; well below 1.0 means supply is sitting at price<=0.
	// columns 56-64: basket and wages. Only the PAIR (real_wage_index, basket_fill) is interpretable:
	// real_wage_index rising while basket_fill falls means the price signal decoupled from availability
	// (rationing), not that living standards improved. real_wage_realized is for monotonicity only.
	double basket_cost_now = 0; // 56: employment-weighted sum_c price(m,c)*coef(c), current prices
	double basket_cost_base = 0; // 57: sum_c cost(c)*coef(c), a run-time constant (base-period basket)
	double cpi_basket = 0; // 58: basket_cost_now / basket_cost_base -- pure price index, fixed weights
	double basket_fill = 0; // 59: employment-weighted (ln*ln_r + en*en_r)/(ln+en) over markets
	double wage_bill = 0; // 60: sum over land provinces and tiers of labour_price * employed
	double wage_employed_people = 0; // 61: sum of the same employed terms (people)
	double wage_nominal_mean = 0; // 62: wage_bill / wage_employed_people
	double real_wage_index = 0; // 63: wage_nominal_mean / basket_cost_now
	double real_wage_realized = 0; // 64: real_wage_index * basket_fill
	// columns 65-68: labour demand split and transport prices
	double labor_demand_people = 0; // 65: old column 42 minus column 66 (same doubles, so the split adds back)
	double labor_demand_capacity = 0; // 66: building capacity that advanced_province_buildings.cpp:244-258 adds into labor_demand (design D3)
	double land_transport_price = 0; // column 36 REWRITTEN: mean market land price over ALL markets, no sample filter. The engine hard-zeroes it daily (economy.cpp:8464), so this is 0 by construction and that 0 is the finding.
	double land_transport_price_max = 0; // 67: max market land price. Any non-zero value means the engine changed behaviour.
	double naval_transport_price_samples = 0; // 68: what old column 36 actually carried -- count of markets with naval price > 0
	// columns 69-72: money pool revaluation
	double pool_total_revalued = 0; // 69: market cash + pop savings, the only two pools economy.cpp revalues
	double pool_total_unrevalued = 0; // 70: the other ten pools, never revalued (see design 3.6 for the mapping)
	double pool_revalued_share = 0; // 71: revalued / (revalued + unrevalued). Only this share can be hit by inflation.
	double inflation = 0; // 72: state.inflation as a multiplier (1.0 = no revaluation)
};
// Every input is a double dump_month has already computed for an existing column, which is what makes
// invariants I2/I3 ("pool totals reconcile with the old columns") exact instead of approximate.
inline summary_measurement compute_measurement(sys::state& state, double labor_demand_field,
		double market_money, double pop_savings, double treasury, double government_bank,
		double gov_escrow, double rgo_bank, double factory_bank, double rent_bank,
		double pop_investments, double pop_dividends, double private_inv, double lender_balance,
		[[maybe_unused]] double fac_wage_sum, [[maybe_unused]] double fac_emp_sum) {
	// fac_wage_sum / fac_emp_sum are accepted so the call site passes exactly what dump_month has, and
	// are unused on purpose: columns 60-63 are a price x quantity recomputation from province labour
	// fields, not the engine's factory payroll (design §3.2, (b2)). They are here for a later
	// factory-vs-province cross-check.
	summary_measurement r;

	// --- 1. basket coefficients: the primary factory worker's base life + everyday needs per head.
	// Raw need coefficients only -- deliberately NOT multiplied by life_needs_mult, needs_weights,
	// invention_factor or needs_scale_factor (economy.cpp:7686-7712). Do not swap this for
	// market_get_life_needs_costs(): that field carries the weight drift, so a cpi built on it counts
	// "needs weights changed" as "prices rose" (design D6 / R6).
	auto const worker_type = state.culture_definitions.primary_factory_worker;
	std::vector<double> coef(size_t(state.world.commodity_size()), 0.0);
	double basket_cost_base = 0.0;
	state.world.for_each_commodity([&](dcon::commodity_id c) {
		if(c == economy::money) return; // same exclusion as the market loop below, so numerator and denominator weigh the same set
		double const cf = double(state.world.pop_type_get_life_needs(worker_type, c))
				+ double(state.world.pop_type_get_everyday_needs(worker_type, c));
		coef[c.index()] = cf;
		if(!(cf > 0.0)) return;
		double const cost = double(state.world.commodity_get_cost(c));
		if(std::isfinite(cost)) basket_cost_base += cost * cf;
	});

	// --- 2. one pass over markets x commodities: nominal gdp, the paasche pair, transport prices,
	// the engine's own gdp accumulator, and the per-market pieces of the basket.
	auto const n_markets = size_t(state.world.market_size());
	std::vector<double> basket_now(n_markets, 0.0); // sum_c price(m,c)*coef(c)
	std::vector<double> fill_share(n_markets, 0.0); // (ln*ln_r + en*en_r)/(ln+en)
	std::vector<double> market_employment(n_markets, 0.0); // employed people living in market m
	double gdp_nominal = 0.0, pa_num = 0.0, pa_den = 0.0, gdp_engine = 0.0;
	double land_sum = 0.0, land_max = 0.0, naval_samples = 0.0;
	int markets = 0;
	state.world.for_each_market([&](dcon::market_id m) {
		++markets;
		gdp_engine += double(state.world.market_get_gdp(m));
		double const lp = double(state.world.market_get_land_transportation_price(m));
		land_sum += lp;
		land_max = std::max(land_max, lp);
		if(double(state.world.market_get_naval_transportation_price(m)) > 0.0) naval_samples += 1.0;
		double bk = 0.0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(c == economy::money) return;
			double const pr = double(state.world.market_get_price(m, c));
			// basket term: fixed coefficients, current market prices. No supply filter and no price>0
			// filter -- a good whose price collapses towards 0 must show up as a cheaper basket, that
			// collapse is exactly what real_wage_index has to be read against (design §3.2).
			double const cf = coef[size_t(c.index())];
			if(cf > 0.0 && std::isfinite(pr)) bk += pr * cf;
			double const sup = double(state.world.market_get_supply(m, c));
			if(!(sup > 0.0) || !std::isfinite(sup)) return; // engine's supply filter
			if(std::isfinite(pr)) gdp_nominal += sup * pr; // column 50 keeps price<=0 cells; a non-finite price carries no value
			double const cost = double(state.world.commodity_get_cost(c));
			if(!(cost > 0.0) || !(pr > 0.0) || !std::isfinite(pr)) return; // engine's priced filter set
			pa_num += sup * pr;
			pa_den += sup * cost;
		});
		basket_now[size_t(m.index())] = bk;
		double const ln = double(state.world.market_get_life_needs_costs(m, worker_type));
		double const en = double(state.world.market_get_everyday_needs_costs(m, worker_type));
		if(std::isfinite(ln) && std::isfinite(en) && ln + en > 0.0) {
			double const ln_r = double(state.world.market_get_satisfied_ratio_of_max_life_needs(m, worker_type));
			double const en_r = double(state.world.market_get_satisfied_ratio_of_max_everyday_needs(m, worker_type));
			double const x = (ln * ln_r + en * en_r) / (ln + en);
			if(std::isfinite(x)) fill_share[size_t(m.index())] = x;
		}
	});

	// --- 3. land provinces: the capacity that labour_demand wrongly contains, and the wage bill.
	// employed = min(supply, max(0, demand - capacity)) deliberately drops the building capacity that
	// advanced_province_buildings.cpp:244-258 adds into province.labor_demand (capacity is slots, not
	// people -- design D3). The engine's own satisfaction ratio does not drop it, so wage_bill is
	// knowingly not the engine's payroll total; the two converge once the capacity bug itself is fixed.
	double capacity_total = 0.0, wage_bill = 0.0, employed_total = 0.0;
	state.world.for_each_province([&](dcon::province_id p) {
		if(!province::is_land(state, p)) return; // same filter as the old labour columns
		// civilian_ports feed labor::no_education, schools_and_universities feed labor::high_education
		// (definitions[] in advanced_province_buildings.cpp:201-227).
		double const cap_no_edu =
				double(state.world.province_get_advanced_province_building_private_size(p, advanced_province_buildings::list::civilian_ports))
				+ double(state.world.province_get_advanced_province_building_national_size(p, advanced_province_buildings::list::civilian_ports));
		double const cap_high_edu =
				double(state.world.province_get_advanced_province_building_private_size(p, advanced_province_buildings::list::schools_and_universities))
				+ double(state.world.province_get_advanced_province_building_national_size(p, advanced_province_buildings::list::schools_and_universities));
		capacity_total += cap_no_edu + cap_high_edu;
		double employed_here = 0.0;
		for(int32_t l = 0; l < economy::labor::total; ++l) {
			double const dem = double(state.world.province_get_labor_demand(p, l));
			double const sup = double(state.world.province_get_labor_supply(p, l));
			double const price = double(state.world.province_get_labor_price(p, l));
			if(!std::isfinite(dem) || !std::isfinite(sup) || !std::isfinite(price)) continue;
			double const cap = l == economy::labor::no_education ? cap_no_edu
					: (l == economy::labor::high_education ? cap_high_edu : 0.0);
			double const dem_people = std::max(0.0, dem - cap);
			double const sup_ok = std::max(0.0, sup);
			double const employed = std::min(sup_ok, dem_people);
			wage_bill += price * employed;
			employed_here += employed;
		}
		employed_total += employed_here;
		auto const sid = state.world.province_get_state_membership(p);
		if(sid) {
			auto const mkt = state.world.state_instance_get_market_from_local_market(sid);
			if(mkt) market_employment[size_t(mkt.index())] += employed_here;
		}
	});

	// --- 4. aggregation. Denominators are guarded, never clamped: a zero denominator yields 0 so no
	// column can go NaN/Inf (harness.py treats a non-finite summary cell as a run violation).
	// Weighted mean over markets, normalised by the weight actually used: a land province with no
	// market contributes wages but no basket, and quietly dividing by the full employment total would
	// then scale the basket down for a reason that has nothing to do with prices.
	double basket_weighted = 0.0, fill_weighted = 0.0, mapped_employment = 0.0;
	for(size_t i = 0; i < market_employment.size(); ++i) {
		if(!(market_employment[i] > 0.0)) continue;
		mapped_employment += market_employment[i];
		basket_weighted += market_employment[i] * basket_now[i];
		fill_weighted += market_employment[i] * fill_share[i];
	}
	r.gdp_nominal = gdp_nominal;
	r.gdp_engine_registered = gdp_engine;
	r.price_level_paasche = pa_den > 0.0 ? pa_num / pa_den : 0.0;
	r.price_coverage_share = gdp_nominal > 0.0 ? pa_num / gdp_nominal : 0.0;
	r.gdp_deflated = r.price_level_paasche > 0.0 ? gdp_nominal / r.price_level_paasche : 0.0;
	r.basket_cost_base = basket_cost_base;
	r.basket_cost_now = mapped_employment > 0.0 ? basket_weighted / mapped_employment : 0.0;
	r.cpi_basket = basket_cost_base > 0.0 ? r.basket_cost_now / basket_cost_base : 0.0;
	r.basket_fill = mapped_employment > 0.0 ? fill_weighted / mapped_employment : 0.0;
	r.gdp_in_baskets = r.basket_cost_now > 0.0 ? gdp_nominal / r.basket_cost_now : 0.0;
	r.wage_bill = wage_bill;
	r.wage_employed_people = employed_total;
	r.wage_nominal_mean = employed_total > 0.0 ? wage_bill / employed_total : 0.0;
	r.real_wage_index = r.basket_cost_now > 0.0 ? r.wage_nominal_mean / r.basket_cost_now : 0.0;
	r.real_wage_realized = r.real_wage_index * r.basket_fill;
	// The split is a subtraction from the very double that feeds old column 42, not a second
	// accumulation of the people part, so columns 65 + 66 == 42 (invariant I1, design §3.3).
	r.labor_demand_capacity = capacity_total;
	r.labor_demand_people = labor_demand_field - capacity_total;
	// Column 36: mean over ALL markets, no "price > 0" sample filter. The old column mixed the naval
	// sample count into this slot; averaging every market instead makes the structural zero show up as
	// a zero instead of masquerading as a count (design §3.4).
	r.land_transport_price = markets > 0 ? land_sum / double(markets) : 0.0;
	r.land_transport_price_max = land_max;
	r.naval_transport_price_samples = naval_samples;
	r.pool_total_revalued = market_money + pop_savings;
	r.pool_total_unrevalued = treasury + government_bank + gov_escrow + rgo_bank + factory_bank
			+ rent_bank + pop_investments + pop_dividends + private_inv + lender_balance;
	// Column 71 is a MAGNITUDE share. Market cash and savings can both go negative (unfunded
	// market debits), which took the signed ratio outside [0,1] and fired 102 false invariant
	// violations in a 10-year run -- a gate that always fires is a gate nobody reads. The
	// magnitude share is always defined and still answers "how much of the money stock sits in
	// inflation-rescaled pools"; the signs live in columns 69/70.
	double const revalued_abs = std::fabs(r.pool_total_revalued);
	double const unrevalued_abs = std::fabs(r.pool_total_unrevalued);
	r.pool_revalued_share = (revalued_abs + unrevalued_abs > 0.0)
			? revalued_abs / (revalued_abs + unrevalued_abs) : 0.0;
	r.inflation = double(state.inflation);
	return r;
}

// ---- summary.csv schema: 72 columns = 49 inherited + 23 appended. The count is enforced at compile
// time (design E6) so a header/argument drift fails the build instead of shifting every field left,
// which is what happened to welfare.csv (see the LOCAL FIX note below). Column 36 keeps its name and
// changes content only. The row format string stays a literal at its fprintf on purpose: that keeps
// the compiler's own format/argument checking alive.
inline constexpr char const* summary_header =
		"date,population,satisfaction,sat_poor,sat_middle,sat_rich,factories,capacity,fill_ratio,builds,"
		"gdp_real,supply_total,demand_total,coverage,market_money,markets,routes,sea_routes,"
		"treasury,private_investment,army_demand,gov_construction_demand,builds_clothes,builds_fabric,pop_savings,market_pop_investments,market_pop_dividends,rgo_bank,factory_bank,rent_bank,government_bank,total_loans,subsidy_spending,trade_volume,naval_transport_price,land_transport_price,naval_transport_demand,land_transport_demand,rgo_employment,factory_employment,labor_supply,labor_demand,factory_hiring_budget,factory_output_value,factory_input_cost,factory_wage_bill,trade_house_debt,net_money,foreign_settlement"
		",gdp_nominal,gdp_engine_registered,price_level_paasche,gdp_deflated,gdp_in_baskets,price_coverage_share,basket_cost_now,basket_cost_base,cpi_basket,basket_fill,wage_bill,wage_employed_people,wage_nominal_mean,real_wage_index,real_wage_realized,labor_demand_people,labor_demand_capacity,land_transport_price_max,naval_transport_price_samples,pool_total_revalued,pool_total_unrevalued,pool_revalued_share,inflation";
inline constexpr int count_summary_columns(char const* s) {
	int n = 1;
	for(; *s; ++s)
		if(*s == ',') ++n;
	return n;
}
static_assert(count_summary_columns(summary_header) == 72,
		"summary.csv header column count drift (expected 72 = 49 inherited + 23 measurement columns)");

inline void dump_month(sys::state& state, FILE* nations_csv, FILE* prices_csv, FILE* provinces_csv, FILE* markets_csv, FILE* welfare_csv, FILE* summary_csv, std::string const& date) {
	if(summary_csv) {
		double pop_total = 0, sat_weighted = 0;
		double sat_sum[3] = {0,0,0}, pop_sum[3] = {0,0,0};
		state.world.for_each_pop([&](dcon::pop_id p) {
			auto pt = state.world.pop_get_poptype(p).id;
			auto size = double(state.world.pop_get_size(p));
			auto sat = double(state.world.pop_get_satisfaction(p));
			pop_total += size;
			sat_weighted += size * sat;
			int st = int(state.world.pop_type_get_strata(pt));
			if(st >= 0 && st < 3) { pop_sum[st] += size; sat_sum[st] += size * sat; }
		});
		double market_money = 0, supply_total = 0, demand_total = 0, coverage = 0;
		int markets = 0, routes = 0, sea_routes = 0;
		double pop_savings_total = 0, pop_investments = 0, pop_dividends = 0;
		double rgo_bank = 0, factory_bank = 0, rent_bank = 0, government_bank = 0, loan_total = 0, subsidy_spend = 0;
		state.world.for_each_market([&](dcon::market_id m) {
			market_money += double(state.world.market_get_stockpile(m, economy::money));
			pop_investments += double(state.world.market_get_pop_investments(m));
			pop_dividends += double(state.world.market_get_pop_dividends(m));
			++markets;
			double sup = 0, dem = 0;
			state.world.for_each_commodity([&](dcon::commodity_id c) {
				if(c == economy::money) return;
				sup += double(state.world.market_get_supply(m, c));
				dem += double(state.world.market_get_demand(m, c));
			});
			supply_total += sup;
			demand_total += dem;
			coverage += (sup > 1e-9 ? 1.0 : 0.0);
		});
		state.world.for_each_trade_route([&](dcon::trade_route_id r) {
			if(!state.world.trade_route_get_target(r)) return; // unused slots in the dense array
			++routes;
			if(state.world.trade_route_get_is_sea_route(r)) ++sea_routes;
		});
		double gdp_real = 0, treasury = 0, private_inv = 0, army_dem = 0, gov_dem = 0;
		double trade_volume = 0, naval_price = 0, naval_price_n = 0, naval_demand = 0, land_demand = 0;
		double rgo_emp = 0, fac_emp = 0, labor_sup = 0, labor_dem = 0;
		int32_t labor_sup_nan = 0, labor_dem_nan = 0;
		double labor_dem_by_type[5] = {0,0,0,0,0};
		double fac_budget = 0, fac_output_value = 0, fac_input_cost = 0, fac_wage = 0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(c == economy::money) return;
			double s = 0;
			state.world.for_each_market([&](dcon::market_id m) {
				s += double(state.world.market_get_supply(m, c));
				army_dem += double(state.world.market_get_army_demand(m, c));
				gov_dem += double(state.world.market_get_government_construction_demand(m, c));
			});
			gdp_real += s * double(state.world.commodity_get_cost(c));
		});
		state.world.for_each_nation([&](dcon::nation_id n) {
			if(state.world.nation_get_owned_province_count(n) == 0) return;
			treasury += double(state.world.nation_get_treasury(n));
			private_inv += double(state.world.nation_get_private_investment(n));
			government_bank += double(state.world.nation_get_national_bank(n));
			loan_total += double(state.world.nation_get_local_loan(n));
			subsidy_spend += double(state.world.nation_get_subsidy_token_total(n)) * double(state.world.nation_get_subsidy_token_price(n));
		});
		state.world.for_each_pop([&](dcon::pop_id p) {
			pop_savings_total += double(state.world.pop_get_savings(p));
		});
		double const trade_house_debt_value = economy::trade_credit_total();
		double const trade_credit_lender_value = economy::trade_credit_lender_balance();
		double const gov_escrow = economy::government_purchase_escrow_total();
		double const net_money = market_money + pop_savings_total + treasury + government_bank + gov_escrow - trade_house_debt_value;
		// LOCAL DIAGNOSTIC: daily money audit with two pool definitions. net_money matches the summary
		// column; wide adds the producer/investment pools so creation can be separated from transfers.
		double audit_market = market_money;
		double audit_savings = pop_savings_total;
		double audit_treasury = treasury;
		double audit_bank = government_bank;
		state.world.for_each_province([&](dcon::province_id p) {
			// Only land provinces carry labour data: the engine initialises and updates labour supply
			// through ve_for_each_land_province, so a sea province holds an uninitialised (NaN) value
			// and used to poison every per-province sum here.
			bool const land = province::is_land(state, p);
			rgo_bank += double(state.world.province_get_rgo_bank(p));
			factory_bank += double(state.world.province_get_factory_bank(p));
			rent_bank += double(state.world.province_get_advanced_province_building_private_savings(p, advanced_province_buildings::list::local_cities_and_towns));
			if(land) {
				for(int32_t l = 0; l < economy::labor::total; ++l) {
					auto dv2 = double(state.world.province_get_labor_demand(p, l));
					if(std::isfinite(dv2) && l < 5) labor_dem_by_type[l] += dv2;
					// province_get_labor_supply is already a head count (economy.cpp sets it to
					// population times the labour distribution). Multiplying by demographics::total
					// again, as this used to, produced ~3x the population and made the workforce look
					// 90% unemployed.
					auto sv = double(state.world.province_get_labor_supply(p, l));
					auto dv = double(state.world.province_get_labor_demand(p, l));
					if(!std::isfinite(sv)) { ++labor_sup_nan; }
					else { labor_sup += sv; }
					if(!std::isfinite(dv)) { ++labor_dem_nan; }
					else { labor_dem += dv; }
				}
			}
			state.world.for_each_commodity([&](dcon::commodity_id c) {
				rgo_emp += double(state.world.province_get_rgo_target_employment(p, c));
			});
		});
		// LOCAL DIAGNOSTIC (not upstream): the producer-bank sums above must be populated before the
		// audit line is emitted. Printing first made rgo/factory/rent always zero and silently omitted
		// these pools from wide, so money creation could not be separated from transfers.
		double const audit_rgo = rgo_bank;
		double const audit_factory = factory_bank;
		double const audit_rent = rent_bank;
		double const audit_pop_inv = pop_investments;
		double const audit_pop_div = pop_dividends;
		double const audit_private_inv = private_inv;
		double const audit_wide = audit_market + audit_savings + audit_treasury + audit_bank + gov_escrow
			+ audit_rgo + audit_factory + audit_rent + audit_pop_inv + audit_pop_div + audit_private_inv
			+ trade_credit_lender_value;
		fprintf(stderr, "MONEYAUDIT %s net_money=%.6f wide=%.6f market=%.6f savings=%.6f treasury=%.6f bank=%.6f escrow=%.6f rgo=%.6f factory=%.6f rent=%.6f popinv=%.6f popdiv=%.6f private=%.6f lender=%.6f\n",
			date.c_str(), net_money, audit_wide, audit_market, audit_savings, audit_treasury, audit_bank,
			gov_escrow, audit_rgo, audit_factory, audit_rent, audit_pop_inv, audit_pop_div, audit_private_inv,
			trade_credit_lender_value);
		state.world.for_each_market([&](dcon::market_id m) {
			naval_demand += double(state.world.market_get_naval_transportation_demand(m));
			land_demand += double(state.world.market_get_land_transportation_demand(m));
			auto np = double(state.world.market_get_naval_transportation_price(m));
			if(np > 0.f) { naval_price += np; naval_price_n++; }
		});
		state.world.for_each_trade_route([&](dcon::trade_route_id r) {
			if(!state.world.trade_route_get_target(r)) return;
			state.world.for_each_commodity([&](dcon::commodity_id c) {
				if(c == economy::money) return;
				trade_volume += double(state.world.trade_route_get_volume(r, c));
			});
		});
		state.world.for_each_factory([&](dcon::factory_id f) {
			fac_emp += double(state.world.factory_get_primary_employment(f));
			auto ftid = state.world.factory_get_building_type(f);
			auto oc = state.world.factory_type_get_output(ftid).id;
			if(!oc) return;
			auto prov = state.world.factory_get_province_from_factory_location(f);
			auto sid = state.world.province_get_state_membership(prov);
			if(!sid) return;
			auto mkt = state.world.state_instance_get_market_from_local_market(sid);
			if(!mkt) return;
			// the engine caps factory hiring by this budget (economy_production.cpp)
			auto budget = economy::factory_profit_to_wage_bound * state.world.factory_get_size(f)
				* state.world.factory_get_output_per_worker(f)
				* state.world.market_get_price(mkt, oc);
			fac_budget += double(budget);
			fac_output_value += double(state.world.factory_get_output(f) * state.world.market_get_price(mkt, oc));
			fac_input_cost += double(state.world.factory_get_input_cost(f));
			fac_wage += double(state.world.factory_get_primary_employment(f)
				* state.world.province_get_labor_price(prov, economy::labor::basic_education));
		});
		int factories = 0, capacity = 0;
		state.world.for_each_factory([&](dcon::factory_id f) { ++factories; });
		for(int32_t i = 0; i < state.province_definitions.first_sea_province.index(); ++i) {
			dcon::province_id p{dcon::province_id::value_base_t(i)};
			auto urban = state.world.province_get_advanced_province_building_max_private_size(
				p, advanced_province_buildings::list::local_cities_and_towns);
			capacity += int(state.defines.factories_per_state * urban / economy::factories_per_state_required_city_size);
		}
		double builds = 0, builds_clothes = 0, builds_fabric = 0;
		{
			static dcon::commodity_id cl{}, fb{};
			if(!cl) {
				state.world.for_each_commodity([&](dcon::commodity_id c) {
					auto n = text::produce_simple_string(state, state.world.commodity_get_name(c));
					if(n == "Regular Clothes") cl = c;
					if(n == "Fabric") fb = c;
				});
			}
			// LOCAL DIAGNOSTIC counters removed on the clean branch; keep the columns at zero.
		}
		// Observation sanity check. A model this complex is only as trustworthy as its
		// instrumentation, and two of these series were silently mis-scaled once already:
		// fill_ratio is factories/capacity (a build-out ratio), not an activity level, and
		// labour_supply used to be a 0..1 sold ratio multiplied by population. Each violation
		// prints once per month with the metric name, so a dimensionally impossible number
		// announces itself instead of being read as a finding.
		{
			auto bad = [&](char const* what, double v) {
				fprintf(stderr, "SANITY %s warn=%s value=%.6g\n", date.c_str(), what, v);
			};
			if(!(pop_total > 0.0)) bad("population_nonpositive", pop_total);
			if(labor_sup > pop_total * 1.01) bad("labor_supply_exceeds_population", labor_sup);
			if(rgo_emp + fac_emp > labor_sup * 1.01 && labor_sup > 0.0)
				bad("employment_exceeds_labor_supply", rgo_emp + fac_emp);
			if(!std::isfinite(gdp_real)) bad("gdp_not_finite", gdp_real);
			if(!std::isfinite(labor_sup)) bad("labor_supply_not_finite", labor_sup);
			if(!std::isfinite(labor_dem)) bad("labor_demand_not_finite", labor_dem);
			if(labor_dem > labor_sup * 10.0 && labor_sup > 0.0)
				bad("labor_demand_over_10x_supply", labor_dem);
			// when the labour market blows up, say which education tier did it
			if(labor_sup > 0.0 && labor_dem > labor_sup * 10.0) {
				fprintf(stderr, "SANITY %s warn=labor_demand_split noedu=%.4g basic=%.4g high=%.4g guild=%.4g high_acc=%.4g\n",
					date.c_str(), labor_dem_by_type[0], labor_dem_by_type[1], labor_dem_by_type[2],
					labor_dem_by_type[3], labor_dem_by_type[4]);
			}
			if(demand_total > 0.0 && supply_total > 0.0 && supply_total / demand_total > 1000.0)
				bad("supply_over_demand_gt_1000x", supply_total / demand_total);
			if(demand_total > 0.0 && supply_total > 0.0 && demand_total / supply_total > 1000.0)
				bad("demand_over_supply_gt_1000x", demand_total / supply_total);
			if(labor_sup_nan > 0)
				bad("labor_supply_nan_slots", double(labor_sup_nan));
			if(labor_dem_nan > 0)
				bad("labor_demand_nan_slots", double(labor_dem_nan));
		}
		// Measurement layer (design E2). Called only after every input it needs is final -- labour
		// columns above, producer banks above, factory wage/employment above -- and before the row is
		// printed. Everything it does is read-only; see the guard comments on compute_measurement.
		auto const meas = compute_measurement(state, labor_dem, market_money, pop_savings_total, treasury,
				government_bank, gov_escrow, rgo_bank, factory_bank, rent_bank, pop_investments,
				pop_dividends, private_inv, trade_credit_lender_value, fac_wage, fac_emp);
		// Instrument self-check (design E7 + §7.3 invariants), stderr only: it must stay silent on a
		// healthy run, because harness.py records every "SANITY" line as a run violation.
		{
			auto bad = [&](char const* what, double v) {
				fprintf(stderr, "SANITY %s warn=%s value=%.6g\n", date.c_str(), what, v);
			};
			if(!std::isfinite(meas.gdp_nominal)) bad("gdp_nominal_not_finite", meas.gdp_nominal);
			if(!std::isfinite(meas.price_level_paasche)) bad("price_level_paasche_not_finite", meas.price_level_paasche);
			if(!std::isfinite(meas.cpi_basket)) bad("cpi_basket_not_finite", meas.cpi_basket);
			if(!std::isfinite(meas.real_wage_index)) bad("real_wage_index_not_finite", meas.real_wage_index);
			if(!std::isfinite(meas.wage_bill)) bad("wage_bill_not_finite", meas.wage_bill);
			// I1: the split must add back up to the old column. Relative tolerance, because (a - b) + b
			// is not an exact identity in IEEE-754 even for the magnitudes involved here.
			double const split_check = meas.labor_demand_people + meas.labor_demand_capacity;
			if(std::fabs(split_check - labor_dem) > 1e-12 * std::max(1.0, std::fabs(labor_dem)))
				bad("labor_demand_split_mismatch", split_check - labor_dem);
			// I8: the engine hard-zeroes land transport price every day (economy.cpp:8464), so any
			// non-zero reading here means the engine changed and both transport columns must be re-read.
			if(meas.land_transport_price_max != 0.0)
				bad("land_transport_price_max_nonzero", meas.land_transport_price_max);
			// I9: domain checks
			if(meas.basket_fill < -1e-6 || meas.basket_fill > 1.0 + 1e-6)
				bad("basket_fill_out_of_range", meas.basket_fill);
			if(meas.pool_revalued_share < -1e-6 || meas.pool_revalued_share > 1.0 + 1e-6)
				bad("pool_revalued_share_out_of_range", meas.pool_revalued_share);
			if(!(meas.inflation > 0.0)) bad("inflation_nonpositive", meas.inflation);
		}
		fprintf(summary_csv,
			"%s,%.6g,%.4g,%.4g,%.4g,%.4g,%.6g,%.6g,%.4g,%.6g,%.6g,%.6g,%.6g,%.4g,%.6g,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.0f,%.0f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.0f,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), pop_total, pop_total > 0 ? sat_weighted / pop_total : 0.0,
			pop_sum[0] > 0 ? sat_sum[0] / pop_sum[0] : 0.0,
			pop_sum[1] > 0 ? sat_sum[1] / pop_sum[1] : 0.0,
			pop_sum[2] > 0 ? sat_sum[2] / pop_sum[2] : 0.0,
			double(factories), double(capacity), capacity > 0 ? double(factories) / capacity : 0.0, builds,
			gdp_real, supply_total, demand_total, coverage / std::max(1, markets),
			market_money, markets, routes, sea_routes, treasury, private_inv, army_dem, gov_dem,
			builds_clothes, builds_fabric, pop_savings_total, pop_investments, pop_dividends, rgo_bank, factory_bank, rent_bank, government_bank, loan_total, subsidy_spend, trade_volume, naval_price_n > 0 ? naval_price / naval_price_n : 0.0, meas.land_transport_price, naval_demand, land_demand, rgo_emp, fac_emp, labor_sup, labor_dem, fac_budget, fac_output_value, fac_input_cost, fac_wage, trade_house_debt_value, net_money, double(economy::total_foreign_settlement()),
			meas.gdp_nominal, meas.gdp_engine_registered, meas.price_level_paasche, meas.gdp_deflated,
			meas.gdp_in_baskets, meas.price_coverage_share, meas.basket_cost_now, meas.basket_cost_base,
			meas.cpi_basket, meas.basket_fill, meas.wage_bill, meas.wage_employed_people,
			meas.wage_nominal_mean, meas.real_wage_index, meas.real_wage_realized, meas.labor_demand_people,
			meas.labor_demand_capacity, meas.land_transport_price_max, meas.naval_transport_price_samples, meas.pool_total_revalued,
			meas.pool_total_unrevalued, meas.pool_revalued_share, meas.inflation);
	}
	// total merchant fleet: transport capacity is what lets goods leave the market that produced them
	{
		double ships = 0.0;
		double sat_sum = 0.0;
		double earn_sum = 0.0;
		double poor_markets = 0.0;
		int markets = 0;
		state.world.for_each_market([&](dcon::market_id m) {
			for(uint32_t u = 2; u < state.military_definitions.unit_base_definitions.size(); ++u) {
				dcon::unit_type_id uid{dcon::unit_type_id::value_base_t(u)};
				ships += double(state.world.market_get_owned_ships(m, uid));
			}
			auto sat = state.world.market_get_naval_transportation_demand_satisfaction(m);
			sat_sum += double(sat);
			auto earn = double(state.world.market_get_total_earn(m));
			earn_sum += earn;
			if(sat < 0.9f)
				poor_markets += 1.0;
			++markets;
		});
		// Factory cap utilisation: is the urbanisation-based limit actually binding?
		{
			int provinces = 0, at_cap = 0, above = 0;
			double fill_sum = 0.0;
			double sum_n = 0.0, sum_cap = 0.0;
			for(int32_t i = 0; i < state.province_definitions.first_sea_province.index(); ++i) {
				dcon::province_id p{dcon::province_id::value_base_t(i)};
				int n = economy::province_factory_count(state, p);
				auto urban = state.world.province_get_advanced_province_building_max_private_size(
					p, advanced_province_buildings::list::local_cities_and_towns);
				int cap = int(state.defines.factories_per_state * urban / economy::factories_per_state_required_city_size);
				++provinces;
				sum_n += n;
				sum_cap += cap;
				if(cap > 0) {
					fill_sum += double(n) / double(cap);
					if(n >= cap) ++at_cap;
					if(n > cap) ++above;
				} else if(n > 0) {
					fill_sum += 1.0;
					++at_cap;
				}
			}
			fprintf(stderr, "FCAP %s provinces=%d at_or_over_cap=%d over=%d mean_fill=%.3f factories=%.0f caps=%.0f\n",
				date.c_str(), provinces, at_cap, above, provinces ? fill_sum / provinces : 0.0, sum_n, sum_cap);
		}
		// mechanism-level counters are local diagnostics; the clean branch has no such switches.
		double fout = 0.0;
		int fcount = 0;
		state.world.for_each_factory([&](dcon::factory_id f) {
			auto v = state.world.factory_get_output(f);
			if(v > 0.f) {
				fout += double(v);
				++fcount;
			}
		});
		fprintf(stderr, "SHIPS %s ships=%.0f sat_mean=%.3f poor=%0.f/%d earn=%.0f factories=%d factory_output=%.0f\n",
			date.c_str(), ships, markets ? sat_sum / markets : 0.0, poor_markets, markets, earn_sum, fcount, fout);
	}
	// coverage probe for one representative final-consumption good: among the markets that cannot buy any of
	// it, how many have an incoming route from a market that *does* produce it?
	{
		dcon::commodity_id probe{};
		bool found = false;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(!found && text::produce_simple_string(state, state.world.commodity_get_name(c)) == "Regular Clothes") {
				probe = c;
				found = true;
			}
		});
		if(found) {
			int total = 0, with_supply = 0, zero_with_route = 0, zero_without_route = 0;
			state.world.for_each_market([&](dcon::market_id m) {
				++total;
				if(state.world.market_get_supply(m, probe) > 1e-9f) {
					++with_supply;
					return;
				}
				bool linked = false;
				state.world.market_for_each_trade_route_as_target(m, [&](auto r) {
					auto from = state.world.trade_route_get_origin(r);
					if(state.world.market_get_supply(from, probe) > 1e-9f)
						linked = true;
				});
				if(linked)
					++zero_with_route;
				else
					++zero_without_route;
			});
			fprintf(stderr, "PROBE %s total=%d with_supply=%d zero_with_producer_route=%d zero_without=%d\n",
				date.c_str(), total, with_supply, zero_with_route, zero_without_route);

			// Cross-Pacific connectivity: routes whose two ends sit in Asia and the Americas. The
			// per-market route counts cannot answer this because they do not record where a route goes.
			{
				int pacific = 0, transpacific_sea = 0;
				state.world.for_each_trade_route([&](dcon::trade_route_id tr) {
					auto tgt = state.world.trade_route_get_target(tr);
					if(!tgt) return;
					auto org = state.world.trade_route_get_origin(tr);
					auto oc = state.world.province_get_continent(state.world.state_instance_get_capital(state.world.market_get_zone_from_local_market(org)));
					auto tc = state.world.province_get_continent(state.world.state_instance_get_capital(state.world.market_get_zone_from_local_market(tgt)));
					auto is_asia = [&](dcon::modifier_id c) { return c == state.province_definitions.asia; };
					auto is_am = [&](dcon::modifier_id c) { return c == state.province_definitions.north_america || c == state.province_definitions.south_america; };
					if((is_asia(oc) && is_am(tc)) || (is_am(oc) && is_asia(tc))) {
						++pacific;
						if(state.world.trade_route_get_is_sea_route(tr)) ++transpacific_sea;
					}
				});
				fprintf(stderr, "PACIFIC %s asia_america_routes=%d of_which_sea=%d\n", date.c_str(), pacific, transpacific_sea);
			}

			// Is a market's price the ratio of the money held there to the goods available there?
			// Fit log(price * supply) = a + b * log(money) over markets that have both.
			double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
			int n = 0;
			double price_med = 0;
			{
				std::vector<double> prices;
				state.world.for_each_market([&](dcon::market_id m) {
					auto sup = double(state.world.market_get_supply(m, probe));
					auto mon = double(state.world.market_get_stockpile(m, economy::money));
					auto pr = double(state.world.market_get_price(m, probe));
					if(pr > 0)
						prices.push_back(pr);
					if(sup > 1e-6 && mon > 1e-6 && pr > 0) {
						double x = std::log(mon), y = std::log(pr * sup);
						sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; ++n;
					}
				});
				if(!prices.empty()) {
					std::sort(prices.begin(), prices.end());
					price_med = prices[prices.size() / 2];
				}
			}
			double slope = 0, r = 0;
			if(n > 2) {
				double den = n * sxx - sx * sx;
				if(den != 0) slope = (n * sxy - sx * sy) / den;
				double dy = n * syy - sy * sy, dx = n * sxx - sx * sx;
				if(dx > 0 && dy > 0) r = (n * sxy - sx * sy) / std::sqrt(dx * dy);
			}
			// decomposition of demand for the probe commodity, to find the component that does not shrink with price
			{
				double tot = 0, army = 0, gc = 0, gs = 0, inter = 0;
				state.world.for_each_market([&](dcon::market_id m) {
					tot += double(state.world.market_get_demand(m, probe));
					army += double(state.world.market_get_army_demand(m, probe));
					gc += double(state.world.market_get_government_construction_demand(m, probe));
					gs += double(state.world.market_get_government_stockpile_demand(m, probe));
					inter += double(state.world.market_get_intermediate_demand(m, probe));
				});
				double pops = tot - army - gc - gs - inter;
				fprintf(stderr, "DEMAND %s total=%.4g pops=%.4g army=%.4g govcon=%.4g govstock=%.4g inter=%.4g\n",
					date.c_str(), tot, pops, army, gc, gs, inter);
			}

			// what the price update actually sees: the per-market smoothed supply/demand ratio
			std::vector<double> ratios;
			state.world.for_each_market([&](dcon::market_id m) {
				auto s = double(state.world.market_get_aggregated_supply_history(m, probe));
				auto d = double(state.world.market_get_aggregated_demand_history(m, probe));
				if(d > 1e-9)
					ratios.push_back(s / d);
			});
			double med_ratio = 0, share_below = 0;
			if(!ratios.empty()) {
				std::sort(ratios.begin(), ratios.end());
				med_ratio = ratios[ratios.size() / 2];
				size_t below = 0;
				for(auto v : ratios) if(v < 1.0) ++below;
				share_below = double(below) / double(ratios.size());
			}
			fprintf(stderr, "FIT %s n=%d slope=%.3f r=%.3f price_median=%.6g sd_median=%.4f sd_below1=%.3f\n",
				date.c_str(), n, slope, r, price_med, med_ratio, share_below);
		}
	}
	// province index is Alice's own (land provinces first), not the Vic2 definition.csv number.
	// The per-province detail columns exist because questions like "this gold mine has no workers"
	// cannot be answered from a global total: they need each province's labour supply against what
	// the RGO and its factories actually employ, broken down by education tier, plus the province's
	// dominant RGO commodity.
	for(int32_t i = 0; i < state.province_definitions.first_sea_province.index(); ++i) {
		dcon::province_id p{dcon::province_id::value_base_t(i)};
		// dominant RGO commodity for this province (largest rgo size, 0 if none)
		dcon::commodity_id best_c{};
		float best_size = 0.f;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			auto s = state.world.province_get_rgo_size(p, c);
			if(s > best_size) { best_size = s; best_c = c; }
		});
		float fac_emp = 0.f;
		float fac_cap = 0.f;
		for(auto f : state.world.province_get_factory_location(p)) {
			auto fac = f.get_factory();
			fac_emp += state.world.factory_get_unqualified_employment(fac)
				+ state.world.factory_get_primary_employment(fac)
				+ state.world.factory_get_secondary_employment(fac);
			fac_cap += state.world.factory_get_size(fac);
		}
		auto labor_sat = double(state.world.province_get_labor_demand_satisfaction(p, economy::labor::no_education));
		auto rgo_target = double(state.world.province_get_rgo_target_employment(p, best_c));
		fprintf(provinces_csv, "%s,%d,%s,%s,%s,%.6g,%s,%.6g,%.6g,%.6g,%.6g,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), i,
			text::produce_simple_string(state, state.world.province_get_name(p)).c_str(),
			tag_of(state, state.world.province_get_nation_from_province_ownership(p)).c_str(),
			tag_of(state, state.world.province_get_nation_from_province_control(p)).c_str(),
			state.world.province_get_demographics(p, demographics::total),
			best_c ? text::produce_simple_string(state, state.world.commodity_get_name(best_c)).c_str() : "",
			double(best_size),
			rgo_target,
			rgo_target * labor_sat,
			double(state.world.province_get_rgo_output(p, best_c)),
			([&]() { auto mc = state.world.province_get_rgo(p); return mc ? text::produce_simple_string(state, state.world.commodity_get_name(mc)) : std::string{}; })().c_str(),
			([&]() { auto mc = state.world.province_get_rgo(p); return mc ? double(state.world.province_get_rgo_size(p, mc)) : 0.0; })(),
			([&]() { auto mc = state.world.province_get_rgo(p); return mc ? double(state.world.province_get_rgo_target_employment(p, mc)) : 0.0; })(),
			([&]() { auto mc = state.world.province_get_rgo(p); auto ls = double(state.world.province_get_labor_demand_satisfaction(p, economy::labor::no_education)); return mc ? double(state.world.province_get_rgo_target_employment(p, mc)) * ls : 0.0; })(),
			labor_sat,
			double(fac_emp), double(fac_cap),
			double(state.world.province_get_labor_supply(p, economy::labor::no_education)),
			double(state.world.province_get_labor_supply(p, economy::labor::basic_education)),
			double(state.world.province_get_labor_supply(p, economy::labor::high_education)),
			double(state.world.province_get_labor_demand(p, economy::labor::no_education)),
			double(state.world.province_get_labor_demand(p, economy::labor::basic_education)),
			// Measured, not inferred: factory capacity is derived from this number, so it is the
			// quantity that decides how much industry a province can ever hold.
			double(state.world.province_get_advanced_province_building_private_size(
				p, advanced_province_buildings::list::local_cities_and_towns)),
			double(state.world.province_get_labor_demand(p, economy::labor::no_education) <= 0.f ? 1.0 : 0.0),
			double(state.world.province_get_labor_supply(p, economy::labor::high_education_and_accepted)),
			double(state.world.province_get_labor_demand(p, economy::labor::high_education_and_accepted)),
			double(state.world.province_get_labor_price(p, economy::labor::high_education_and_accepted)),
			// The value the province panel prints as "administrative efficiency" (gui_province_window
			// reads province_get_control_ratio). Added 2026-11-13 because the panel read 0.1%
			// everywhere in a running game and there was no column to verify a fix against.
			double(state.world.province_get_control_ratio(p)));
	}
	// routes touching each market, so the dump shows where goods can actually move
	static std::vector<int32_t> route_count;
	route_count.assign(size_t(state.world.market_size()), 0);
	state.world.for_each_trade_route([&](dcon::trade_route_id r) {
		auto ro = state.world.trade_route_get_origin(r);
		auto rt = state.world.trade_route_get_target(r);
		if(ro) route_count[size_t(ro.index())] += 1;
		if(rt && rt != ro) route_count[size_t(rt.index())] += 1;
	});
	// per-market snapshot: what each local market supplies, demands and prices
	dcon::commodity_id clothes_good = find_good(state, "Regular Clothes");
	dcon::commodity_id ammo_good = find_good(state, "Ammunition");
	dcon::commodity_id grain_good = find_good(state, "Grain");
	dcon::commodity_id fabric_good = find_good(state, "Fabric");
	state.world.for_each_market([&](dcon::market_id m) {
		auto sid = state.world.market_get_zone_from_local_market(m);
		auto owner = state.world.state_instance_get_nation_from_state_ownership(sid);
		double supply = 0, demand = 0, wealth = 0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			supply += double(state.world.market_get_supply(m, c));
			demand += double(state.world.market_get_demand(m, c));
			wealth += double(state.world.market_get_price(m, c) * state.world.market_get_supply(m, c));
		});
		double money = double(state.world.market_get_stockpile(m, economy::money));
		auto capital = state.world.state_instance_get_capital(sid);
		auto cont = capital ? state.world.province_get_continent(capital) : dcon::modifier_id{};
		auto prov_idx = capital ? int32_t(capital.id.value) : -1;
		fprintf(markets_csv, "%s,%d,%s,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), m.value,
			tag_of(state, owner).c_str(), owner ? owner.value : -1, prov_idx, int32_t(cont.value),
			route_count[size_t(m.index())], supply, demand, wealth, money,
			clothes_good ? double(state.world.market_get_price(m, clothes_good)) : 0.0,
			clothes_good ? double(state.world.market_get_supply(m, clothes_good)) : 0.0,
			clothes_good ? double(state.world.market_get_demand(m, clothes_good)) : 0.0,
			ammo_good ? double(state.world.market_get_price(m, ammo_good)) : 0.0,
			ammo_good ? double(state.world.market_get_supply(m, ammo_good)) : 0.0,
			ammo_good ? double(state.world.market_get_demand(m, ammo_good)) : 0.0,
			double(state.world.market_get_naval_transportation_supply(m)),
			double(state.world.market_get_naval_transportation_demand(m)),
			double(state.world.market_get_naval_transportation_demand_satisfaction(m)),
			double(state.world.market_get_trade_house_budget_import_scale(m)),
			double(state.world.market_get_trade_house_budget_scale(m)),
			double(state.world.market_get_total_earn(m)),
			state.world.market_get_naval_transportation_demand_satisfaction(m) > 0.f ? 1 : 0,
			0,
			grain_good ? double(state.world.market_get_price(m, grain_good)) : 0.0,
			grain_good ? double(state.world.market_get_supply(m, grain_good)) : 0.0,
			grain_good ? double(state.world.market_get_demand(m, grain_good)) : 0.0,
			fabric_good ? double(state.world.market_get_price(m, fabric_good)) : 0.0,
			fabric_good ? double(state.world.market_get_supply(m, fabric_good)) : 0.0,
			fabric_good ? double(state.world.market_get_demand(m, fabric_good)) : 0.0);
	});

	// Daily cost of army goods at each nation's local market prices. The army demand is stored per
	// market, so attribute it to the controller of the market's capital.
	std::vector<double> army_goods_cost(size_t(state.world.nation_size()), 0.0);
	state.world.for_each_market([&](dcon::market_id m) {
		auto sid = state.world.market_get_zone_from_local_market(m);
		auto capital = state.world.state_instance_get_capital(sid);
		auto controller = capital ? state.world.province_get_nation_from_province_control(capital) : dcon::nation_id{};
		if(!controller) return;
		double cost = 0.0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			cost += double(state.world.market_get_army_demand(m, c)) * double(state.world.market_get_price(m, c));
		});
		army_goods_cost[size_t(controller.index())] += cost;
	});

	state.world.for_each_nation([&](dcon::nation_id n) {
		if(state.world.nation_get_owned_province_count(n) == 0)
			return;
		int unlocked = 0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(state.world.nation_get_unlocked_commodities(n, c))
				++unlocked;
		});
		auto gov = state.world.nation_get_government_type(n);
		auto party = state.world.nation_get_ruling_party(n);
		auto sphere = state.world.nation_get_in_sphere_of(n);
		auto gov_name = csv_safe(gov ? text::produce_simple_string(state, state.world.government_type_get_name(gov.id)) : std::string{});
		auto party_name = csv_safe(party ? text::produce_simple_string(state, state.world.political_party_get_name(party.id)) : std::string{});
		// LOCAL DIAGNOSTIC: per-nation money breakdown (treasury/bank/market/pops/investment/rgo/
		// factory/artisans) so a nominal explosion can be attributed to a nation and a pool.
		auto mb = economy::breakdown_nation_monetary_structure(state, n);
		fprintf(nations_csv, "%s,%s,%.6g,%.6g,%.6g,%d,%d,%d,%.6g,%.6g,%d,%.6g,%.6g,%d,%d,%.6g,%s,%s,%d,%s,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n", date.c_str(), tag_of(state, n).c_str(),
				state.world.nation_get_prestige(n), float(state.world.nation_get_industrial_score(n)),
				float(state.world.nation_get_military_score(n)), int(state.world.nation_get_rank(n)),
				int(state.world.nation_get_is_great_power(n)), int(state.world.nation_get_is_civilized(n)),
				state.world.nation_get_treasury(n), state.world.nation_get_demographics(n, demographics::total),
				int(state.world.nation_get_owned_province_count(n)), state.world.nation_get_infamy(n),
				state.world.nation_get_war_exhaustion(n), unlocked,
				int(state.world.nation_get_military_spending(n)),
				army_goods_cost[size_t(n.index())],
				gov_name.c_str(), party_name.c_str(), int(state.world.nation_get_active_regiments(n)),
				tag_of(state, sphere ? sphere.id : dcon::nation_id{}).c_str(),
				int(state.world.nation_get_vassals_count(n)),
				double(mb.nation), double(mb.bank), double(mb.market), double(mb.pops),
				double(mb.investment_pool), double(mb.rgo), double(mb.factory), double(mb.artisans), double(mb.total));
	});
	if(welfare_csv) {
		auto const n_types = state.world.pop_type_size();
		std::vector<double> size(size_t(n_types), 0.0), sat(size_t(n_types), 0.0), sav(size_t(n_types), 0.0);
		std::vector<double> basket(size_t(n_types), 0.0), spent(size_t(n_types), 0.0), rich(size_t(n_types), 0.0), richw(size_t(n_types), 0.0);
		std::vector<double> lifer(size_t(n_types), 0.0), everyr(size_t(n_types), 0.0), luxr(size_t(n_types), 0.0);
		state.world.for_each_pop([&](dcon::pop_id p) {
			auto pt = state.world.pop_get_poptype(p).id;
			auto s = double(state.world.pop_get_size(p));
			size[pt.index()] += s;
			sat[pt.index()] += s * double(state.world.pop_get_satisfaction(p));
			sav[pt.index()] += s * double(state.world.pop_get_savings(p));
			// what this pop type's basket costs in its own market, and how much of it was actually bought
			auto prov = state.world.pop_get_province_from_pop_location(p);
			auto sid = state.world.province_get_state_membership(prov);
			if(!sid)
				return;
			auto mkt = state.world.state_instance_get_market_from_local_market(sid);
			if(!mkt)
				return;
			double ln = double(state.world.market_get_life_needs_costs(mkt, pt));
			double en = double(state.world.market_get_everyday_needs_costs(mkt, pt));
			double lx = double(state.world.market_get_luxury_needs_costs(mkt, pt));
			double ln_r = double(state.world.market_get_satisfied_ratio_of_max_life_needs(mkt, pt));
			double en_r = double(state.world.market_get_satisfied_ratio_of_max_everyday_needs(mkt, pt));
			double lx_r = double(state.world.market_get_satisfied_ratio_of_max_luxury_needs(mkt, pt));
			basket[pt.index()] += s * (ln + en + lx);
				auto tcn = 0.00001f + (ln + en + lx) * s / state.defines.alice_needs_scaling_factor;
				auto sv = double(state.world.pop_get_savings(p));
				auto r = std::min(3.f, std::max(0.f, float((sv - tcn) / tcn)));
				rich[pt.index()] += s * r;
				richw[pt.index()] += s;
			spent[pt.index()] += s * (ln * ln_r + en * en_r + lx * lx_r);
			lifer[pt.index()] += s * ln_r;
			everyr[pt.index()] += s * en_r;
			luxr[pt.index()] += s * lx_r;
		});
		for(size_t i = 0; i < size_t(n_types); ++i) {
			if(size[i] <= 0.0)
				continue;
			dcon::pop_type_id pt{dcon::pop_type_id::value_base_t(i)};
			// LOCAL FIX (not upstream) 2026-11-08: the header has 11 fields but the argument list
			// omitted mean_is_rich, so every row shifted left by one: life_sat held everyday, etc.,
			// and the last column printed a stale float. Emit rich[i]/size[i] in its slot.
			fprintf(welfare_csv, "%s,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n", date.c_str(),
				text::produce_simple_string(state, state.world.pop_type_get_name(pt)).c_str(),
				size[i], sat[i] / size[i], sav[i] / size[i], basket[i] / size[i], spent[i] / size[i],
				rich[i] / size[i], lifer[i] / size[i], everyr[i] / size[i], luxr[i] / size[i]);
		}
	}
	// factory output per commodity (one pass for the whole month)
	std::vector<double> fac_out(size_t(state.world.commodity_size()), 0.0);
	std::vector<int> fac_cnt(size_t(state.world.commodity_size()), 0);
	state.world.for_each_factory([&](dcon::factory_id f) {
		auto ftid = state.world.factory_get_building_type(f);
		dcon::commodity_id oc = state.world.factory_type_get_output(ftid).id;
		if(!oc)
			return;
		auto v = double(state.world.factory_get_output(f));
		fac_out[oc.index()] += v;
		if(v > 1e-9)
			fac_cnt[oc.index()] += 1;
	});

	state.world.for_each_commodity([&](dcon::commodity_id c) {
		if(c == economy::money)
			return;
		float supply = 0.f;
		float demand = 0.f;
		float stockpile = 0.f;
		float merchants = 0.f;
		float staple_consumption = 0.f;
		float imported_stock = 0.f;
		int markets_with_supply = 0;
		int markets_total = 0;
		{
			state.world.for_each_market([&](dcon::market_id m) {
				auto const s = state.world.market_get_supply(m, c);
				auto const st = state.world.market_get_stockpile(m, c);
				auto const me = state.world.market_get_stockpile_sales(m, c);
				supply += s;
				demand += state.world.market_get_demand(m, c);
				stockpile += st;
				merchants += me;
				// staple/import buffers are local diagnostics; left at zero on the clean branch.
				++markets_total;
				if(s > 1e-9f)
					++markets_with_supply;
			});
		}
		// army demand for this commodity (summed over markets) -- used to find accumulators that never reset
		double army_dem_of_good = 0.0;
		state.world.for_each_market([&](dcon::market_id m) {
			army_dem_of_good += double(state.world.market_get_army_demand(m, c));
		});
		// RGO output of this commodity (the third source of supply, next to artisans and factories)
		double rgo_out_of_good = 0.0;
		state.world.for_each_province([&](dcon::province_id p) {
			rgo_out_of_good += double(state.world.province_get_rgo_output(p, c));
		});
		// artisan allocation and the output it produces (per province, summed)
		float artisan_score = 0.f;
		float artisan_out = 0.f;
		int artisan_producers = 0;
		float artisan_max = 0.f;
		state.world.for_each_province([&](dcon::province_id p) {
			artisan_score += state.world.province_get_artisan_score(p, c);
			auto v = state.world.province_get_artisan_actual_production(p, c);
			artisan_out += v;
			if(v > 1e-9f) {
				++artisan_producers;
				artisan_max = std::max(artisan_max, v);
			}
		});
		// price = supply-weighted mean over the per-state markets; median_price = median over markets
		fprintf(prices_csv, "%s,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g\n", date.c_str(),
				text::produce_simple_string(state, state.world.commodity_get_name(c)).c_str(), economy::price_slow(state, c),
				economy::median_price_slow(state, c), supply, demand, stockpile, merchants, artisan_score, artisan_out,
				markets_with_supply, markets_total, artisan_producers, artisan_max, rgo_out_of_good,
				fac_out[c.index()], fac_cnt[c.index()], army_dem_of_good, staple_consumption,
				imported_stock, stockpile - imported_stock);
	});
}

// One row per live war. The engine stores participants per war; dumping the primary actors and
// counts is enough to see whether the 100-year run produces a plausible diplomatic history.
inline void dump_wars(sys::state& state, FILE* wars_csv, std::string const& date) {
	if(!wars_csv) return;
	state.world.for_each_war([&](dcon::war_id w) {
		auto pa = state.world.war_get_primary_attacker(w);
		auto pd = state.world.war_get_primary_defender(w);
		int attackers = 0, defenders = 0;
		state.world.war_for_each_war_participant_as_war(w, [&](auto wp) {
			if(state.world.war_participant_get_is_attacker(wp)) ++attackers; else ++defenders;
		});
		auto name = csv_safe(text::produce_simple_string(state, state.world.war_get_name(w)));
		auto start = state.world.war_get_start_date(w).to_ymd(state.start_date);
		fprintf(wars_csv, "%s,%d,%s,%s,%s,%s,%d,%d,%.6g,%.6g,%d\n",
			date.c_str(), w.value, name.c_str(), tag_of(state, pa.id).c_str(), tag_of(state, pd.id).c_str(),
			ymd_str(start).c_str(), attackers, defenders,
			double(state.world.war_get_attacker_battle_score(w)),
			double(state.world.war_get_defender_battle_score(w)),
			state.world.war_get_is_great(w) ? 1 : 0);
	});
}

// One row per live trade route. Written yearly (plus at start) because a monthly dump of ~12k
// routes is tens of megabytes and the interesting change is which routes exist and whether they
// carry anything, not the day-to-day jitter.
inline void dump_routes(sys::state& state, FILE* routes_csv, std::string const& date) {
	if(!routes_csv) return;
	dcon::commodity_id clothes_good = find_good(state, "Regular Clothes");
	dcon::commodity_id ammo_good = find_good(state, "Ammunition");
	state.world.for_each_trade_route([&](dcon::trade_route_id r) {
		auto org = state.world.trade_route_get_origin(r);
		auto tgt = state.world.trade_route_get_target(r);
		if(!org || !tgt) return;
		auto own = state.world.trade_route_get_owner(r);
		auto ownsid = own ? state.world.market_get_zone_from_local_market(own.id) : dcon::state_instance_id{};
		auto ownn = ownsid ? state.world.state_instance_get_nation_from_state_ownership(ownsid) : dcon::nation_id{};
		auto osid = state.world.market_get_zone_from_local_market(org);
		auto tsid = state.world.market_get_zone_from_local_market(tgt);
		auto on = osid ? state.world.state_instance_get_nation_from_state_ownership(osid) : dcon::nation_id{};
		auto tn = tsid ? state.world.state_instance_get_nation_from_state_ownership(tsid) : dcon::nation_id{};
		auto ocap = osid ? state.world.state_instance_get_capital(osid) : dcon::province_id{};
		auto tcap = tsid ? state.world.state_instance_get_capital(tsid) : dcon::province_id{};
		auto ocont = ocap ? state.world.province_get_continent(ocap) : dcon::modifier_id{};
		auto tcont = tcap ? state.world.province_get_continent(tcap) : dcon::modifier_id{};
		double total = 0.0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(c == economy::money) return;
			total += double(state.world.trade_route_get_volume(r, c));
		});
		fprintf(routes_csv, "%s,%d,%d,%d,%d,%s,%s,%s,%d,%.6g,%.6g,%.6g,%.6g,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), r.value, org.value, tgt.value, own ? own.id.value : -1,
			tag_of(state, on).c_str(), tag_of(state, tn).c_str(), tag_of(state, ownn).c_str(),
			state.world.trade_route_get_is_sea_route(r) ? 1 : 0,
			double(state.world.trade_route_get_distance_km(r)), total,
			clothes_good ? double(state.world.trade_route_get_volume(r, clothes_good)) : 0.0,
			ammo_good ? double(state.world.trade_route_get_volume(r, ammo_good)) : 0.0,
			int32_t(ocont.value), int32_t(tcont.value),
			clothes_good ? double(state.world.market_get_expected_probability_to_sell(tgt, clothes_good)) : 0.0,
			clothes_good ? double(state.world.market_get_expected_probability_to_buy(org, clothes_good)) : 0.0,
			clothes_good ? double(state.world.market_get_price(org, clothes_good)) : 0.0,
			clothes_good ? double(state.world.market_get_price(tgt, clothes_good)) : 0.0,
			double(state.world.market_get_naval_transportation_demand_satisfaction(org)),
			double(state.world.market_get_naval_transportation_demand_satisfaction(tgt)),
			double(state.world.market_get_total_earn(tgt)));
	});
}

inline void run(sys::state& state, int years, std::string const& dump_dir) {
	using clock = std::chrono::steady_clock;
	FILE* nations_csv = nullptr;
	FILE* welfare_csv = nullptr;
	FILE* summary_csv = nullptr;
	FILE* prices_csv = nullptr;
	FILE* provinces_csv = nullptr;
	FILE* markets_csv = nullptr;
	FILE* routes_csv = nullptr;
	FILE* wars_csv = nullptr;
	// LOCAL (ALICE_DUMP_MINIMAL=1): the per-province and per-route dumps are 66% of a run's bytes
	// (119 MB + 28 MB of 223 MB at twenty years), and a hundred-year run is the game's real
	// horizon. Sending just those two to /dev/null keeps a century at ~380 MB while everything the
	// gates read -- summary.csv, prices.csv, markets.csv, nations.csv -- is still written.
	auto const big_dump = [&](char const* name) {
		bool minimal = false;
		if(auto e = std::getenv("ALICE_DUMP_MINIMAL"); e && std::atoi(e) != 0)
			minimal = true;
		return minimal ? std::string("/dev/null") : (dump_dir + "/" + name);
	};
	if(!dump_dir.empty()) {
		std::filesystem::create_directories(dump_dir);
		nations_csv = fopen((dump_dir + "/nations.csv").c_str(), "w");
		prices_csv = fopen((dump_dir + "/prices.csv").c_str(), "w");
		provinces_csv = fopen(big_dump("provinces.csv").c_str(), "w");
		markets_csv = fopen((dump_dir + "/markets.csv").c_str(), "w");
		routes_csv = fopen(big_dump("routes.csv").c_str(), "w");
		wars_csv = fopen((dump_dir + "/wars.csv").c_str(), "w");
		factory_trace_csv = nullptr;
		construction_trace_csv = nullptr;
		seller_share_trace_csv = nullptr;
		if(std::getenv("ALICE_TRACE_SELLER_SHARE")) {
			seller_share_trace_csv = fopen((dump_dir + "/market_seller_share.csv").c_str(), "w");
			if(seller_share_trace_csv)
				fprintf(seller_share_trace_csv, "date,market,owner,commodity,rgo,guild,factories,total\n");
		}
		if(std::getenv("ALICE_TRACE_CONSTRUCTION")) {
			construction_trace_csv = fopen((dump_dir + "/construction_trace.csv").c_str(), "w");
			if(construction_trace_csv)
				fprintf(construction_trace_csv, "date,construction,owner,province,good,is_upgrade,is_pop_project,progress\n");
		}
		if(std::getenv("ALICE_TRACE_FACTORY")) {
			factory_trace_csv = fopen((dump_dir + "/factory_trace.csv").c_str(), "w");
			if(factory_trace_csv)
				fprintf(factory_trace_csv, "date,factory,owner,province_id,province,market_id,good,size,base_workforce,hiring_budget,affordable_employment,desired_employment,construction_cost_per_capacity,output_per_worker,output_price,input_cost_per_worker,wage_per_worker,profit,profit_per_capacity,actual_output_per_capacity,actual_probability_to_sell,employment_ratio,labor_satisfaction,hiring_sat,hiring_multiplier,min_expected_input,min_available_input,subsidy,tokens,expand_priority\n");
			factory_input_trace_csv = fopen((dump_dir + "/factory_input_trace.csv").c_str(), "w");
			if(factory_input_trace_csv)
				fprintf(factory_input_trace_csv, "date,factory,owner,province,output_good,input_good,amount,price,actual_buy_prob,expected_buy_prob,supply,demand,stockpile\n");
		}
		// Static province map, written once: map pixel -> province id (id 0 = sea/uncolonised).
		{
			FILE* map_file = fopen((dump_dir + "/province_map.bin").c_str(), "wb");
			if(map_file) {
				uint32_t sx = state.map_state.map_data.size_x;
				uint32_t sy = state.map_state.map_data.size_y;
				fwrite(&sx, sizeof(uint32_t), 1, map_file);
				fwrite(&sy, sizeof(uint32_t), 1, map_file);
				if(!state.map_state.map_data.province_id_map.empty())
					fwrite(state.map_state.map_data.province_id_map.data(), sizeof(uint16_t),
						state.map_state.map_data.province_id_map.size(), map_file);
				fclose(map_file);
			}
		}
		welfare_csv = fopen((dump_dir + "/welfare.csv").c_str(), "w");
		summary_csv = fopen((dump_dir + "/summary.csv").c_str(), "w");
		if(summary_csv)
			fprintf(summary_csv, "%s\n", summary_header);
		if(welfare_csv)
			fprintf(welfare_csv, "date,pop_type,population,mean_satisfaction,savings_per_capita,basket_cost_per_capita,spent_per_capita,mean_is_rich,life_sat,everyday_sat,luxury_sat\n");
		if(provinces_csv)
			fprintf(provinces_csv, "date,province,name,owner,controller,population,rgo_good,rgo_size,rgo_target_employment,rgo_actual_employment,rgo_output,main_rgo,main_rgo_size,main_rgo_target,main_rgo_actual,labor_demand_satisfaction,factory_employment,factory_capacity,labor_supply_no_edu,labor_supply_basic,labor_supply_high,labor_demand_no_edu,labor_demand_basic,city_size,zero_demand,labor_supply_high_acc,labor_demand_high_acc,labor_price_high_acc,control_ratio\n");
		if(nations_csv)
			fprintf(nations_csv, "date,tag,prestige,industrial_score,military_score,rank,is_gp,is_civilized,treasury,population,provinces,infamy,war_exhaustion,unlocked_commodities,military_spending,army_goods_cost,government,ruling_party,active_regiments,sphere_leader,vassals,nat_money,bank,market,pops,inv,rgo,factory,artisans,total\n");
		if(prices_csv)
			fprintf(prices_csv, "date,good,price,median_price,world_supply,world_demand,world_stockpile,merchant_sales,artisan_score,artisan_production,markets_with_supply,markets_total,artisan_producers,artisan_max,rgo_output,factory_output,factory_count,army_demand,staple_consumption,imported_stock,local_stock\n");
		if(markets_csv)
			fprintf(markets_csv, "date,market,owner_tag,owner_nation,province_index,continent,routes,supply,demand,supply_value,market_money,clothes_price,clothes_supply,clothes_demand,ammo_price,ammo_supply,ammo_demand,naval_supply,naval_demand,naval_satisfaction,trade_import_scale,trade_budget_scale,total_earn,has_port,naval_satisfied,grain_price,grain_supply,grain_demand,fabric_price,fabric_supply,fabric_demand\n");
		if(routes_csv)
			fprintf(routes_csv, "date,route,origin_market,target_market,owner_market,origin_nation,target_nation,owner_nation,is_sea,distance_km,total_volume,clothes_volume,ammo_volume,origin_continent,target_continent,tgt_sell_prob,org_buy_prob,org_clothes_price,tgt_clothes_price,org_naval_sat,tgt_naval_sat,tgt_earn\n");
		if(wars_csv)
			fprintf(wars_csv, "date,war,name,primary_attacker,primary_defender,start_date,attackers,defenders,attacker_score,defender_score,is_great\n");
	}

	auto start = state.current_date.to_ymd(state.start_date);
	int const end_year = start.year + years;
	int year = start.year;
	printf("HEADLESS start=%s years=%d seed=%u pops=%u\n", ymd_str(start).c_str(), years, state.game_seed, state.world.pop_size());
	fflush(stdout);
	if(nations_csv)
		dump_month(state, nations_csv, prices_csv, provinces_csv, markets_csv, welfare_csv, summary_csv, ymd_str(start));
	dump_factory_trace(state, ymd_str(start));
	dump_factory_input_trace(state, ymd_str(start));
	dump_seller_share_trace(state, ymd_str(start));
	dump_construction_trace(state, ymd_str(start));
	dump_routes(state, routes_csv, ymd_str(start));
	dump_wars(state, wars_csv, ymd_str(start));

	state.user_settings.autosaves = sys::autosave_frequency::none; // parallel runs would share one autosave slot set
	auto t_start = clock::now();
	auto t_year = t_start;
	while(years > 0) {
		command::execute_pending_commands(state);
		state.single_game_tick();
		auto ymd = state.current_date.to_ymd(state.start_date);
		if(ymd.day == 1 && nations_csv) {
			dump_month(state, nations_csv, prices_csv, provinces_csv, markets_csv, welfare_csv, summary_csv, ymd_str(ymd));
			dump_factory_trace(state, ymd_str(ymd));
			dump_factory_input_trace(state, ymd_str(ymd));
			dump_seller_share_trace(state, ymd_str(ymd));
			dump_construction_trace(state, ymd_str(ymd));
			fflush(nations_csv);
			fflush(prices_csv);
			fflush(provinces_csv);
			if(welfare_csv) fflush(welfare_csv);
			if(markets_csv) fflush(markets_csv);
			if(ymd.month == 1) {
				dump_routes(state, routes_csv, ymd_str(ymd));
				dump_wars(state, wars_csv, ymd_str(ymd));
			}
			if(routes_csv) fflush(routes_csv);
			if(wars_csv) fflush(wars_csv);
		}
		if(ymd.year != year) {
			auto now = clock::now();
			printf("YEAR %d secs=%.3f pops=%u wars=%u\n", year, std::chrono::duration<double>(now - t_year).count(),
					state.world.pop_size(), state.world.war_size());
			fflush(stdout);
			t_year = now;
			year = ymd.year;
		}
		if(ymd.year >= end_year)
			break;
	}
	printf("TOTAL years=%d secs=%.3f\n", years, std::chrono::duration<double>(clock::now() - t_start).count());
	fflush(stdout);
	if(nations_csv) fclose(nations_csv);
	if(prices_csv) fclose(prices_csv);
	if(provinces_csv) fclose(provinces_csv);
	if(markets_csv) fclose(markets_csv);
	if(routes_csv) fclose(routes_csv);
	if(wars_csv) fclose(wars_csv);
	if(welfare_csv) fclose(welfare_csv);
	if(summary_csv) fclose(summary_csv);
	if(factory_trace_csv) fclose(factory_trace_csv);
	if(factory_input_trace_csv) fclose(factory_input_trace_csv);
	if(seller_share_trace_csv) fclose(seller_share_trace_csv);
	if(construction_trace_csv) fclose(construction_trace_csv);
}

} // namespace headless
