#pragma once
// -headless -years N: runs N game years with no speed cap, prints seconds per game year, and with -dump DIR
// writes monthly CSVs (nations.csv, prices.csv, provinces.csv) on the 1st of each month.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

// PATCHED: 上游新增的 -dump/-years 代码依赖下面两个头文件，但本头文件未包含，
// 单独编译 entry_point_nix.cpp 时会报 price_slow / execute_pending_commands 未声明。
#include "economy/economy_stats.hpp"
#include "economy/economy_production.hpp"
#include "economy/economy_constants.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "provinces/province.hpp"
#include "gamestate/commands.hpp"

namespace headless {

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
			auto& pb = economy::probe_build_count();
			for(size_t i = 0; i < pb.size(); ++i) builds += pb[i];
			if(cl) builds_clothes = pb[cl.index()];
			if(fb) builds_fabric = pb[fb.index()];
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
			if(demand_total > 0.0 && supply_total > 0.0 && supply_total / demand_total > 1000.0)
				bad("supply_over_demand_gt_1000x", supply_total / demand_total);
			if(demand_total > 0.0 && supply_total > 0.0 && demand_total / supply_total > 1000.0)
				bad("demand_over_supply_gt_1000x", demand_total / supply_total);
			if(labor_sup_nan > 0)
				bad("labor_supply_nan_slots", double(labor_sup_nan));
			if(labor_dem_nan > 0)
				bad("labor_demand_nan_slots", double(labor_dem_nan));
		}
		fprintf(summary_csv,
			"%s,%.6g,%.4g,%.4g,%.4g,%.4g,%.6g,%.6g,%.4g,%.6g,%.6g,%.6g,%.6g,%.4g,%.6g,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.0f,%.0f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), pop_total, pop_total > 0 ? sat_weighted / pop_total : 0.0,
			pop_sum[0] > 0 ? sat_sum[0] / pop_sum[0] : 0.0,
			pop_sum[1] > 0 ? sat_sum[1] / pop_sum[1] : 0.0,
			pop_sum[2] > 0 ? sat_sum[2] / pop_sum[2] : 0.0,
			double(factories), double(capacity), capacity > 0 ? double(factories) / capacity : 0.0, builds,
			gdp_real, supply_total, demand_total, coverage / std::max(1, markets),
			market_money, markets, routes, sea_routes, treasury, private_inv, army_dem, gov_dem,
			builds_clothes, builds_fabric, pop_savings_total, pop_investments, pop_dividends, rgo_bank, factory_bank, rent_bank, government_bank, loan_total, subsidy_spend, trade_volume, naval_price_n > 0 ? naval_price / naval_price_n : 0.0, naval_price_n, naval_demand, land_demand, rgo_emp, fac_emp, labor_sup, labor_dem, fac_budget, fac_output_value, fac_input_cost, fac_wage);
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
		// mechanism-level counters: what did the experimental switches actually do this month?
		{
			static dcon::commodity_id clothes_id{}, fabric_id{};
			if(!clothes_id) {
				state.world.for_each_commodity([&](dcon::commodity_id c) {
					auto n = text::produce_simple_string(state, state.world.commodity_get_name(c));
					if(n == "Regular Clothes") clothes_id = c;
					if(n == "Fabric") fabric_id = c;
				});
			}
			auto& pu = economy::probe_anchor_up();
			auto& pd = economy::probe_anchor_down();
			auto& pb = economy::probe_build_count();
			double up = 0, down = 0, builds = 0;
			for(size_t i = 0; i < pu.size(); ++i) { up += pu[i]; down += pd[i]; pu[i] = 0.0; pd[i] = 0.0; }
			for(size_t i = 0; i < pb.size(); ++i) { builds += pb[i]; }
			double b_clothes = clothes_id ? pb[clothes_id.index()] : 0.0;
			double b_fabric = fabric_id ? pb[fabric_id.index()] : 0.0;
			for(size_t i = 0; i < pb.size(); ++i) pb[i] = 0.0;
			fprintf(stderr, "MECH %s anchor_up=%.0f anchor_down=%.0f builds=%.0f builds_clothes=%.0f builds_fabric=%.0f\n",
				date.c_str(), up, down, builds, b_clothes, b_fabric);
		}
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
		fprintf(provinces_csv, "%s,%d,%s,%s,%.6g,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			date.c_str(), i,
			tag_of(state, state.world.province_get_nation_from_province_ownership(p)).c_str(),
			tag_of(state, state.world.province_get_nation_from_province_control(p)).c_str(),
			state.world.province_get_demographics(p, demographics::total),
			best_c ? text::produce_simple_string(state, state.world.commodity_get_name(best_c)).c_str() : "",
			double(best_size),
			double(state.world.province_get_rgo_target_employment(p, best_c)),
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
			double(state.world.province_get_labor_demand(p, economy::labor::no_education) <= 0.f ? 1.0 : 0.0));
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
		auto own = owner ? owner.value : -1;
		auto capital = state.world.state_instance_get_capital(sid);
		auto cont = capital ? state.world.province_get_continent(capital) : dcon::modifier_id{};
		auto prov_idx = capital ? int32_t(capital.id.value) : -1;
		fprintf(markets_csv, "%s,%d,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%d,%d\n", date.c_str(), m.value, own, prov_idx, int32_t(cont.value),
			route_count[size_t(m.index())], supply, demand, wealth, money,
			state.world.market_get_naval_transportation_demand_satisfaction(m) > 0.f ? 1 : 0,
			0);
	});

	state.world.for_each_nation([&](dcon::nation_id n) {
		if(state.world.nation_get_owned_province_count(n) == 0)
			return;
		int unlocked = 0;
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(state.world.nation_get_unlocked_commodities(n, c))
				++unlocked;
		});
		fprintf(nations_csv, "%s,%s,%.6g,%.6g,%.6g,%d,%d,%d,%.6g,%.6g,%d,%.6g,%.6g,%d\n", date.c_str(), tag_of(state, n).c_str(),
				state.world.nation_get_prestige(n), float(state.world.nation_get_industrial_score(n)),
				float(state.world.nation_get_military_score(n)), int(state.world.nation_get_rank(n)),
				int(state.world.nation_get_is_great_power(n)), int(state.world.nation_get_is_civilized(n)),
				state.world.nation_get_treasury(n), state.world.nation_get_demographics(n, demographics::total),
				int(state.world.nation_get_owned_province_count(n)), state.world.nation_get_infamy(n),
				state.world.nation_get_war_exhaustion(n), unlocked);
	});
	if(welfare_csv) {
		auto const n_types = state.world.pop_type_size();
		std::vector<double> size(size_t(n_types), 0.0), sat(size_t(n_types), 0.0), sav(size_t(n_types), 0.0);
		std::vector<double> basket(size_t(n_types), 0.0), spent(size_t(n_types), 0.0), rich(size_t(n_types), 0.0), richw(size_t(n_types), 0.0);
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
		});
		for(size_t i = 0; i < size_t(n_types); ++i) {
			if(size[i] <= 0.0)
				continue;
			dcon::pop_type_id pt{dcon::pop_type_id::value_base_t(i)};
			fprintf(welfare_csv, "%s,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n", date.c_str(),
				text::produce_simple_string(state, state.world.pop_type_get_name(pt)).c_str(),
				size[i], sat[i] / size[i], sav[i] / size[i], basket[i] / size[i], spent[i] / size[i]);
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
		int markets_with_supply = 0;
		int markets_total = 0;
		state.world.for_each_market([&](dcon::market_id m) {
			supply += state.world.market_get_supply(m, c);
			demand += state.world.market_get_demand(m, c);
			stockpile += state.world.market_get_stockpile(m, c);
			merchants += state.world.market_get_stockpile_sales(m, c);
			++markets_total;
			if(state.world.market_get_supply(m, c) > 1e-9f)
				++markets_with_supply;
		});
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
		fprintf(prices_csv, "%s,%s,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%.6g,%.6g,%.6g,%d,%.6g\n", date.c_str(),
				text::produce_simple_string(state, state.world.commodity_get_name(c)).c_str(), economy::price_slow(state, c),
				economy::median_price_slow(state, c), supply, demand, stockpile, merchants, artisan_score, artisan_out,
				markets_with_supply, markets_total, artisan_producers, artisan_max, rgo_out_of_good,
				fac_out[c.index()], fac_cnt[c.index()], army_dem_of_good);
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
	if(!dump_dir.empty()) {
		std::filesystem::create_directories(dump_dir);
		nations_csv = fopen((dump_dir + "/nations.csv").c_str(), "w");
		prices_csv = fopen((dump_dir + "/prices.csv").c_str(), "w");
		provinces_csv = fopen((dump_dir + "/provinces.csv").c_str(), "w");
	markets_csv = fopen((dump_dir + "/markets.csv").c_str(), "w");
	if(markets_csv != nullptr) fprintf(markets_csv, "date,market,owner_nation,province_index,continent,routes,supply,demand,supply_value,market_money,has_port,naval_satisfied\n");
		welfare_csv = fopen((dump_dir + "/welfare.csv").c_str(), "w");
		summary_csv = fopen((dump_dir + "/summary.csv").c_str(), "w");
		if(summary_csv)
			fprintf(summary_csv,
				"date,population,satisfaction,sat_poor,sat_middle,sat_rich,factories,capacity,fill_ratio,builds,"
				"gdp_real,supply_total,demand_total,coverage,market_money,markets,routes,sea_routes,"
				"treasury,private_investment,army_demand,gov_construction_demand,builds_clothes,builds_fabric,pop_savings,market_pop_investments,market_pop_dividends,rgo_bank,factory_bank,rent_bank,government_bank,total_loans,subsidy_spending,trade_volume,naval_transport_price,land_transport_price,naval_transport_demand,land_transport_demand,rgo_employment,factory_employment,labor_supply,labor_demand,factory_hiring_budget,factory_output_value,factory_input_cost,factory_wage_bill\n");
		if(welfare_csv)
			fprintf(welfare_csv, "date,pop_type,population,mean_satisfaction,savings_per_capita,basket_cost_per_capita,spent_per_capita,mean_is_rich\n");
		fprintf(provinces_csv, "date,province,owner,controller,population,rgo_good,rgo_size,rgo_target_employment,factory_employment,factory_capacity,labor_supply_no_edu,labor_supply_basic,labor_supply_high,labor_demand_no_edu,labor_demand_basic,city_size,zero_demand\n");
		fprintf(nations_csv, "date,tag,prestige,industrial_score,military_score,rank,is_gp,is_civilized,treasury,population,provinces,infamy,war_exhaustion,unlocked_commodities\n");
		fprintf(prices_csv, "date,good,price,median_price,world_supply,world_demand,world_stockpile,merchant_sales,artisan_score,artisan_production,markets_with_supply,markets_total,artisan_producers,artisan_max,rgo_output,factory_output,factory_count,army_demand\n");
	}

	auto start = state.current_date.to_ymd(state.start_date);
	int const end_year = start.year + years;
	int year = start.year;
	printf("HEADLESS start=%s years=%d seed=%u pops=%u\n", ymd_str(start).c_str(), years, state.game_seed, state.world.pop_size());
	fflush(stdout);
	if(nations_csv)
		dump_month(state, nations_csv, prices_csv, provinces_csv, markets_csv, welfare_csv, summary_csv, ymd_str(start));

	state.user_settings.autosaves = sys::autosave_frequency::none; // parallel runs would share one autosave slot set
	auto t_start = clock::now();
	auto t_year = t_start;
	while(years > 0) {
		command::execute_pending_commands(state);
		state.single_game_tick();
		auto ymd = state.current_date.to_ymd(state.start_date);
		if(ymd.day == 1 && nations_csv) {
			dump_month(state, nations_csv, prices_csv, provinces_csv, markets_csv, welfare_csv, summary_csv, ymd_str(ymd));
			fflush(nations_csv);
			if(welfare_csv)
				fflush(welfare_csv);
			fflush(prices_csv);
			fflush(provinces_csv);
	if(markets_csv != nullptr) fflush(markets_csv);
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
	if(nations_csv) {
		fclose(nations_csv);
		fclose(prices_csv);
		fclose(provinces_csv);
	if(markets_csv != nullptr) fclose(markets_csv);
	}
}

} // namespace headless
