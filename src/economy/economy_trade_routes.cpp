#include "economy_trade_routes.hpp"
#include "economy.hpp"
#include "economy_stats.hpp"
#include "system_state.hpp"
#include "economy_government.hpp"
#include "adaptive_ve.hpp"
#include "province_templates.hpp"
#include "advanced_province_buildings.hpp"
#include "economy_constants.hpp"
#include "economy_templates.hpp"
#include "money.hpp"
#include "province.hpp"
#include "price.hpp"
#include "terms.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

#ifdef NDEBUG
#ifdef _MSC_VER
#define DCON_RELEASE_INLINE __forceinline
#define DCON_RESTRICT __restrict
#else
#define DCON_RELEASE_INLINE inline __attribute__((always_inline))
#define DCON_RESTRICT __restrict__
#endif
#else
#define DCON_RELEASE_INLINE inline
#ifdef _MSC_VER
#define DCON_RESTRICT __restrict
#else
#define DCON_RESTRICT __restrict__
#endif
#endif
namespace adaptive_ve {

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto trade_route_get_volume(sys::state const& state, left_t route, right_t cid) {
	return state.world.trade_route_get_volume(route, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_trade_volume<left_t, right_t> trade_route_get_volume(sys::state const& state, left_t route, right_t cid)  {
	return { route, cid };
}

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_price(sys::state const& state, left_t market, right_t cid) {
	return state.world.market_get_price(market, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_price<left_t, right_t> market_get_price(sys::state const& state, left_t market, right_t cid) {
	return { market, cid };
}

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_expected_probability_to_sell(sys::state const& state, left_t market, right_t cid) {
	return state.world.market_get_expected_probability_to_sell(market, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_expected_probability_to_sell<left_t, right_t> market_get_expected_probability_to_sell(sys::state const& state, left_t market, right_t cid) {
	return { market, cid };
}

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_expected_probability_to_buy(sys::state const& state, left_t market, right_t cid) {
	return state.world.market_get_expected_probability_to_buy(market, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_expected_probability_to_buy<left_t, right_t> market_get_expected_probability_to_buy(sys::state const& state, left_t market, right_t cid) {
	return { market, cid };
}

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_aggregated_demand_history(sys::state const& state, left_t market, right_t cid) {
	return state.world.market_get_aggregated_demand_history(market, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_aggregated_demand_history<left_t, right_t> market_get_aggregated_demand_history(sys::state const& state, left_t market, right_t cid) {
	return { market, cid };
}

template<bool eager, typename left_t, typename right_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_price_confidence(sys::state const& state, left_t market, right_t cid) {
	return state.world.market_get_price_confidence(market, cid);
}
template<bool eager, typename left_t, typename right_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_price_confidence<left_t, right_t> market_get_price_confidence(sys::state const& state, left_t market, right_t cid) {
	return { market, cid };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_trade_house_budget_import_scale(sys::state const& state, left_t market) {
	return state.world.market_get_trade_house_budget_import_scale(market);
}
template<bool eager, typename left_t,  std::enable_if_t<!eager, bool> = true>
constexpr term::trade_house_budget_import_scale<left_t> market_get_trade_house_budget_import_scale(sys::state const& state, left_t market) {
	return { market };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_naval_transportation_demand_satisfaction(sys::state const& state, left_t market) {
	return state.world.market_get_naval_transportation_demand_satisfaction(market);
}
template<bool eager, typename left_t, std::enable_if_t<!eager, bool> = true>
constexpr term::naval_transportation_demand_satisfaction<left_t> market_get_naval_transportation_demand_satisfaction(sys::state const& state, left_t market) {
	return { market };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto market_get_land_transportation_demand_satisfaction(sys::state const& state, left_t market) {
	return state.world.market_get_land_transportation_demand_satisfaction(market);
}
template<bool eager, typename left_t, std::enable_if_t<!eager, bool> = true>
constexpr term::land_transportation_demand_satisfaction<left_t> market_get_land_transportation_demand_satisfaction(sys::state const& state, left_t market) {
	return { market };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto trade_route_get_origin(sys::state const& state, left_t route) {
	return state.world.trade_route_get_origin(route);
}
template<bool eager, typename left_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_trade_origin<left_t> trade_route_get_origin(sys::state const& state, left_t route) {
	return { route };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto trade_route_get_target(sys::state const& state, left_t route) {
	return state.world.trade_route_get_target(route);
}
template<bool eager, typename left_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_trade_target<left_t> trade_route_get_target(sys::state const& state, left_t route) {
	return { route };
}

template<bool eager, typename left_t, std::enable_if_t<eager, bool> = true>
DCON_RELEASE_INLINE auto trade_route_get_owner(sys::state const& state, left_t route) {
	return state.world.trade_route_get_owner(route);
}
template<bool eager, typename left_t, std::enable_if_t<!eager, bool> = true>
constexpr term::fetch_trade_owner<left_t> trade_route_get_owner(sys::state const& state, left_t route) {
	return { route };
}

}
#undef DCON_RELEASE_INLINE
#undef DCON_RESTRICT

namespace economy {

// LOCAL EXPERIMENT (ALICE_EXP_TRADE_PHYSICAL_FLOW=1): for grain, guarantee a physical flow from
// markets with a production surplus to markets with an unmet deficit, independent of the price
// differential (which collapses to ~1e-8 everywhere and zeroes price-driven volume). The floor is
// split across the origin's outgoing and the target's incoming grain routes. Default off.
static bool const trade_physical_flow = []() {
	auto e = std::getenv("ALICE_EXP_TRADE_PHYSICAL_FLOW");
	return e && std::atoi(e) != 0;
}();


// LOCAL EXPERIMENT (ALICE_EXP_TRADE_BY_SCARCITY=1): after the price-based volume update, add a
// stock/demand-days routing term, so goods move from high-stock-days to low-stock-days markets.
static bool const trade_by_scarcity = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_BY_SCARCITY");
	return s && std::atoi(s) != 0;
}();
static float const trade_scarcity_scale = []() {
	auto s = std::getenv("ALICE_TRADE_SCARCITY_SCALE");
	return s ? std::max(0.1f, std::min(100.f, float(std::atof(s)))) : 10.f;
}();

// LOCAL EXPERIMENT (rejected; ALICE_EXP_TRADE_VOLUME_TARGET=1): let profitable routes converge toward a
// volume target derived from the target market's observed deficit, the origin's merchant offer,
// and the target market's affordable import budget. The target is shared across routes, ramped
// instead of forced, and never reduces an existing volume. Rejected as a standalone fix: the 1y
// runtime-only arm left the Machine Parts price tail unchanged and created negative market money
// in high-price import markets. Default off.
static bool const trade_volume_target = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_VOLUME_TARGET");
	return s && std::atoi(s) != 0;
}();
// LOCAL EXPERIMENT (rejected; ALICE_EXP_TRADE_VOLUME_TARGET_RUNTIME_ONLY=1): apply the volume target only
// after presimulation. This isolates whether the target's macro effects come from rewriting the
// initial 730-day economy or from its normal-play behaviour. Default off keeps the full-period arm.
static bool const trade_volume_target_runtime_only = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_VOLUME_TARGET_RUNTIME_ONLY");
	return s && std::atoi(s) != 0;
}();

// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (rejected; ALICE_EXP_TRADE_DEMAND_BUDGET=1): upstream gates route expansion with
// target_daily_sales / (100 * unit_sale_price). For high-value, low-quantity goods this requires
// the target to be able to buy 100 units before the first unit can enter. Use the value of one
// day of observed target demand instead. This is a dimensional correction to the liquidity
// constraint, not a volume target: the route still grows only through the existing profit,
// transport, expected-flow, and budget terms. Default 0 keeps upstream behaviour.
// Rejected by 1y seed 1234: GDP 445,008 -> 471,571 and trade 134,249 -> 116,191, while the
// all-commodity median price/cost rose 0.0117 -> 40.69 and Ammunition reached 2.33e6x cost.
// The gate was load-bearing: route expansion fed price and total_earn, which fed the next
// route expansion, while market settlement still had no closed buyer/seller payment.
static bool const trade_demand_budget = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_DEMAND_BUDGET");
	return s && std::atoi(s) != 0;
}();


// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (rejected; ALICE_EXP_TRADE_GOODS_CONSERVATION=1): apply only the physical half
// of the trade account: exported volume leaves the origin stockpile. Upstream credits the target
// and records the export flow but never removes the origin units, so the C0 ledger finds a goods
// residual almost exactly equal to recorded exports. Money, route volume, prices and tariffs are
// untouched. Rejected by 1y seed 1234: the daily goods residual fell from 107,952 to 42,473, but
// GDP fell 445,008 -> 389,892, factories 452 -> 365, and summary market money rose from 17.9M to
// 2.79B (the daily money ledger stayed near 32M, so the two measures also diverged). Conserving
// origin stock without a matching buyer, seller, ownership and settlement path is not viable.
// Default 0 keeps upstream behaviour for controlled comparison.
static bool const trade_goods_conservation = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_GOODS_CONSERVATION");
	return s && std::atoi(s) != 0;
}();

// LOCAL EXPERIMENT (ALICE_EXP_TRADE_ACCOUNT=1): general trade account, all commodities.
// Goods: exported volume is removed from the origin stockpile (upstream only credits the target).
// Money: origin receives the full origin value; target pays the target value; the trade house
// receives target - origin - tariffs. Sum of credits equals the target payment.
static bool const trade_account = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_ACCOUNT");
	return s && std::atoi(s) != 0;
}();

// LOCAL EXPERIMENT (ALICE_EXP_TRADE_IMPORT_ACCOUNT_FIX=1): the import buffer was accumulated from
// the target's *export* buffer. Import accounting therefore mixed two different trade directions.
// This switch only corrects the accumulation base; route volumes, prices and budgets are unchanged.
// Default 0 preserves upstream behaviour for controlled comparison.
static bool const trade_import_account_fix = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_IMPORT_ACCOUNT_FIX");
	return s && std::atoi(s) != 0;
}();

// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (ALICE_EXP_TRADE_ESCROW=1): settle each import payment against the exporting
// market's registered sellers. The target market pays first from its positive cash, then from a
// bounded trade-credit line; the same payment is split into seller receipt, export/import tariff,
// transport and merchant margin. Outstanding credit is repaid from the target's later market
// income. This replaces the legacy unilateral export cut/import spending only for cash settlement;
// route volumes and prices continue to use the existing logic. Default 0 keeps upstream behaviour.
static bool const trade_escrow = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_ESCROW");
	return s && std::atoi(s) != 0;
}();
static double const trade_credit_days = []() {
	auto s = std::getenv("ALICE_TRADE_CREDIT_DAYS");
	return s ? std::clamp(std::atof(s), 0.0, 3650.0) : 60.0;
}();
static std::vector<double> trade_credit_outstanding;
static bool const trace_trade_escrow = []() {
	auto s = std::getenv("ALICE_TRACE_TRADE_ESCROW");
	return s && std::atoi(s) != 0;
}();
// LOCAL EXPERIMENT (ALICE_EXP_FREIGHT_AD_VALOREM=1): charge sea freight as a share of the shipment's
// own value instead of an absolute amount of money per unit. The absolute form (distance x
// naval_transportation_price) is added to pay_per_unit next to prices that span four orders of
// magnitude across commodities: at the 1836 world mean naval price, moving one unit of grain 5,000 km
// costs 5.17 while the median grain price is 0.074, i.e. 70x the good's value, so such a route can
// never clear its first trade; moving one unit of clothes (median price 144.4) over the same distance
// costs 3.6% of its value. The ad valorem form makes the freight share independent of the price level.
// Land freight is untouched (it is written to 0 every day, economy.cpp:8512). Default 0 keeps the
// existing expression bit-for-bit.
static bool const freight_ad_valorem = []() {
	auto s = std::getenv("ALICE_EXP_FREIGHT_AD_VALOREM");
	return s && std::atoi(s) != 0;
}();
// Freight as a fraction of the unit's own value per km. 2.0e-5 => 2% per 1,000 km, 10% per 5,000 km.
// Calibration: 0.10 / 5000 km, i.e. the middle of the 19th century ocean-freight range (5-20% of
// value). Parameter validation only: a negative or absurd rate would make the experiment unreadable.
static float const freight_ad_valorem_rate = []() {
	auto s = std::getenv("ALICE_FREIGHT_AD_VALOREM_RATE");
	return s ? std::clamp(float(std::atof(s)), 0.f, 1.f) : 2.0e-5f;
}();
// Money per unit of a shipment. Both overloads below compute the same expression; the scalar one is
// for the per-route scalar paths (prediction, escrow settlement, trade ledger) and the vector one for
// the vectorised volume update, so the decision and the settlement cannot drift apart.
template<typename T>
static inline T trade_freight_unit_cost(bool is_sea, T distance, T absolute_per_km, T unit_value) {
	auto const absolute = distance * absolute_per_km;
	if(!freight_ad_valorem || !is_sea)
		return absolute;
	return distance * T(freight_ad_valorem_rate) * unit_value;
}
template<typename MASK>
static inline ve::fp_vector trade_freight_unit_cost_vec(MASK is_sea, ve::fp_vector distance, ve::fp_vector absolute_per_km, ve::fp_vector unit_value) {
	auto const absolute = distance * absolute_per_km;
	if(!freight_ad_valorem)
		return absolute;
	return ve::select(is_sea, distance * ve::fp_vector{ freight_ad_valorem_rate } * unit_value, absolute);
}
static double trade_escrow_day_sellers = 0.0;
static double trade_escrow_day_costs = 0.0;
static double trade_escrow_day_merchant = 0.0;
static double trade_escrow_day_funding = 0.0;
static double trade_escrow_day_cash = 0.0;
static double trade_escrow_day_credit = 0.0;
static double trade_escrow_day_unmatched = 0.0;
static double trade_escrow_day_blocked = 0.0;
static double trade_escrow_day_blocked_seller = 0.0;
static double trade_escrow_day_blocked_stock = 0.0;
static double trade_escrow_day_blocked_budget = 0.0;
static double trade_escrow_day_blocked_unprofitable = 0.0;
static double trade_escrow_day_blocked_partial = 0.0;
static double trade_escrow_day_blocked_other = 0.0;
static double trade_escrow_day_margin_origin = 0.0;
static double trade_escrow_day_margin_target = 0.0;
static double trade_escrow_day_margin_third = 0.0;

bool trade_escrow_enabled() {
	return trade_escrow && !is_presimulation_update();
}

double trade_credit_total() {
	double total = 0.0;
	for(auto const value : trade_credit_outstanding) {
		if(std::isfinite(value))
			total += value;
	}
	return total;
}

// LOCAL FORK (not upstream): the clearing bank's cash is the exact negative of outstanding trade
// credit. Issuing credit debits this account and credits the seller; repayment debits the buyer
// and credits it. Keeping the lender as an identity avoids a second mutable balance that could
// drift from the loan book while still making the credit source explicit in the money audit.
double trade_credit_lender_balance() {
	return -trade_credit_total();
}

double trade_repay_credit(sys::state& state, dcon::market_id market, double available) {
	if(!trade_escrow_enabled() || !market || !(available > 0.0) || !std::isfinite(available))
		return 0.0;
	auto const market_count = size_t(state.world.market_size());
	if(trade_credit_outstanding.size() != market_count)
		return 0.0;
	auto const index = size_t(market.index());
	if(index >= trade_credit_outstanding.size())
		return 0.0;
	auto const repayment = std::min(std::max(0.0, trade_credit_outstanding[index]), available);
	if(!(repayment > 0.0) || !std::isfinite(repayment))
		return 0.0;
	trade_credit_outstanding[index] -= repayment;
	return repayment;
}

// LOCAL EXPERIMENT (ALICE_EXP_TRADE_FINANCE=1): general income/credit-based trade finance.
// Daily import budget = 50% of the market's realized daily income (sales + export revenue EMA);
// trade credit limit = 60 days of that income. Negative market money is the credit balance.
static bool const trade_finance = []() {
	auto s = std::getenv("ALICE_EXP_TRADE_FINANCE");
	return s && std::atoi(s) != 0;
}();
static std::vector<float> trade_income_ema;
static std::vector<float> trade_import_used;
static std::vector<float> trade_import_month;
static std::vector<float> trade_outstanding;
static FILE* trade_finance_file = nullptr;
static bool trade_finance_file_init = false;
static std::string last_trade_finance_date;

static void update_trade_finance_daily(sys::state& state) {
	if(!trade_finance)
		return;
	auto const n = size_t(state.world.market_size());
	if(trade_income_ema.size() < n) {
		trade_income_ema.resize(n, 0.f);
		trade_import_used.resize(n, 0.f);
		trade_import_month.resize(n, 0.f);
		trade_outstanding.resize(n, 0.f);
	}
	state.world.for_each_market([&](dcon::market_id m) {
		auto const i = size_t(m.index());
		auto const income = state.world.market_get_sales(m)
			+ std::max(0.f, state.world.market_get_export_cut(m));
		auto& ema = trade_income_ema[i];
		ema = ema > 0.f ? ema * 0.9f + income * 0.1f : income;
		trade_import_used[i] = 0.f;
		trade_outstanding[i] = std::max(0.f, -state.world.market_get_stockpile(m, economy::money));
	});
	auto const ymd = state.current_date.to_ymd(state.start_date);
	if(ymd.day != 1)
		return;
	char datebuf[16];
	std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
	if(last_trade_finance_date == datebuf)
		return;
	last_trade_finance_date = datebuf;
	if(!trade_finance_file_init) {
		trade_finance_file_init = true;
		auto const path = std::getenv("ALICE_TRADE_FINANCE_FILE");
		if(path) {
			trade_finance_file = fopen(path, "w");
			if(trade_finance_file)
				fprintf(trade_finance_file, "date,market,owner_index,income_ema,flow_budget,credit_limit,outstanding,import_month\n");
		}
	}
	if(!trade_finance_file)
		return;
	for(size_t i = 0; i < n; ++i) {
		auto const m = dcon::market_id{dcon::market_id::value_base_t(i)};
		auto const zone = state.world.market_get_zone_from_local_market(m);
		auto const owner = state.world.state_instance_get_nation_from_state_ownership(zone);
		fprintf(trade_finance_file, "%s,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g\n",
			datebuf, int(i), owner.index(), double(trade_income_ema[i]), double(0.5f * trade_income_ema[i]),
			double(60.f * trade_income_ema[i]), double(trade_outstanding[i]), double(trade_import_month[i]));
	}
	fflush(trade_finance_file);
	std::fill(trade_import_month.begin(), trade_import_month.end(), 0.f);
}

/*
New model of trade update:
Assume that there is a segment [0, 1] of traders operating this route.
Trader 0 gets 0x of the sales.
Trader 1 gets 2x of the sales.
Traders inbetween earn a linear combination of trader 0 and trader 1 earning.
If a certain trader earns less than SPEND, they change their volume (1 - a) times - q dt.
If a certain trader earns more than SPEND, they change their volume (1 + a) times + q dt.
We want to find a proportion of traders which would earn more than they spend and update the total traded amount accordingly.

2 * EARN * x + y = 2 * EARN;
if y = SPEND
2 * EARN * x + SPEND = 2 * EARN
x = (2 * EARN - SPEND) / (2 * EARN) --- the ratio of traders with profitable trade
2 * (x - 0.5) = (EARN - SPEND) / (EARN)  --- signed amount of traders increasing/reducing their volume
*/

template<bool eager, typename scalar, typename ROUTE_ID, typename COMMODITY_ID, typename VALUE_IN, typename BOOL_IN1, typename BOOL_IN2>
inline auto trade_route_compute_change_multiplier(
	sys::state const& state,
	BOOL_IN1 reset_route,
	BOOL_IN2 is_sea_route,
	ROUTE_ID trade_route,
	COMMODITY_ID c,

	VALUE_IN export_tariff,
	VALUE_IN import_tariff,
	VALUE_IN transport_cost,
	VALUE_IN budget,
	VALUE_IN trade_good_loss_mult,
	VALUE_IN merchant_cut,
	VALUE_IN price_import,
	VALUE_IN price_export
) {
	auto origin = adaptive_ve::trade_route_get_origin<eager, ROUTE_ID>(state, trade_route);
	auto target = adaptive_ve::trade_route_get_target<eager, ROUTE_ID>(state, trade_route);
	auto owner = adaptive_ve::trade_route_get_owner<eager, ROUTE_ID>(state, trade_route);
	auto reset_route_commodity = reset_route;
	auto current_volume = adaptive_ve::trade_route_get_volume<eager>(state, trade_route, c);

	auto expected_to_buy = adaptive_ve::market_get_expected_probability_to_buy<eager>(state, origin, c);
	auto expected_to_sell = adaptive_ve::market_get_expected_probability_to_sell<eager>(state, target, c);
	auto historical_demand = adaptive_ve::market_get_aggregated_demand_history<eager>(state, target, c);
	auto import_budget_scale = adaptive_ve::market_get_trade_house_budget_import_scale<eager>(state, target);
	auto naval_transport_availability = adaptive_ve::market_get_naval_transportation_demand_satisfaction<eager>(state, owner);
	auto land_transport_availability = adaptive_ve::market_get_land_transportation_demand_satisfaction<eager>(state, owner);
	auto transport_availability = adaptive_ve::select<BOOL_IN2, scalar>(is_sea_route, naval_transport_availability, land_transport_availability);

	auto earn_per_unit = price_import * trade_good_loss_mult;
	auto pay_per_unit = price_export * (1.f + export_tariff + merchant_cut) + price_import * import_tariff + transport_cost;
	auto owner_is_exporter = owner == origin;
	auto price_sold = adaptive_ve::select<decltype(owner_is_exporter), scalar>(owner_is_exporter, price_import, price_export);
	auto budget_scale = adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), 1.f - import_budget_scale);
	auto budget_factor = adaptive_ve::min(adaptive_ve::constant_value<scalar>(1.f), adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), budget / (1.f + import_budget_scale * 5.f) / 100.f * budget_scale / (economy::price_properties::commodity::min + price_sold)));

	auto hard_limit = transport_availability;
	auto soft_limit = expected_to_sell * expected_to_buy;
	auto const demand_units = adaptive_ve::max(
		historical_demand, adaptive_ve::constant_value<scalar>(0.0001f));
	auto const demand_weight = adaptive_ve::constant_value<scalar>(trade_demand_budget ? 1.f : 0.f);
	auto const liquidity_need = price_sold
		* (100.f + demand_weight * (demand_units - 100.f));
	auto change_multiplier =
		adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), (hard_limit - 0.9f) / 0.1f)
		* adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), (soft_limit - 0.8f) / 0.2f)
		* adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), (budget_scale - 0.1f) / 0.2f)
		* adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), adaptive_ve::min(adaptive_ve::constant_value<scalar>(100.f), budget / (economy::price_properties::commodity::min + liquidity_need) - 0.01f));

	return change_multiplier;
}

template<bool eager, typename scalar, typename ROUTE_ID, typename COMMODITY_ID, typename VALUE_IN, typename BOOL_IN1, typename BOOL_IN2>
inline auto trade_route_compute_decay(
	sys::state const& state,
	BOOL_IN1 reset_route,
	BOOL_IN2 is_sea_route,
	ROUTE_ID trade_route,
	COMMODITY_ID c,

	VALUE_IN export_tariff,
	VALUE_IN import_tariff,
	VALUE_IN transport_cost,
	VALUE_IN budget,
	VALUE_IN trade_good_loss_mult,
	VALUE_IN merchant_cut,
	VALUE_IN price_import,
	VALUE_IN price_export
) {
	auto origin = adaptive_ve::trade_route_get_origin<eager, ROUTE_ID>(state, trade_route);
	auto target = adaptive_ve::trade_route_get_target<eager, ROUTE_ID>(state, trade_route);
	auto owner = adaptive_ve::trade_route_get_owner<eager, ROUTE_ID>(state, trade_route);
	auto reset_route_commodity = reset_route;
	auto current_volume = adaptive_ve::trade_route_get_volume<eager>(state, trade_route, c);

	auto expected_to_buy = adaptive_ve::market_get_expected_probability_to_buy<eager>(state, origin, c);
	auto expected_to_sell = adaptive_ve::market_get_expected_probability_to_sell<eager>(state, target, c);
	auto historical_demand = adaptive_ve::market_get_aggregated_demand_history<eager>(state, target, c);
	auto import_budget_scale = adaptive_ve::market_get_trade_house_budget_import_scale<eager>(state, target);
	auto naval_transport_availability = adaptive_ve::market_get_naval_transportation_demand_satisfaction<eager>(state, owner);
	auto land_transport_availability = adaptive_ve::market_get_land_transportation_demand_satisfaction<eager>(state, owner);
	auto transport_availability = adaptive_ve::select<BOOL_IN2, scalar>(is_sea_route, naval_transport_availability, land_transport_availability);

	auto earn_per_unit = price_import * trade_good_loss_mult;
	auto pay_per_unit = price_export * (1.f + export_tariff + merchant_cut) + price_import * import_tariff + transport_cost;
	auto owner_is_exporter = owner == origin;
	auto price_sold = adaptive_ve::select<decltype(owner_is_exporter), scalar>(owner_is_exporter, price_import, price_export);
	auto budget_scale = adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), 1.f - import_budget_scale);
	auto budget_factor = adaptive_ve::min(adaptive_ve::constant_value<scalar>(1.f), adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.f), budget / (1.f + import_budget_scale * 5.f) / 100.f * budget_scale / (economy::price_properties::commodity::min + price_sold)));

	auto hard_limit = transport_availability;
	auto soft_limit = expected_to_sell * expected_to_buy;
	auto decay = adaptive_ve::max(adaptive_ve::constant_value<scalar>(0.99f), adaptive_ve::min(adaptive_ve::constant_value<scalar>(1.f), 0.5f + hard_limit * soft_limit * budget_scale));

	return decay;
}


template<bool eager, typename scalar, typename ROUTE_ID, typename COMMODITY_ID, typename VALUE_IN, typename BOOL_IN1, typename BOOL_IN2>
inline auto trade_route_compute_next_volume(
	sys::state const& state,
	BOOL_IN1 reset_route,
	BOOL_IN2 is_sea_route,
	ROUTE_ID trade_route,
	COMMODITY_ID c,

	VALUE_IN export_tariff,
	VALUE_IN import_tariff,
	VALUE_IN transport_cost,
	VALUE_IN budget,
	VALUE_IN trade_good_loss_mult,
	VALUE_IN merchant_cut,
	VALUE_IN price_import,
	VALUE_IN price_export,
	VALUE_IN change_multiplier,
	VALUE_IN decay
) {
	auto origin = adaptive_ve::trade_route_get_origin<eager, ROUTE_ID>(state, trade_route);
	auto target = adaptive_ve::trade_route_get_target<eager, ROUTE_ID>(state, trade_route);
	auto owner = adaptive_ve::trade_route_get_owner<eager, ROUTE_ID>(state, trade_route);
	auto reset_route_commodity = reset_route;
	auto current_volume = adaptive_ve::trade_route_get_volume<eager>(state, trade_route, c);
	
	auto expected_to_buy = adaptive_ve::market_get_expected_probability_to_buy<eager>(state, origin, c);
	auto expected_to_sell = adaptive_ve::market_get_expected_probability_to_sell<eager>(state, target, c);
	auto historical_demand = adaptive_ve::market_get_aggregated_demand_history<eager>(state, target, c);
	auto naval_transport_availability = adaptive_ve::market_get_naval_transportation_demand_satisfaction<eager>(state, owner);
	auto land_transport_availability = adaptive_ve::market_get_land_transportation_demand_satisfaction<eager>(state, owner);
	auto transport_availability = adaptive_ve::select<BOOL_IN2, scalar>(is_sea_route, naval_transport_availability, land_transport_availability);
	
	auto earn_per_unit = price_import * trade_good_loss_mult;
	auto pay_per_unit = price_export * (1.f + export_tariff + merchant_cut) + price_import * import_tariff + transport_cost;

	auto diff = 2.f * (earn_per_unit - pay_per_unit) / (economy::price_properties::commodity::min + earn_per_unit);
	auto change = 0.01f * (0.1f + 0.001f * current_volume) * diff;
	auto change_is_negative = change <= adaptive_ve::constant_value<scalar>(0.f);
	auto change_slow_down_expansion = adaptive_ve::select<decltype(change_is_negative), scalar>(change_is_negative, change, change * change_multiplier);
	auto next = adaptive_ve::select<BOOL_IN1, scalar>(
		reset_route_commodity,
		adaptive_ve::constant_value<scalar>(0.f),
		adaptive_ve::max(
			adaptive_ve::constant_value<scalar>(0.f),
			current_volume * decay + change_slow_down_expansion + adaptive_ve::min(
				adaptive_ve::constant_value<scalar>(0.f),
				historical_demand - 0.0001f
			)
		)
	);

	return next;
}

void make_trade_volume_tooltip(
	sys::state& state,
	text::columnar_layout& contents,
	dcon::trade_route_id route,
	dcon::commodity_id cid,
	dcon::market_id point_of_view
) {
	auto prediction = predict_trade_route_volume_change(state, route, cid);

	auto multiplier = 1.f;
	auto B = state.world.trade_route_get_target(route);
	if(B == point_of_view) {
		multiplier = -1.f;
	}

	int this_id = 0;
	if(B == point_of_view) {
		this_id = 1;
	}

	if(prediction.commodity_is_not_discovered) {
		text::add_line(state, contents, "commodity_is_unknown");
		return;
	}

	if(prediction.commodity_is_not_tradable) {
		text::add_line(state, contents, "commodity_is_not_tradable");
		return;
	}

	if(prediction.trade_blocked) {
		text::add_line(state, contents, "trade_is_blocked");
		return;
	}

	text::add_line(state, contents, "trade_route_volume_profit_score",
		text::variable_type::val, text::fp_two_places{ multiplier * prediction.profit_score }
	);

	text::add_line(state, contents, "trade_route_volume_transport_cost",
		text::variable_type::val, text::format_money( prediction.transport_cost_per_unit )
	);
	text::add_line(state, contents, "trade_route_volume_export_price",
		text::variable_type::val, text::format_money(prediction.export_price)
	);
	text::add_line(state, contents, "trade_route_volume_import_price",
		text::variable_type::val, text::format_money(prediction.import_price)
	);

	//text::add_line(state, contents, "trade_route_volume_profit_score_export",
	//	text::variable_type::val, text::fp_two_places{ prediction.export_profit[this_id] / prediction.export_price[this_id] },
	//	text::variable_type::x, text::fp_two_places{ prediction.export_profit[this_id] },
	//	text::variable_type::y, text::fp_two_places{ prediction.export_price[this_id] }
	//);

	//text::add_line(state, contents, "trade_route_volume_profit_score_import",
	//	text::variable_type::val, text::fp_two_places{ -prediction.export_profit[1 - this_id] / prediction.export_price[1 - this_id] },
	//	text::variable_type::x, text::fp_two_places{ -prediction.export_profit[1 - this_id] },
	//	text::variable_type::y, text::fp_two_places{ prediction.export_price[1 - this_id] }
	//);

	text::add_line(state, contents, "trade_route_volume_base_change",
		text::variable_type::val, text::fp_two_places{ multiplier * prediction.base_change },
		text::variable_type::x, text::fp_two_places{ std::abs(prediction.current_volume) }
	);

	text::add_line(state, contents, "trade_route_volume_expansion_multiplier",
		text::variable_type::val, text::fp_two_places{ prediction.expansion_multiplier }
	);
	text::add_line(state, contents, "trade_route_volume_transport_availability",
		text::variable_type::val, text::fp_two_places{ prediction.transport_availability }
	);
	text::add_line(state, contents, "trade_route_volume_budget_availability",
		text::variable_type::val, text::fp_two_places{ prediction.import_ratio }
	);
	text::add_line(state, contents, "trade_route_volume_risk",
		text::variable_type::val, text::fp_two_places{ prediction.risk }
	);
	//text::add_line(state, contents, "trade_route_confidence_export",
	//	text::variable_type::val, text::fp_two_places{ prediction.export_price_confidence }
	//);
	//text::add_line(state, contents, "trade_route_confidence_import",
	//	text::variable_type::val, text::fp_two_places{ prediction.import_price_confidence }
	//);

	//text::add_line(state, contents, "trade_route_volume_decay",
	//	text::variable_type::val, text::fp_two_places{ multiplier * prediction.decay }
	//);

	text::add_line(state, contents, "trade_route_volume_final_change",
		text::variable_type::val, text::fp_two_places{ multiplier * prediction.final_change }
	);

	term::bool_t trade_blocked {
		prediction.trade_blocked,
		"trade_prediction_impossible"
	};

	term::bool_t sea_route{
		state.world.trade_route_get_is_sea_route(route),
		"trade_prediction_sea_route"
	};

	term::trade_route_id_wrapper trade_route{
		route,
		"trade_prediction_route"
	};

	term::commodity_id_wrapper cid_wrapper{
		cid,
		"trade_prediction_commodity"
	};

	auto origin = adaptive_ve::trade_route_get_origin<false, term::trade_route_id_wrapper>(state, trade_route);
	auto target = adaptive_ve::trade_route_get_target<false, term::trade_route_id_wrapper>(state, trade_route);
	auto price_origin = adaptive_ve::market_get_price<false>(state, origin, cid_wrapper);
	auto price_target = adaptive_ve::market_get_price<false>(state, target, cid_wrapper);
	auto confidence_export = adaptive_ve::max(adaptive_ve::constant_value<float>(0.5f), adaptive_ve::market_get_price_confidence<false>(state, origin, cid_wrapper));
	auto confidence_import = adaptive_ve::max(adaptive_ve::constant_value<float>(0.5f), adaptive_ve::market_get_price_confidence<false>(state, target, cid_wrapper));
	auto price_export = price_origin / confidence_export;
	auto price_import = price_target * confidence_import;

	auto price_export_computed = term::compute(state, nullptr, price_export);
	auto price_import_computed = term::compute(state, nullptr, price_import);

	auto multiplier_value = trade_route_compute_change_multiplier<false, float, term::trade_route_id_wrapper>(
		state,
		trade_blocked,
		sea_route,
		trade_route,
		cid_wrapper,
		term::value_t{ prediction.export_tariff, "export_tariff", text::text_color::dark_red },
		term::value_t{ prediction.import_tariff, "import_tariff", text::text_color::dark_red },
		term::value_t{ prediction.transport_cost_per_unit, "transport_cost", text::text_color::dark_red },
		term::value_t{ prediction.budget, "trade_budget", text::text_color::dark_green },
		term::value_t{ prediction.trade_good_loss_mult, "trade_good_loss_mult", text::text_color::dark_red },
		term::value_t{ prediction.merchant_cut, "merchant_cut", text::text_color::dark_red },
		term::value_t{ price_import_computed.data, "expected_import_price", text::text_color::gold },
		term::value_t{ price_export_computed.data , "expected_export_price", text::text_color::gold }
	);
	auto multiplier_computed = term::compute(state, nullptr, multiplier_value);


	auto decay_value = trade_route_compute_decay<false, float, term::trade_route_id_wrapper>(
		state,
		trade_blocked,
		sea_route,
		trade_route,
		cid_wrapper,
		term::value_t{ prediction.export_tariff, "export_tariff", text::text_color::dark_red },
		term::value_t{ prediction.import_tariff, "import_tariff", text::text_color::dark_red },
		term::value_t{ prediction.transport_cost_per_unit, "transport_cost", text::text_color::dark_red },
		term::value_t{ prediction.budget, "trade_budget", text::text_color::dark_green },
		term::value_t{ prediction.trade_good_loss_mult, "trade_good_loss_mult", text::text_color::dark_red },
		term::value_t{ prediction.merchant_cut, "merchant_cut", text::text_color::dark_red },
		term::value_t{ price_import_computed.data, "expected_import_price", text::text_color::gold },
		term::value_t{ price_export_computed.data , "expected_export_price", text::text_color::gold }
	);
	auto decay_computed = term::compute(state, nullptr, decay_value);

	auto value = trade_route_compute_next_volume<false, float, term::trade_route_id_wrapper>(
		state,
		trade_blocked,
		sea_route,
		trade_route,
		cid_wrapper,
		term::value_t{ prediction.export_tariff, "export_tariff", text::text_color::dark_red },
		term::value_t{ prediction.import_tariff, "import_tariff", text::text_color::dark_red },
		term::value_t{ prediction.transport_cost_per_unit, "transport_cost", text::text_color::dark_red },
		term::value_t{ prediction.budget, "trade_budget", text::text_color::dark_green },
		term::value_t{ prediction.trade_good_loss_mult, "trade_good_loss_mult", text::text_color::dark_red },
		term::value_t{ prediction.merchant_cut, "merchant_cut", text::text_color::dark_red },
		term::value_t{ price_import_computed.data, "expected_import_price", text::text_color::gold },
		term::value_t{ price_export_computed.data , "expected_export_price", text::text_color::gold },
		term::value_t{ multiplier_computed.data, "trade_volume_speed_multiplier", text::text_color::brown },
		term::value_t{ decay_computed.data, "trade_route_volume_decay", text::text_color::brown }
	);

	auto value_computed = term::compute(state, nullptr, value);
	{
		auto box = text::open_layout_box(contents, 15);
		term::evaluate_derivative(state, contents, box, "", value_computed, term::value_t_result ( term::value_t{ prediction.export_tariff, "export_tariff", text::text_color::dark_red }));
		term::evaluate_derivative(state, contents, box, "", value_computed, term::value_t_result ( term::value_t{ prediction.export_tariff, "import_tariff", text::text_color::dark_red }));
		text::close_layout_box(contents, box);
	}

	{
		auto box = text::open_layout_box(contents, 15);
		text::localised_format_box(state, contents, box, "expected_export_price");
		text::add_line_break_to_layout_box(state, contents, box);
		term::evaluate_string(state, contents, box, "", false, price_export_computed);
		text::close_layout_box(contents, box);
	}

	{
		auto box = text::open_layout_box(contents, 15);
		text::localised_format_box(state, contents, box, "expected_import_price");
		text::add_line_break_to_layout_box(state, contents, box);
		term::evaluate_string(state, contents, box, "", false, price_import_computed);
		text::close_layout_box(contents, box);
	}

	{
		auto box = text::open_layout_box(contents, 15);
		text::localised_format_box(state, contents, box, "trade_volume_speed_multiplier");
		text::add_line_break_to_layout_box(state, contents, box);
		term::evaluate_string(state, contents, box, "", false, multiplier_computed);
		text::close_layout_box(contents, box);
	}

	{
		auto box = text::open_layout_box(contents, 15);
		text::localised_format_box(state, contents, box, "trade_route_volume_decay");
		text::add_line_break_to_layout_box(state, contents, box);
		term::evaluate_string(state, contents, box, "", false, decay_computed);
		text::close_layout_box(contents, box);
	}

	{
		auto box = text::open_layout_box(contents, 15);
		term::evaluate_string(state, contents, box, "", false, value_computed);
		text::close_layout_box(contents, box);
	}
}

embargo_explanation embargo_exists(
	sys::state& state, dcon::nation_id n_A, dcon::nation_id n_B
) {
	auto sphere_A = state.world.nation_get_in_sphere_of(n_A);
	auto sphere_B = state.world.nation_get_in_sphere_of(n_B);
	auto overlord_A = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(n_A)
	);
	auto overlord_B = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(n_B)
	);
	// Subjects have embargo of overlords propagated onto them
	auto market_leader_A = nations::get_market_leader(state, n_A);
	auto market_leader_B = nations::get_market_leader(state, n_B);

	// if market capital controllers are at war then we will break the link
	auto at_war = military::are_at_war(state, n_A, n_B);

	auto is_A_civ = state.world.nation_get_is_civilized(n_A);
	auto is_B_civ = state.world.nation_get_is_civilized(n_B);

	// sphere joins embargo
	// subject joins embargo
	// diplomatic embargos
	auto A_joins_sphere_wide_embargo = military::are_at_war(state, market_leader_A, market_leader_B);
	auto B_joins_sphere_wide_embargo = military::are_at_war(state, market_leader_B, market_leader_A);

	auto A_has_embargo = non_war_embargo_status(state, n_A, n_B, market_leader_A, market_leader_B);

	auto B_has_embargo = non_war_embargo_status(state, n_B, n_A, market_leader_B, market_leader_A);

	embargo_explanation result;

	result.war = at_war;
	result.origin_embargo = A_has_embargo;
	result.target_embargo = B_has_embargo;
	result.origin_join_embargo = A_joins_sphere_wide_embargo;
	result.target_join_embargo = B_joins_sphere_wide_embargo;

	result.combined = at_war
		|| A_has_embargo
		|| B_has_embargo
		|| A_joins_sphere_wide_embargo
		|| B_joins_sphere_wide_embargo;

	return result;
}

trade_route_volume_change_reasons predict_trade_route_volume_change(
	sys::state const& state, dcon::trade_route_id route, dcon::commodity_id cid
) {
	trade_route_volume_change_reasons result{
		.export_price = 0.f,
		.import_price = 0.f,

		.trade_blocked = false,
		.commodity_is_not_tradable = false
	};

	auto A = state.world.trade_route_get_origin(route);
	auto B = state.world.trade_route_get_target(route);
	auto s_A = state.world.market_get_zone_from_local_market(A);
	auto s_B = state.world.market_get_zone_from_local_market(B);
	auto formal_n_A = state.world.state_instance_get_nation_from_state_ownership(s_A);
	auto formal_n_B = state.world.state_instance_get_nation_from_state_ownership(s_B);
	auto capital_A = state.world.state_instance_get_capital(s_A);
	auto capital_B = state.world.state_instance_get_capital(s_B);
	auto port_A = province::state_get_coastal_capital(state, s_A);
	auto port_B = province::state_get_coastal_capital(state, s_B);
	auto controller_capital_A = state.world.province_get_nation_from_province_control(capital_A);
	auto controller_capital_B = state.world.province_get_nation_from_province_control(capital_B);
	controller_capital_A = controller_capital_A ? controller_capital_A : formal_n_A;
	controller_capital_B = controller_capital_B ? controller_capital_B : formal_n_B;
	auto controller_port_A = state.world.province_get_nation_from_province_control(port_A);
	auto controller_port_B = state.world.province_get_nation_from_province_control(port_B);
	auto sphere_A = state.world.nation_get_in_sphere_of(controller_capital_A);
	auto sphere_B = state.world.nation_get_in_sphere_of(controller_capital_B);
	auto overlord_A = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(controller_capital_A)
	);
	auto overlord_B = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(controller_capital_B)
	);
	// Subjects have embargo of overlords propagated onto them
	auto market_leader_A = nations::get_market_leader(state, controller_capital_A);
	auto market_leader_B = nations::get_market_leader(state, controller_capital_B);

	// Equal/unequal trade treaties
	auto A_is_open_to_B = !state.world.trade_route_get_is_tariff_applied_origin(route);
	auto B_is_open_to_A = !state.world.trade_route_get_is_tariff_applied_target(route);

	auto port_occupied_A = military::are_at_war(state, controller_capital_A, controller_port_A);
	auto port_occupied_B = military::are_at_war(state, controller_capital_B, controller_port_B);

	auto is_A_blockaded = state.world.province_get_is_blockaded(port_A) || port_occupied_A;
	auto is_B_blockaded = state.world.province_get_is_blockaded(port_B) || port_occupied_B;

	// if market capital controllers are at war then we will break the link
	auto at_war = military::are_at_war(state, controller_capital_A, controller_capital_B);

	auto is_A_civ = state.world.nation_get_is_civilized(controller_capital_A);
	auto is_B_civ = state.world.nation_get_is_civilized(controller_capital_B);

	auto is_sea_route = state.world.trade_route_get_is_sea_route(route);
	auto same_nation = controller_capital_A == controller_capital_B;

	// US3AC7. Ban international sea routes or international land routes based on the corresponding modifiers
	auto A_bans_sea_trade = state.world.nation_get_modifier_values(controller_capital_A, sys::national_mod_offsets::disallow_naval_trade) > 0.f;
	auto B_bans_sea_trade = state.world.nation_get_modifier_values(controller_capital_B, sys::national_mod_offsets::disallow_naval_trade) > 0.f;
	auto sea_trade_banned = A_bans_sea_trade || B_bans_sea_trade;
	// US3AC8. Ban international sea routes or international land routes based on the corresponding modifiers
	auto A_bans_land_trade = state.world.nation_get_modifier_values(controller_capital_A, sys::national_mod_offsets::disallow_land_trade) > 0.f;
	auto B_bans_land_trade = state.world.nation_get_modifier_values(controller_capital_B, sys::national_mod_offsets::disallow_land_trade) > 0.f;
	auto land_trade_banned = A_bans_land_trade || B_bans_land_trade;
	auto trade_banned = (is_sea_route && sea_trade_banned && !same_nation) || (!is_sea_route && land_trade_banned && !same_nation);

	is_sea_route = is_sea_route && !is_A_blockaded && !is_B_blockaded;

	// US3AC9. Wartime embargoes
	auto A_joins_sphere_wide_embargo = ve::apply([&](auto n_a, auto n_b) {
		return military::are_at_war(state, n_a, n_b);
	}, market_leader_A, market_leader_B);

	auto B_joins_sphere_wide_embargo = ve::apply([&](auto n_a, auto n_b) {
		return military::are_at_war(state, n_a, n_b);
	}, market_leader_B, market_leader_A);

	// US3AC10. diplomatic embargos
	// US3AC11. sphere joins embargo
	// US3AC12 subject joins embargo
	auto A_has_embargo = non_war_embargo_status(state, controller_capital_A, controller_capital_B, market_leader_A, market_leader_B);
	A_joins_sphere_wide_embargo = A_has_embargo || A_joins_sphere_wide_embargo;

	auto B_has_embargo = non_war_embargo_status(state, controller_capital_B, controller_capital_A, market_leader_B, market_leader_A);
	B_joins_sphere_wide_embargo = B_has_embargo || B_joins_sphere_wide_embargo;

	// US3AC13
	auto merchant_cut = same_nation ? economy::merchant_cut_domestic : economy::merchant_cut_foreign;
	result.merchant_cut = merchant_cut;

	// US3AC14
	auto export_tariff = (same_nation || A_is_open_to_B) ? 0.f : effective_tariff_export_rate(state, controller_capital_A, A);
	result.export_tariff = export_tariff;
	auto import_tariff = (same_nation || B_is_open_to_A) ? 0.f : effective_tariff_import_rate(state, controller_capital_B, B);
	result.import_tariff = import_tariff;

	auto distance = state.world.trade_route_get_distance_km(route);
	auto trade_good_loss_mult = std::max(0.f, 1.f - trade_goods_lost_per_km * distance);
	result.trade_good_loss_mult = trade_good_loss_mult;

	auto current_volume = state.world.trade_route_get_volume(route, cid);
	result.current_volume = current_volume;

	auto owner = state.world.trade_route_get_owner(route);

	auto transport_cost_per_distance_weight = state.world.market_get_land_transportation_price(owner);
	if(is_sea_route) {
		transport_cost_per_distance_weight = state.world.market_get_naval_transportation_price(owner);
	}

	auto transport_cost_per_weight = trade_freight_unit_cost(is_sea_route, distance, transport_cost_per_distance_weight, state.world.market_get_price(A, cid));
	result.transport_cost_per_unit = transport_cost_per_weight;

	result.trade_blocked = at_war
		|| A_joins_sphere_wide_embargo
		|| B_joins_sphere_wide_embargo
		|| trade_banned
		|| !state.world.trade_route_is_valid(route);

	result.commodity_is_not_tradable =
		state.world.commodity_get_money_rgo(cid)
		|| state.world.commodity_get_is_local(cid);

	result.commodity_is_not_discovered =
		state.world.commodity_get_rgo_amount(cid) > 0.f
		&& !state.world.commodity_get_actually_exists_in_nature(cid);

	auto price_export_adjusted = state.world.market_get_price(A, cid) * (1.f + export_tariff + merchant_cut);
	result.export_price = price_export_adjusted;

	auto price_import_adjusted = state.world.market_get_price(B, cid) * (1.f - import_tariff) * trade_good_loss_mult;
	result.import_price = price_import_adjusted;

	auto c = cid;

	auto expected_to_buy = state.world.market_get_expected_probability_to_buy(A, c);
	auto expected_to_sell = state.world.market_get_expected_probability_to_sell(B, c);

	auto pessimism_confidence_A = 0.5f * (state.world.market_get_aggregated_demand_history(A, c) + state.world.market_get_aggregated_supply_history(A, c));
	auto pessimism_confidence_B = 0.5f * (state.world.market_get_aggregated_demand_history(B, c) + state.world.market_get_aggregated_supply_history(B, c));

	auto sold_boundary = stockpile_to_supply / (stockpile_spoilage + stockpile_to_supply);

	auto confidence_export = std::max(0.5f, state.world.market_get_price_confidence(A, c));
	auto confidence_import = std::max(0.5f, state.world.market_get_price_confidence(B, c));

	result.export_price_confidence = confidence_export;
	result.import_price_confidence = confidence_import;

	auto price_export = state.world.market_get_price(A, c) / confidence_export;
	auto price_import = state.world.market_get_price(B, c) * confidence_import;

	auto transport_availability = is_sea_route
		? state.world.market_get_naval_transportation_demand_satisfaction(owner)
		: state.world.market_get_land_transportation_demand_satisfaction(owner);

	auto import_budget_scale = state.world.market_get_trade_house_budget_import_scale(B);
	auto budget = 1.f + state.world.market_get_total_earn(B);
	result.budget = budget;

	auto price_sold = owner == A ? price_import : price_export;
	auto budget_scale = std::max(0.f, 1.f - import_budget_scale);
	auto budget_factor = std::min(1.f, std::max(0.f, budget / 100.f / (1.f + 5.f * import_budget_scale) / (economy::price_properties::commodity::min + price_sold)));
	auto hard_limit = transport_availability;
	auto soft_limit = expected_to_sell * expected_to_buy;
	auto change_multiplier =
		std::max(0.f, (hard_limit - 0.9f) / 0.1f)
		* std::max(0.f, (soft_limit - 0.8f) / 0.2f)
		* budget_factor * budget_factor;
	auto decay = std::max(0.999f, std::min(1.f, 0.5f + budget_scale * soft_limit * hard_limit));
	auto earn_per_unit = price_import * trade_good_loss_mult;
	auto pay_per_unit = price_export * (1.f + export_tariff + merchant_cut) + price_import * import_tariff + transport_cost_per_weight;
	auto diff = 2.f * (earn_per_unit - pay_per_unit) / (earn_per_unit + economy::price_properties::commodity::min);
	auto base_change = (current_volume * 0.001f + 0.1f) * diff;
	auto change = base_change <= 0.f ? base_change : base_change * change_multiplier;
	auto next = std::max(0.f, current_volume * decay + change);

	result.profit_score = diff;
	result.expected_to_buy_in_origin_ratio = expected_to_buy;
	result.expansion_multiplier = change_multiplier;
	result.import_ratio = budget_scale;
	result.transport_availability = transport_availability;
	result.risk = budget_factor;
	result.profit = earn_per_unit - pay_per_unit;
	result.base_change = base_change;
	result.decay = current_volume * (1.f - decay);
	result.final_change = next;

	return result;
}

void update_trade_routes_volume(
	sys::state& state,
	bool ignore_reality,
	ve::vectorizable_buffer<float, dcon::market_id>& export_tariff_buffer,
	ve::vectorizable_buffer<float, dcon::market_id>& import_tariff_buffer,
	ve::vectorizable_buffer<dcon::province_id, dcon::state_instance_id>& coastal_capital_buffer,
	ve::vectorizable_buffer<float, dcon::state_instance_id>& state_port_is_occupied
) {

	update_trade_finance_daily(state);

	// LOCAL EXPERIMENT: route-count caches are only built when the target-volume probe is enabled.
	static ve::vectorizable_buffer<float, dcon::market_id> incoming_route_count(uint32_t(1));
	static ve::vectorizable_buffer<float, dcon::market_id> outgoing_route_count(uint32_t(1));
	bool const apply_trade_volume_target = trade_volume_target
		&& (!trade_volume_target_runtime_only || !is_presimulation_update());
	if(apply_trade_volume_target) {
		incoming_route_count = state.world.market_make_vectorizable_float_buffer();
		outgoing_route_count = state.world.market_make_vectorizable_float_buffer();
		state.world.for_each_market([&](dcon::market_id m) {
			incoming_route_count.set(m, 0.f);
			outgoing_route_count.set(m, 0.f);
		});
		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route) || state.world.trade_route_get_is_trade_forbidden(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			if(!origin || !target)
				return;
			outgoing_route_count.set(origin, outgoing_route_count.get(origin) + 1.f);
			incoming_route_count.set(target, incoming_route_count.get(target) + 1.f);
		});
	}

	state.world.execute_parallel_over_trade_route([&](auto trade_route) {
		auto origin = state.world.trade_route_get_origin(trade_route);
		auto target = state.world.trade_route_get_target(trade_route);

		auto owner = state.world.trade_route_get_owner(trade_route);
		auto budget = 1.f + state.world.market_get_total_earn(target);

		auto s_origin = state.world.market_get_zone_from_local_market(origin);
		auto s_target = state.world.market_get_zone_from_local_market(target);

		auto capital_origin = state.world.state_instance_get_capital(s_origin);
		auto capital_target = state.world.state_instance_get_capital(s_target);

		auto controller_origin = state.world.province_get_nation_from_province_control(capital_origin);
		auto controller_target = state.world.province_get_nation_from_province_control(capital_target);

		auto formal_owner_origin = state.world.province_get_nation_from_province_ownership(capital_origin);
		auto formal_owner_target = state.world.province_get_nation_from_province_ownership(capital_target);

		controller_origin = ve::select(controller_origin != dcon::nation_id{ }, controller_origin, formal_owner_origin);
		controller_target = ve::select(controller_target != dcon::nation_id{ }, controller_target, formal_owner_target);

		auto origin_tariff_is_applied = state.world.trade_route_get_is_tariff_applied_origin(trade_route);
		auto target_tariff_is_applied = state.world.trade_route_get_is_tariff_applied_target(trade_route);

		ve::mask_vector is_origin_blockaded = state_port_is_occupied.get(s_origin) > 0.f;
		ve::mask_vector is_target_blockaded = state_port_is_occupied.get(s_target) > 0.f;

		auto trade_closed = state.world.trade_route_get_is_trade_forbidden(trade_route);

		auto is_origin_civ = state.world.nation_get_is_civilized(controller_origin);
		auto is_target_civ = state.world.nation_get_is_civilized(controller_target);
		auto is_sea_route = state.world.trade_route_get_is_sea_route(trade_route);
		auto blockaded = is_sea_route && (is_origin_blockaded || is_target_blockaded);
		auto same_nation = controller_origin == controller_target;

		// US3AC7 US3AC8 Ban international sea routes or international land routes based on the corresponding modifiers
		auto origin_bans_sea_trade = state.world.nation_get_modifier_values(controller_origin, sys::national_mod_offsets::disallow_naval_trade) > 0.f;
		auto target_bans_sea_trade = state.world.nation_get_modifier_values(controller_target, sys::national_mod_offsets::disallow_naval_trade) > 0.f;
		auto sea_trade_banned = origin_bans_sea_trade || target_bans_sea_trade;
		auto origin_bans_land_trade = state.world.nation_get_modifier_values(controller_origin, sys::national_mod_offsets::disallow_land_trade) > 0.f;
		auto target_bans_land_trade = state.world.nation_get_modifier_values(controller_target, sys::national_mod_offsets::disallow_land_trade) > 0.f;
		auto land_trade_banned = origin_bans_land_trade || target_bans_land_trade;
		auto trade_banned = (is_sea_route && sea_trade_banned && !same_nation) || (!is_sea_route && land_trade_banned && !same_nation);

		auto merchant_cut = ve::select(same_nation, ve::fp_vector{ economy::merchant_cut_domestic }, ve::fp_vector{ economy::merchant_cut_foreign });

		auto export_tariff = ve::select(same_nation || !origin_tariff_is_applied, ve::fp_vector{ 0.f }, export_tariff_buffer.get(origin));
		auto import_tariff = ve::select(same_nation || !target_tariff_is_applied, ve::fp_vector{ 0.f }, import_tariff_buffer.get(target));

		ve::fp_vector distance = state.world.trade_route_get_distance_km(trade_route);

		// US3AC18
		auto trade_good_loss_mult = ve::max(0.f, 1.f - trade_goods_lost_per_km * distance);

		auto transport_price_per_km = ve::select(is_sea_route, state.world.market_get_naval_transportation_price(owner), state.world.market_get_land_transportation_price(owner));

		auto reset_route =
			trade_closed || blockaded || trade_banned
			|| !ve::apply([&](auto r) { return state.world.trade_route_is_valid(r); }, trade_route);

		for(auto c : state.world.in_commodity) {
			// US3AC19
			if(state.world.commodity_get_money_rgo(c) || state.world.commodity_get_is_local(c)) {
				continue;
			}
			if(
				state.world.commodity_get_rgo_amount(c) > 0.f
				&& !state.world.commodity_get_actually_exists_in_nature(c)
			) {
			                continue;
			}
			auto reset_route_commodity = reset_route;

			auto price_origin = adaptive_ve::market_get_price<true>(state, origin, c);
			auto price_target = adaptive_ve::market_get_price<true>(state, target, c);
			auto confidence_import = adaptive_ve::max(adaptive_ve::constant_value<ve::fp_vector>(0.5f), adaptive_ve::market_get_price_confidence<true>(state, target, c));
			auto confidence_export = adaptive_ve::max(adaptive_ve::constant_value<ve::fp_vector>(0.5f), adaptive_ve::market_get_price_confidence<true>(state, origin, c));
			auto price_import = price_target * confidence_import;
			auto price_export = price_origin / confidence_export;

			// ad valorem sea freight is a fraction of this commodity's own origin price
			auto transport_cost = trade_freight_unit_cost_vec(is_sea_route, distance, transport_price_per_km, price_origin);

			auto multiplier_value = trade_route_compute_change_multiplier<true, ve::fp_vector>(
				state, reset_route, is_sea_route,
				trade_route, c,
				export_tariff, import_tariff, transport_cost, budget, trade_good_loss_mult, merchant_cut, price_import, price_export
			);			
			auto decay_value = trade_route_compute_decay<true, ve::fp_vector>(
				state, reset_route, is_sea_route,
				trade_route, c,
				export_tariff, import_tariff, transport_cost, budget, trade_good_loss_mult, merchant_cut, price_import, price_export
			);
			auto next = trade_route_compute_next_volume<true, ve::fp_vector>(
				state, reset_route, is_sea_route,
				trade_route, c,
				export_tariff, import_tariff, transport_cost, budget, trade_good_loss_mult, merchant_cut, price_import, price_export,
				multiplier_value, decay_value
			);

			if(apply_trade_volume_target) {
				auto const current_volume = state.world.trade_route_get_volume(trade_route, c);
				auto const historical_demand = state.world.market_get_aggregated_demand_history(target, c);
				auto const historical_supply = state.world.market_get_aggregated_supply_history(target, c);
				auto const target_deficit = ve::max(ve::fp_vector{0.f}, historical_demand - historical_supply);
				auto const incoming_count = ve::max(ve::fp_vector{1.f}, incoming_route_count.get(target));
				auto const outgoing_count = ve::max(ve::fp_vector{1.f}, outgoing_route_count.get(origin));
				auto const origin_offer = ve::max(ve::fp_vector{0.f}, state.world.market_get_stockpile_sales(origin, c));
				auto const target_demand_share = target_deficit / incoming_count;
				auto const source_capacity_share = origin_offer / outgoing_count;
				auto const price_sold = adaptive_ve::select(owner == origin, price_import, price_export);
				auto const budget_capacity_share = budget / (economy::price_properties::commodity::min + price_sold) / incoming_count;
				auto const target_volume = ve::min(ve::min(target_demand_share, source_capacity_share), budget_capacity_share);
				auto const earn_per_unit = price_import * trade_good_loss_mult;
				auto const pay_per_unit = price_export * (1.f + export_tariff + merchant_cut) + price_import * import_tariff + transport_cost;
				auto const transport_availability = ve::select(is_sea_route,
					state.world.market_get_naval_transportation_demand_satisfaction(owner),
					state.world.market_get_land_transportation_demand_satisfaction(owner));
				auto const can_expand = (earn_per_unit > pay_per_unit)
					&& (transport_availability > 0.5f)
					&& (current_volume < target_volume);
				auto const ramped = current_volume + (target_volume - current_volume) * 0.02f;
				next = ve::select(reset_route, next, ve::select(can_expand, ve::max(next, ramped), next));
			}

			state.world.trade_route_set_volume(trade_route, c, next);
		}
	});

	if(trade_finance) {
		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			auto const ti = size_t(target.index());
			if(ti >= trade_income_ema.size())
				return;
			auto const income = trade_income_ema[ti];
			auto remaining_value = std::max(0.f, 0.5f * income - trade_import_used[ti]);
			auto const credit_limit = 60.f * income;
			auto const outstanding = std::max(0.f, -state.world.market_get_stockpile(target, economy::money));
			remaining_value = std::min(remaining_value, std::max(0.f, credit_limit - outstanding));
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto volume = state.world.trade_route_get_volume(route, cid);
				if(!(volume > 0.f) || !std::isfinite(volume))
					return;
				auto const price = state.world.market_get_price(target, cid);
				if(price > 0.f)
					volume = std::min(volume, remaining_value / price);
				volume = std::min(volume, std::max(0.f, state.world.market_get_stockpile(origin, cid)));
				if(!std::isfinite(volume) || !(volume > 0.f)) {
					state.world.trade_route_set_volume(route, cid, 0.f);
					return;
				}
				state.world.trade_route_set_volume(route, cid, volume);
				auto const value = volume * price;
				trade_import_used[ti] += value;
				trade_import_month[ti] += value;
				remaining_value = std::max(0.f, remaining_value - value);
			});
		});
	}

	if(trade_by_scarcity) {
		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route))
				return;
			if(state.world.trade_route_get_is_trade_forbidden(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const volume = state.world.trade_route_get_volume(route, cid);
				if(!(volume > 0.f) || !std::isfinite(volume))
					return;
				auto const stock_o = state.world.market_get_stockpile(origin, cid);
				auto const stock_t = state.world.market_get_stockpile(target, cid);
				auto const dem_o = state.world.market_get_demand(origin, cid);
				auto const dem_t = state.world.market_get_demand(target, cid);
				auto const eps = 0.000001f;
				auto const days_o = stock_o / (dem_o + eps);
				auto const days_t = stock_t / (dem_t + eps);
				auto const sc = std::max(-1.f, std::min(1.f, (days_o - days_t) / (days_o + days_t + eps)));
				if(!std::isfinite(sc))
					return;
				auto const change = (volume * 0.001f + 0.1f) * sc * trade_scarcity_scale;
				auto const next = std::max(0.f, volume + change);
				state.world.trade_route_set_volume(route, cid, next);
			});
		});
	}

	if(trade_physical_flow) {
		static dcon::commodity_id grain_id{};
		if(!grain_id) {
			state.world.for_each_commodity([&](dcon::commodity_id c) {
				if(text::produce_simple_string(state, state.world.commodity_get_name(c)) == "Grain")
					grain_id = c;
			});
		}
		if(grain_id) {
			auto const n = size_t(state.world.market_size());
			std::vector<float> surplus(n, 0.f), deficit(n, 0.f);
			std::vector<int32_t> out_routes(n, 0), in_routes(n, 0);
			state.world.for_each_market([&](dcon::market_id m) {
				auto const s = state.world.market_get_supply(m, grain_id);
				auto const c = state.world.market_get_consumption(m, grain_id);
				auto const d = state.world.market_get_demand(m, grain_id);
				auto const im = state.world.market_get_import(m, grain_id);
				if(std::isfinite(s) && std::isfinite(c))
					surplus[size_t(m.index())] = std::max(0.f, s - c);
				if(std::isfinite(d) && std::isfinite(im))
					deficit[size_t(m.index())] = std::max(0.f, d - im);
			});
			state.world.for_each_trade_route([&](dcon::trade_route_id r) {
				if(!state.world.trade_route_is_valid(r) || state.world.trade_route_get_is_trade_forbidden(r))
					return;
				out_routes[size_t(state.world.trade_route_get_origin(r).index())]++;
				in_routes[size_t(state.world.trade_route_get_target(r).index())]++;
			});
			state.world.for_each_trade_route([&](dcon::trade_route_id r) {
				if(!state.world.trade_route_is_valid(r) || state.world.trade_route_get_is_trade_forbidden(r))
					return;
				auto const o = state.world.trade_route_get_origin(r);
				auto const t = state.world.trade_route_get_target(r);
				auto const so = surplus[size_t(o.index())] / float(std::max(1, out_routes[size_t(o.index())]));
				auto const dt = deficit[size_t(t.index())] / float(std::max(1, in_routes[size_t(t.index())]));
				auto const floor = std::min(so, dt);
				if(floor > 0.f) {
					auto const cur = state.world.trade_route_get_volume(r, grain_id);
					if(std::isfinite(cur) && floor > cur)
						state.world.trade_route_set_volume(r, grain_id, floor);
				}
			});
		}
	}
}


void update_trade_routes_consumption(sys::state& state) {
	auto const total_commodities = state.world.commodity_size();

	// Register trade demand on goods
	concurrency::parallel_for(uint32_t(0), total_commodities, [&](uint32_t k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		if(state.world.commodity_get_money_rgo(cid)) {
			return;
		}
		state.world.for_each_trade_route([&](auto trade_route) {
			auto origin = state.world.trade_route_get_origin(trade_route);
			auto current_volume = state.world.trade_route_get_volume(trade_route, cid);
			register_demand(state, origin, cid, current_volume);
		});
	});

	// Transportation demand
	state.world.for_each_trade_route([&](auto trade_route) {
		auto owner = state.world.trade_route_get_owner(trade_route);
		auto distance = state.world.trade_route_get_distance_km(trade_route);
		state.world.for_each_commodity([&](auto cid){
			auto volume = state.world.trade_route_get_volume(trade_route, cid);
			if(state.world.trade_route_get_is_sea_route(trade_route)) {
				auto demand = state.world.market_get_naval_transportation_demand(owner);
				state.world.market_set_naval_transportation_demand(owner, demand + volume * distance);
			} else {
				auto demand = state.world.market_get_land_transportation_demand(owner);
				state.world.market_set_land_transportation_demand(owner, demand + volume * distance);
			}
		});
	});
}

// CAUTION: when we generate trade demand for a good, we promise to pay money to local producers during the next tick
// it means that during economy update, rgo profits were calculated according to this promise
// so local producers ALREADY received money for their production and local market take only a "merchant cut"

template<typename TRADE_ROUTE>
trade_and_tariff<TRADE_ROUTE> explain_trade_route_commodity_internal(
	sys::state const& state,
	TRADE_ROUTE trade_route,
	tariff_data<TRADE_ROUTE>& additional_data,
	dcon::commodity_id cid
) {
	using VALUE = typename std::conditional_t<ve::is_vector_type_s<TRADE_ROUTE>::value, ve::fp_vector, float>;
	auto current_volume = state.world.trade_route_get_volume(trade_route, cid);

	auto owner = state.world.trade_route_get_owner(trade_route);

	auto origin = state.world.trade_route_get_origin(trade_route);
	auto target = state.world.trade_route_get_target(trade_route);

	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);

	auto actual_n_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto actual_n_target = state.world.province_get_nation_from_province_control(capital_target);

	actual_n_origin = adaptive_ve::select(actual_n_origin != dcon::nation_id{ }, actual_n_origin, n_origin);
	actual_n_target = adaptive_ve::select(actual_n_target != dcon::nation_id{ }, actual_n_target, n_target);

	auto price_origin = state.world.market_get_price(origin, cid);
	auto price_target = state.world.market_get_price(target, cid);

	auto sat = state.world.market_get_actual_probability_to_buy(origin, cid);

	auto absolute_volume = sat * adaptive_ve::abs(current_volume);

	auto import_amount = absolute_volume * additional_data.loss;

	auto sea_route = state.world.trade_route_get_is_sea_route(trade_route);
	auto transport_cost = adaptive_ve::select(sea_route, state.world.market_get_naval_transportation_price(owner), state.world.market_get_land_transportation_price(owner));

	const VALUE cut_domestic = economy::merchant_cut_domestic;
	const VALUE cut_foreign = economy::merchant_cut_foreign;

	auto merchant_cut = adaptive_ve::select(actual_n_origin == actual_n_target, cut_domestic, cut_foreign);

	auto export_tariff = additional_data.export_tariff;
	auto import_tariff = additional_data.import_tariff;

	auto origin_tariff = price_origin * export_tariff;
	auto origin_merchant_cut = price_origin * merchant_cut;
	auto target_tariff = price_target * import_tariff;

	/*
	When transporting a unit of commodity:
	Pay for commodity at origin, taxes to governments.
	The payment will be negated by sales from the stockpile, so actually only tariffs are paid
	When arriving at target port, receive arbitrage pay and unload goods to their stockpiles.
	Transport cost is paid separately via mechanics of ships maintenance
	*/

	return {
		.origin = origin,
		.target = target,
		.origin_nation = actual_n_origin,
		.target_nation = actual_n_target,

		.amount_origin = absolute_volume,
		.amount_target = import_amount,

		.tariff_origin = absolute_volume * price_origin * export_tariff,
		.tariff_target = import_amount * price_target * import_tariff,

		.tariff_rate_origin = export_tariff,
		.tariff_rate_target = import_tariff,

		.price_origin = price_origin,
		.price_target = price_target,

		.transport_cost = transport_cost,

		.transportaion_loss = additional_data.loss,
		.distance = additional_data.distance,

		// Origin earns mostly via normal supply demand matching
		.origin_earn_per_unit = origin_merchant_cut,
		// Target pays the normal price
		.target_spend_per_unit = price_target,
		// We receive payment of target
		.owner_earn_per_unit = price_target,
		// We have to actually pay the export price and taxes
		.owner_spend_per_unit = price_origin + origin_tariff + target_tariff + origin_merchant_cut
	};
}

trade_and_tariff<ve::contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::contiguous_tags<dcon::trade_route_id> trade_route,
	tariff_data<ve::contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
) {
	return explain_trade_route_commodity_internal(state, trade_route, additional_data, cid);
}
trade_and_tariff<ve::partial_contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::partial_contiguous_tags<dcon::trade_route_id> trade_route,
	tariff_data<ve::partial_contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
) {
	return explain_trade_route_commodity_internal(state, trade_route, additional_data, cid);
}

bool is_trade_route_relevant(sys::state& state, dcon::trade_route_id trade_route, dcon::nation_id n) {
	auto origin = state.world.trade_route_get_origin(trade_route);
	auto target = state.world.trade_route_get_target(trade_route);
	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);
	auto controller_capital_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto controller_capital_target = state.world.province_get_nation_from_province_control(capital_target);

	controller_capital_origin = controller_capital_origin ? controller_capital_origin : n_origin;
	controller_capital_target = controller_capital_target ? controller_capital_target : n_target;

	if(controller_capital_origin == n) {
		return true;
	}
	if(controller_capital_target == n) {
		return true;
	}
	return false;
}

trade_and_tariff<dcon::trade_route_id> explain_trade_route_commodity(sys::state const& state, dcon::trade_route_id trade_route, dcon::commodity_id cid) {
	auto current_volume = state.world.trade_route_get_volume(trade_route, cid);
	auto origin = state.world.trade_route_get_origin(trade_route);
	auto target = state.world.trade_route_get_target(trade_route);

	auto owner = state.world.trade_route_get_owner(trade_route);

	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);
	auto controller_capital_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto controller_capital_target = state.world.province_get_nation_from_province_control(capital_target);

	controller_capital_origin = controller_capital_origin ? controller_capital_origin : n_origin;
	controller_capital_target = controller_capital_target ? controller_capital_target : n_target;

	auto origin_apply_tariff = state.world.trade_route_get_is_tariff_applied_origin(trade_route);
	auto target_apply_tariff = state.world.trade_route_get_is_tariff_applied_target(trade_route);

	auto sat = state.world.market_get_actual_probability_to_buy(origin, cid);
	auto absolute_volume = sat * current_volume;
	auto distance = state.world.trade_route_get_distance_km(trade_route);

	auto trade_good_loss_mult = std::max(0.f, 1.f - trade_goods_lost_per_km * distance);
	auto import_amount = absolute_volume * trade_good_loss_mult;

	auto is_sea_route = state.world.trade_route_get_is_sea_route(trade_route);
	auto transport_cost = is_sea_route ? state.world.market_get_naval_transportation_price(owner) : state.world.market_get_land_transportation_price(owner);

	auto export_tariff = origin_apply_tariff ? effective_tariff_export_rate(state, controller_capital_origin, origin) : 0.f;
	auto import_tariff = target_apply_tariff ? effective_tariff_import_rate(state, controller_capital_target, target) : 0.f;

	auto price_origin = state.world.market_get_price(origin, cid);
	auto price_target = state.world.market_get_price(target, cid);

	auto merchant_cut = controller_capital_origin == controller_capital_target ? economy::merchant_cut_domestic : economy::merchant_cut_foreign;

	auto origin_tariff = price_origin * export_tariff;
	auto origin_merchant_cut = price_origin * merchant_cut;
	auto target_tariff = price_target * import_tariff;

	/*
	When transporting a unit of commodity:
	Pay for commodity at origin, taxes to governments.
	The payment will be negated by sales from the stockpile, so actually only tariffs are paid
	When arriving at target port, receive arbitrage pay and unload goods to their stockpiles.
	Transport cost is paid separately via mechanics of ships maintenance
	*/

	return {
		.origin = origin,
		.target = target,
		.origin_nation = controller_capital_origin,
		.target_nation = controller_capital_target,

		.amount_origin = absolute_volume,
		.amount_target = import_amount,
		.tariff_origin = absolute_volume * price_origin * export_tariff,
		.tariff_target = import_amount * price_target * import_tariff,

		.tariff_rate_origin = export_tariff,
		.tariff_rate_target = import_tariff,

		.price_origin = price_origin,
		.price_target = price_target,

		.transport_cost = transport_cost,

		.transportaion_loss = trade_good_loss_mult,
		.distance = distance,

		// Origin earns mostly via normal supply demand matching
		.origin_earn_per_unit = origin_merchant_cut,
		// Target pays the normal price
		.target_spend_per_unit = price_target,
		// We receive payment of target
		.owner_earn_per_unit = price_target,
		// We have to actually pay the export price and taxes
		.owner_spend_per_unit = price_origin + origin_tariff + target_tariff + origin_merchant_cut
	};
}

// DO NOT USE OUTSIDE OF UI
std::vector<trade_breakdown_item> explain_national_tariff(sys::state& state, dcon::nation_id n, bool import_flag, bool export_flag) {
	std::vector<trade_breakdown_item> result;
	auto buffer_volume_per_nation = state.world.nation_make_vectorizable_float_buffer();
	auto buffer_tariff_per_nation = state.world.nation_make_vectorizable_float_buffer();

	state.world.for_each_commodity([&](dcon::commodity_id cid) {
		state.world.execute_serial_over_nation([&](auto nids) {
			buffer_volume_per_nation.set(nids, 0.f);
			buffer_tariff_per_nation.set(nids, 0.f);
		});

		state.world.for_each_trade_route([&](auto route) {
			if(!is_trade_route_relevant(state, route, n)) return;
			trade_and_tariff route_data = explain_trade_route_commodity(state, route, cid);

			if(import_flag && route_data.target_nation == n) {
				buffer_volume_per_nation.get(route_data.origin_nation) += route_data.amount_target;
				buffer_tariff_per_nation.get(route_data.origin_nation) += route_data.tariff_target;
			}

			if(export_flag && route_data.origin_nation == n) {
				buffer_volume_per_nation.get(route_data.target_nation) += route_data.amount_origin;
				buffer_tariff_per_nation.get(route_data.target_nation) += route_data.tariff_origin;
			}
		});

		state.world.for_each_nation([&](auto nid) {
			trade_breakdown_item item = {
				.trade_partner = nid,
				.commodity = cid,
				.traded_amount = buffer_volume_per_nation.get(nid),
				.tariff = buffer_tariff_per_nation.get(nid)
			};

			if(item.traded_amount == 0.f || item.tariff < 0.001f) {
				return;
			}

			result.push_back(item);
		});
	});

	return result;
}



template <typename TRADE_ROUTE>
auto explain_trade_route(
	sys::state& state,
	TRADE_ROUTE trade_route,
	// Paid for each unit x price
	ve::vectorizable_buffer<float, dcon::market_id>& export_tariff,
	// Paid for each unit x price
	ve::vectorizable_buffer<float, dcon::market_id>& import_tariff
) {
	using VALUE = typename std::conditional_t<ve::is_vector_type_s<TRADE_ROUTE>::value, ve::fp_vector, float>;

	auto origin = state.world.trade_route_get_origin(trade_route);
	auto target = state.world.trade_route_get_target(trade_route);
	auto owner = state.world.trade_route_get_owner(trade_route);

	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);	
	auto tariff_applied_origin = state.world.trade_route_get_is_tariff_applied_origin(trade_route);
	auto tariff_applied_target = state.world.trade_route_get_is_tariff_applied_target(trade_route);
	auto distance = state.world.trade_route_get_distance_km(trade_route);
	auto trade_good_loss_mult = adaptive_ve::max<VALUE>(0.f, 1.f - economy::trade_goods_lost_per_km * distance);

	auto is_sea_route = state.world.trade_route_get_is_sea_route(trade_route);

	auto transport_cost_per_distance_weight = adaptive_ve::select(is_sea_route, state.world.market_get_naval_transportation_price(owner), state.world.market_get_land_transportation_price(owner));

	auto export_tariff_origin = ve::select(tariff_applied_origin, export_tariff.get(origin), 0.f);
	auto import_tariff_target = ve::select(tariff_applied_target, import_tariff.get(target), 0.f);

	tariff_data<TRADE_ROUTE> result{
		.applies_export_tariff = tariff_applied_origin,
		.applies_import_tariff = tariff_applied_target,
		.export_tariff = export_tariff_origin,
		.import_tariff = import_tariff_target,

		.distance = distance,
		.loss = trade_good_loss_mult,
		.transportation_cost = transport_cost_per_distance_weight * distance
	};

	return result;
}

// ===== LOCAL FORK (not upstream) =====
// LOCAL TRACE (ALICE_TRACE_TRADE_LEDGER=1): monthly, read-only route x commodity trade account.
// It is written after fill_trade_buffers(), so the quantities and prices are the ones used to build
// the market export/import, tariff and arbitrage buffers for the next settlement stage. The trace
// deliberately keeps the upstream and experimental account readings side by side: a positive route
// volume is not evidence that the origin seller was paid or that the target buyer was debited.
static bool const trace_trade_ledger = []() {
	auto s = std::getenv("ALICE_TRACE_TRADE_LEDGER");
	return s && std::atoi(s) != 0;
}();
static std::string const trade_ledger_good_filter = []() {
	auto s = std::getenv("ALICE_TRADE_LEDGER_GOODS");
	return s ? std::string(s) : std::string{};
}();
static double const trade_ledger_min_value = []() {
	auto s = std::getenv("ALICE_TRADE_LEDGER_MIN_VALUE");
	return s ? std::max(0.0, std::atof(s)) : 1.0;
}();
static FILE* trade_ledger_route_file = nullptr;
static FILE* trade_ledger_market_file = nullptr;
static bool trade_ledger_files_init = false;
static std::string trade_ledger_last_date;

static dcon::nation_id trade_ledger_market_nation(sys::state const& state, dcon::market_id market) {
	auto const zone = state.world.market_get_zone_from_local_market(market);
	auto const capital = state.world.state_instance_get_capital(zone);
	auto const controller = state.world.province_get_nation_from_province_control(capital);
	return controller ? controller : state.world.state_instance_get_nation_from_state_ownership(zone);
}

static bool trade_ledger_good_selected(std::string const& name) {
	if(trade_ledger_good_filter.empty())
		return true;
	size_t begin = 0;
	while(begin <= trade_ledger_good_filter.size()) {
		auto const end = trade_ledger_good_filter.find(',', begin);
		auto const token = trade_ledger_good_filter.substr(
			begin, end == std::string::npos ? std::string::npos : end - begin);
		if(!token.empty() && name.find(token) != std::string::npos)
			return true;
		if(end == std::string::npos)
			break;
		begin = end + 1;
	}
	return false;
}

static void dump_trade_ledger_trace(
	sys::state const& state,
	bool presimulation,
	const ve::vectorizable_buffer<float, dcon::market_id>& export_tariff_buffer,
	const ve::vectorizable_buffer<float, dcon::market_id>& import_tariff_buffer
) {
	if(!trace_trade_ledger || presimulation)
		return;

	auto const ymd = state.current_date.to_ymd(state.start_date);
	if(ymd.day != 1)
		return;

	char datebuf[16];
	std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", ymd.year, ymd.month, ymd.day);
	if(trade_ledger_last_date == datebuf)
		return;
	trade_ledger_last_date = datebuf;

	if(!trade_ledger_files_init) {
		trade_ledger_files_init = true;
		auto const route_path = std::getenv("ALICE_TRADE_LEDGER_FILE");
		auto const market_path = std::getenv("ALICE_TRADE_LEDGER_MARKET_FILE");
		if(route_path) {
			trade_ledger_route_file = std::fopen(route_path, "w");
			if(trade_ledger_route_file)
				std::fprintf(trade_ledger_route_file,
					"date,route,origin_market,target_market,owner_market,origin_nation,target_nation,owner_nation,is_sea,distance_km,good,good_id,route_volume,sat,moved_qty,arrived_qty,loss_rate,price_origin,price_target,origin_value_shipment,target_value_arrived,origin_export_credit_scheduled,target_import_debit_scheduled,export_tariff_rate,import_tariff_rate,export_tariff_value,import_tariff_value_shipment,import_tariff_value_arrived,merchant_cut_rate,merchant_cut_value,transport_cost_per_unit,transport_cost_value,owner_arbitrage_scheduled,origin_removed_qty,target_added_qty,origin_market_money,target_market_money,origin_market_export_cut,target_market_import_spending,origin_market_tariff,target_market_tariff,trade_forbidden\n");
		}
		if(market_path) {
			trade_ledger_market_file = std::fopen(market_path, "w");
			if(trade_ledger_market_file)
				std::fprintf(trade_ledger_market_file,
					"date,market,owner_market,owner_nation,export_qty,import_qty,export_value,import_value,net_value,export_cut,import_spending,arbitrage,tariff_collected,market_money,net_cash_scheduled\n");
		}
	}
	if(!trade_ledger_route_file && !trade_ledger_market_file)
		return;

	auto const commodity_count = size_t(state.world.commodity_size());
	std::vector<std::string> good_names(commodity_count);
	std::vector<char> good_selected(commodity_count, 0);
	state.world.for_each_commodity([&](dcon::commodity_id cid) {
		auto const i = size_t(cid.index());
		if(i >= commodity_count)
			return;
		good_names[i] = text::produce_simple_string(state, state.world.commodity_get_name(cid));
		good_selected[i] = !state.world.commodity_get_money_rgo(cid)
			&& !state.world.commodity_get_is_local(cid)
			&& trade_ledger_good_selected(good_names[i]) ? 1 : 0;
	});

	if(trade_ledger_route_file) {
	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		if(!state.world.trade_route_is_valid(route))
			return;
		auto const origin = state.world.trade_route_get_origin(route);
		auto const target = state.world.trade_route_get_target(route);
		auto const owner = state.world.trade_route_get_owner(route);
		if(!origin || !target)
			return;

		auto const is_sea = state.world.trade_route_get_is_sea_route(route);
		auto const distance = double(state.world.trade_route_get_distance_km(route));
		auto const origin_nation = trade_ledger_market_nation(state, origin);
		auto const target_nation = trade_ledger_market_nation(state, target);
		auto const owner_nation = trade_ledger_market_nation(state, owner);

		for(auto cid : state.world.in_commodity) {
			auto const ci = size_t(cid.id.index());
			if(ci >= commodity_count || !good_selected[ci])
				continue;
			auto const volume = double(state.world.trade_route_get_volume(route, cid));
			if(!(volume > 0.0) || !std::isfinite(volume))
				continue;

			auto const sat = double(state.world.market_get_actual_probability_to_buy(origin, cid));
			auto const moved = volume * sat;
			auto const price_origin = double(state.world.market_get_price(origin, cid));
			auto const price_target = double(state.world.market_get_price(target, cid));
			auto const export_rate = state.world.trade_route_get_is_tariff_applied_origin(route)
				? double(export_tariff_buffer.get(origin)) : 0.0;
			auto const import_rate = state.world.trade_route_get_is_tariff_applied_target(route)
				? double(import_tariff_buffer.get(target)) : 0.0;
			auto const loss_multiplier = std::max(0.0, 1.0 - distance * economy::trade_goods_lost_per_km);
			auto const loss_rate = 1.0 - loss_multiplier;
			auto const arrived = moved * loss_multiplier;
			auto const cut_rate = double(state.world.trade_route_get_origin_cut_rate(route));
			auto const cut_value = moved * price_origin * cut_rate;
			auto const origin_credit = trade_account ? moved * price_origin : cut_value;
			auto const target_debit = moved * price_target;
			auto const origin_value = moved * price_origin;
			auto const target_value = arrived * price_target;
			if(std::max(std::max(origin_value, target_value), target_debit) < trade_ledger_min_value)
				continue;
			auto const export_tariff_value = moved * price_origin * export_rate;
			auto const import_tariff_value = moved * price_target * import_rate;
			auto const import_tariff_arrived = arrived * price_target * import_rate;
			auto const transport_per_unit = trade_freight_unit_cost(bool(is_sea), distance, double(is_sea
				? state.world.market_get_naval_transportation_price(owner)
				: state.world.market_get_land_transportation_price(owner)), price_origin);
			auto const owner_arbitrage = (owner == origin || owner == target)
				? moved * (price_target * (1.0 - import_rate)
					- price_origin * (1.0 + export_rate + (trade_account ? 0.0 : cut_rate)))
				: 0.0;
			auto const origin_removed = (trade_account || trade_goods_conservation) ? moved : 0.0;

			std::fprintf(trade_ledger_route_file,
				"%s,%d,%d,%d,%d,%d,%d,%d,%d,%.6g,%s,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d\n",
				datebuf, route.index(), origin.index(), target.index(), owner.id.index(),
				origin_nation.index(), target_nation.index(), owner_nation.index(), is_sea ? 1 : 0,
				distance, good_names[ci].c_str(), cid.id.index(), volume,
				sat,
				moved, arrived, loss_rate, price_origin, price_target,
				origin_value, target_value, origin_credit, target_debit,
				export_rate, import_rate, export_tariff_value, import_tariff_value,
				import_tariff_arrived, cut_rate, cut_value, transport_per_unit,
				transport_per_unit * moved, owner_arbitrage, origin_removed, arrived,
				double(state.world.market_get_stockpile(origin, economy::money)),
				double(state.world.market_get_stockpile(target, economy::money)),
				double(state.world.market_get_export_cut(origin)),
				double(state.world.market_get_import_spending(target)),
				double(state.world.market_get_tariff_collected(origin)),
				double(state.world.market_get_tariff_collected(target)),
				state.world.trade_route_get_is_trade_forbidden(route) ? 1 : 0);
		}
	});
		std::fflush(trade_ledger_route_file);
	}

	if(trade_ledger_market_file) {
		state.world.for_each_market([&](dcon::market_id market) {
			double export_qty = 0.0;
			double import_qty = 0.0;
			double export_value = 0.0;
			double import_value = 0.0;
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				auto const ci = size_t(cid.index());
				if(ci >= commodity_count || !good_selected[ci])
					return;
				auto const price = double(state.world.market_get_price(market, cid));
				auto const exported = double(state.world.market_get_export(market, cid));
				auto const imported = double(state.world.market_get_import(market, cid));
				if(!std::isfinite(price) || !std::isfinite(exported) || !std::isfinite(imported))
					return;
				export_qty += exported;
				import_qty += imported;
				export_value += price * exported;
				import_value += price * imported;
			});
			auto const export_cut = double(state.world.market_get_export_cut(market));
			auto const import_spending = double(state.world.market_get_import_spending(market));
			auto const arbitrage = double(state.world.market_get_arbitrage(market));
			auto const tariff = double(state.world.market_get_tariff_collected(market));
			auto const money = double(state.world.market_get_stockpile(market, economy::money));
			auto const owner_nation = trade_ledger_market_nation(state, market);
			std::fprintf(trade_ledger_market_file,
				"%s,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
				datebuf, market.index(), market.index(), owner_nation.index(), export_qty, import_qty,
				export_value, import_value, export_value - import_value, export_cut,
				import_spending, arbitrage, tariff, money,
				export_cut - import_spending + arbitrage + tariff);
		});
		std::fflush(trade_ledger_market_file);
	}
}

// LOCAL EXPERIMENT (ALICE_EXP_TRADE_ESCROW=1): only goods whose sellers are registered may be
// exported. Market supply has already been reset when trade buffers run, so the surviving seller
// batches are the authoritative capacity. Without this cap, a route can move goods while the
// corresponding seller batches are missing, producing a delivery with no payee.
static double trade_seller_supply_capacity(
	sys::state const& state,
	dcon::market_id market,
	dcon::commodity_id commodity
) {
	auto const* batches = market_good_supply_batches(state, market, commodity);
	if(!batches)
		return 0.0;
	double known = 0.0;
	for(auto const& batch : *batches) {
		if(batch.quantity > 0.f && std::isfinite(batch.quantity))
			known += double(batch.quantity);
	}
	if(!(known > 0.0) || !std::isfinite(known))
		return 0.0;
	return known;
}

void fill_trade_buffers(
	sys::state& state,
	const ve::vectorizable_buffer<float, dcon::market_id>& export_tariff_buffer,
	const ve::vectorizable_buffer<float, dcon::market_id>& import_tariff_buffer,
	bool presimulation
) {
	uint32_t total_commodities = state.world.commodity_size();
	bool const trade_escrow_active = trade_escrow_enabled();
	std::vector<double> trade_import_scale;
	std::vector<double> trade_cash_budget;
	std::vector<double> trade_credit_budget;
	std::vector<double> trade_seller_capacity;
	std::vector<double> trade_origin_scale;
	if(trade_escrow_active) {
		auto const market_count = size_t(state.world.market_size());
		trade_escrow_day_sellers = 0.0;
		trade_escrow_day_costs = 0.0;
		trade_escrow_day_merchant = 0.0;
		trade_escrow_day_funding = 0.0;
		trade_escrow_day_cash = 0.0;
		trade_escrow_day_credit = 0.0;
		trade_escrow_day_unmatched = 0.0;
		trade_escrow_day_blocked = 0.0;
		trade_escrow_day_blocked_seller = 0.0;
		trade_escrow_day_blocked_stock = 0.0;
		trade_escrow_day_blocked_budget = 0.0;
		trade_escrow_day_blocked_unprofitable = 0.0;
		trade_escrow_day_blocked_partial = 0.0;
		trade_escrow_day_blocked_other = 0.0;
		trade_escrow_day_margin_origin = 0.0;
		trade_escrow_day_margin_target = 0.0;
		trade_escrow_day_margin_third = 0.0;
		if(trade_credit_outstanding.size() != market_count)
			trade_credit_outstanding.assign(market_count, 0.0);
		std::vector<double> scheduled_import(market_count, 0.0);
		trade_import_scale.assign(market_count, 0.0);
		trade_cash_budget.assign(market_count, 0.0);
		trade_credit_budget.assign(market_count, 0.0);
		trade_seller_capacity.assign(market_count * size_t(total_commodities), 0.0);
		trade_origin_scale.assign(market_count * size_t(total_commodities), 1.0);
		state.world.for_each_market([&](dcon::market_id market) {
			auto const base = size_t(market.index()) * size_t(total_commodities);
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const index = base + size_t(cid.index());
				if(index < trade_seller_capacity.size())
					trade_seller_capacity[index] = trade_seller_supply_capacity(state, market, cid);
			});
		});

		state.world.for_each_trade_route([&](auto route) {
			if(!state.world.trade_route_is_valid(route) || state.world.trade_route_get_is_trade_forbidden(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			if(!origin || !target)
				return;
			auto const distance = state.world.trade_route_get_distance_km(route);
			auto const lost_modifier = std::max(0.f, 1.f - distance * trade_goods_lost_per_km);
			state.world.for_each_commodity([&](auto cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const sat = state.world.market_get_actual_probability_to_buy(origin, cid);
				auto const volume = state.world.trade_route_get_volume(route, cid) * sat;
				auto const price_target = state.world.market_get_price(target, cid);
				if(!(volume > 0.f) || !std::isfinite(volume) || !(price_target > 0.f) || !std::isfinite(price_target))
					return;
				auto const capacity_index = size_t(origin.index()) * size_t(total_commodities) + size_t(cid.index());
				auto const seller_capacity = capacity_index < trade_seller_capacity.size() ? trade_seller_capacity[capacity_index] : 0.0;
				if(!(seller_capacity > 0.0))
					return;
				auto const moved = std::min(double(volume), seller_capacity);
				scheduled_import[size_t(target.index())] += moved * double(lost_modifier) * double(price_target);
			});
		});

		state.world.for_each_market([&](dcon::market_id market) {
			auto const index = size_t(market.index());
			if(index >= market_count)
				return;
			auto const cash = double(state.world.market_get_stockpile(market, economy::money));
			auto const income = double(std::max(0.f, state.world.market_get_total_earn(market)));
			auto const credit_limit = trade_credit_days * income;
			auto const credit_available = std::max(0.0, credit_limit - trade_credit_outstanding[index]);
			trade_cash_budget[index] = std::max(0.0, std::isfinite(cash) ? cash : 0.0);
			trade_credit_budget[index] = std::isfinite(credit_available) ? credit_available : 0.0;
			auto const available = trade_cash_budget[index] + trade_credit_budget[index];
			trade_import_scale[index] = scheduled_import[index] > 0.0
				? std::min(1.0, available / scheduled_import[index])
				: 0.0;
		});

		// Allocate the physical origin stock across all routes before settling them. Without this,
		// route iteration order decides which targets receive goods and the rest are starved.
		std::vector<double> desired_export(market_count * size_t(total_commodities), 0.0);
		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route) || state.world.trade_route_get_is_trade_forbidden(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			if(!origin || !target)
				return;
			auto const target_index = size_t(target.index());
			if(target_index >= market_count)
				return;
			auto const scale = double(trade_import_scale[target_index]);
			if(!(scale > 0.0) || !std::isfinite(scale))
				return;
			auto const origin_index = size_t(origin.index());
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const requested = double(state.world.trade_route_get_volume(route, cid))
					* double(state.world.market_get_actual_probability_to_buy(origin, cid)) * scale;
				if(!(requested > 0.0) || !std::isfinite(requested))
					return;
				auto const index = origin_index * size_t(total_commodities) + size_t(cid.index());
				if(index < desired_export.size())
					desired_export[index] += requested;
			});
		});
		state.world.for_each_market([&](dcon::market_id market) {
			auto const market_index = size_t(market.index());
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const index = market_index * size_t(total_commodities) + size_t(cid.index());
				if(index >= desired_export.size() || index >= trade_origin_scale.size())
					return;
				auto const desired = desired_export[index];
				if(!(desired > 0.0) || !std::isfinite(desired))
					return;
				auto const stock = double(std::max(0.f, state.world.market_get_stockpile(market, cid)));
				auto const capacity = index < trade_seller_capacity.size() ? trade_seller_capacity[index] : 0.0;
				auto const available = std::min(stock, capacity);
				trade_origin_scale[index] = available > 0.0
					? std::min(1.0, available / desired)
					: 0.0;
			});
		});
	}
	if(!trade_escrow_active) {
	concurrency::parallel_for(uint32_t(1), total_commodities, [&](uint32_t k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		state.world.for_each_trade_route([&](auto route) {
			auto origin = state.world.trade_route_get_origin(route);
			auto target = state.world.trade_route_get_target(route);
			if(!origin || !target)
				return;
			auto distance = state.world.trade_route_get_distance_km(route);
			auto volume = state.world.trade_route_get_volume(route, cid);
			auto lost_modifier = std::max(0.f, 1.f - distance * trade_goods_lost_per_km);
			auto sat = state.world.market_get_actual_probability_to_buy(origin, cid);
			auto const scale = trade_escrow_active
				? float(trade_import_scale[size_t(target.index())])
				: 1.f;
			auto const moved = sat * volume * scale;
			state.world.market_set_export(origin, cid, state.world.market_get_export(origin, cid) + moved);
				auto const import_base = trade_import_account_fix
					? state.world.market_get_import(target, cid)
					: state.world.market_get_export(target, cid);
					state.world.market_set_import(target, cid, import_base + moved * lost_modifier);
			state.world.market_set_stockpile(target, cid, state.world.market_get_stockpile(target, cid) + moved * lost_modifier);
			if(presimulation && state.world.commodity_get_actually_exists_in_nature(cid)) {
				state.world.market_set_stockpile(target, cid, state.world.market_get_stockpile(target, cid) + volume);
			}
		});
	});
	uint32_t total_markets = state.world.market_size();
	concurrency::parallel_for(uint32_t(1), total_markets, [&](uint32_t k) {
		{
			dcon::market_id origin{ dcon::market_id::value_base_t(k) };
			auto total_earn_export = 0.f;
			auto total_origin_tariff = 0.f;
			auto total_arbitrage = 0.f;

			state.world.market_for_each_trade_route_as_origin(origin, [&](auto route) {
				auto origin = state.world.trade_route_get_origin(route);
				auto target = state.world.trade_route_get_target(route);
				auto owner = state.world.trade_route_get_owner(route);
				auto cut = state.world.trade_route_get_origin_cut_rate(route);
				auto export_rate =
					state.world.trade_route_get_is_tariff_applied_origin(route)
					? export_tariff_buffer.get(origin) : 0.f;
				auto import_rate =
					state.world.trade_route_get_is_tariff_applied_target(route)
					? import_tariff_buffer.get(target) : 0.f;


				state.world.for_each_commodity([&](auto cid) {
					auto sat = state.world.market_get_actual_probability_to_buy(origin, cid);
						auto volume = state.world.trade_route_get_volume(route, cid) * sat
							* (trade_escrow_active ? float(trade_import_scale[size_t(target.index())]) : 1.f);
					auto price_origin = state.world.market_get_price(origin, cid);
					auto price_target = state.world.market_get_price(target, cid);
					if(trade_account) {
						// origin gets the goods value; the merchant margin is target - origin - tariffs
						total_earn_export = total_earn_export + volume * price_origin;
						total_origin_tariff = total_origin_tariff + volume * (price_origin * export_rate);
						if(owner == origin) {
							total_arbitrage = total_arbitrage + volume * (price_target * (1.f - import_rate) - price_origin * (1.f + export_rate));
						}
					} else {
						total_earn_export = total_earn_export + volume * (price_origin * cut);
						total_origin_tariff = total_origin_tariff + volume * (price_origin * export_rate);
						if(owner == origin) {
							total_arbitrage = total_arbitrage + volume * (price_target * (1.f - import_rate) - price_origin * (1.f + export_rate + cut));
						}
					}
				});
			});

			state.world.market_set_arbitrage(origin, state.world.market_get_arbitrage(origin) + total_arbitrage);
			state.world.market_set_export_cut(origin, state.world.market_get_export_cut(origin) + total_earn_export);
			if(!trade_escrow_active)
				state.world.market_set_tariff_collected(origin, state.world.market_get_tariff_collected(origin) + total_origin_tariff);
		}
		{
			dcon::market_id target{ dcon::market_id::value_base_t(k) };
			auto total_spend_import = 0.f;
			auto total_target_tariff = 0.f;
			auto total_arbitrage = 0.f;

			state.world.market_for_each_trade_route_as_target(target, [&](auto route) {
				auto origin = state.world.trade_route_get_origin(route);
				auto target = state.world.trade_route_get_target(route);
				auto owner = state.world.trade_route_get_owner(route);
				auto cut = state.world.trade_route_get_origin_cut_rate(route);
				auto export_rate =
					state.world.trade_route_get_is_tariff_applied_origin(route)
					? export_tariff_buffer.get(origin) : 0.f;
				auto import_rate = 
					state.world.trade_route_get_is_tariff_applied_target(route)
					? import_tariff_buffer.get(target) : 0.f;

				state.world.for_each_commodity([&](auto cid) {
					auto sat = state.world.market_get_actual_probability_to_buy(origin, cid);
						auto volume = state.world.trade_route_get_volume(route, cid) * sat
							* (trade_escrow_active ? float(trade_import_scale[size_t(target.index())]) : 1.f);
					auto price_origin = state.world.market_get_price(origin, cid);
					auto price_target = state.world.market_get_price(target, cid);

					total_spend_import = total_spend_import + volume * (price_target);
					total_target_tariff = total_target_tariff + volume * (price_target * import_rate);
					if(owner == target) {
						auto const margin = trade_account
							? volume * (price_target * (1.f - import_rate) - price_origin * (1.f + export_rate))
							: volume * (price_target * (1.f - import_rate) - price_origin * (1.f + export_rate + cut));
						total_arbitrage = total_arbitrage + margin;
					}
				});
			});

			state.world.market_set_arbitrage(target, state.world.market_get_arbitrage(target) + total_arbitrage);
			state.world.market_set_import_spending(target, state.world.market_get_import_spending(target) + total_spend_import);
			if(!trade_escrow_active)
				state.world.market_set_tariff_collected(target, state.world.market_get_tariff_collected(target) + total_target_tariff);
		}
	});

	if(trade_account || trade_goods_conservation || trade_escrow_active) {
		// general goods conservation: every exported unit leaves the origin stockpile
		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			if(!origin || !target)
				return;
			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const sat = state.world.market_get_actual_probability_to_buy(origin, cid);
				auto const scale = trade_escrow_active
					? float(trade_import_scale[size_t(target.index())])
					: 1.f;
				auto const moved = state.world.trade_route_get_volume(route, cid) * sat * scale;
				if(!(moved > 0.f) || !std::isfinite(moved))
					return;
				auto const stock = state.world.market_get_stockpile(origin, cid);
				state.world.market_set_stockpile(origin, cid, std::max(0.f, stock - moved));
			});
		});
	}
	}

	if(trade_escrow_active) {
		auto const market_count = size_t(state.world.market_size());
		std::vector<double> remaining_cash = trade_cash_budget;
		std::vector<double> remaining_credit = trade_credit_budget;
		auto credit_market_cash = [&](dcon::market_id market, double amount) {
			if(!(amount > 0.0) || !std::isfinite(amount))
				return;
			auto const current = double(state.world.market_get_stockpile(market, economy::money));
			auto const next = current + amount;
			if(std::isfinite(next))
				state.world.market_set_stockpile(market, economy::money, float(next));
		};

		state.world.for_each_trade_route([&](dcon::trade_route_id route) {
			if(!state.world.trade_route_is_valid(route) || state.world.trade_route_get_is_trade_forbidden(route))
				return;
			auto const origin = state.world.trade_route_get_origin(route);
			auto const target = state.world.trade_route_get_target(route);
			auto owner = state.world.trade_route_get_owner(route);
			if(!origin || !target)
				return;
			if(!owner)
				owner = origin;
			auto const distance = double(state.world.trade_route_get_distance_km(route));
			auto const lost_modifier = std::max(0.0, 1.0 - distance * double(trade_goods_lost_per_km));
			auto const export_rate = state.world.trade_route_get_is_tariff_applied_origin(route)
				? double(export_tariff_buffer.get(origin)) : 0.0;
			auto const import_rate = state.world.trade_route_get_is_tariff_applied_target(route)
				? double(import_tariff_buffer.get(target)) : 0.0;
			auto const transport_per_km = double(state.world.trade_route_get_is_sea_route(route)
				? state.world.market_get_naval_transportation_price(owner)
				: state.world.market_get_land_transportation_price(owner));
			auto const cut_rate = double(state.world.trade_route_get_origin_cut_rate(route));
			auto const target_index = size_t(target.index());
			if(target_index >= market_count)
				return;
			auto const origin_index = size_t(origin.index());

			state.world.for_each_commodity([&](dcon::commodity_id cid) {
				if(cid == economy::money || state.world.commodity_get_money_rgo(cid) || state.world.commodity_get_is_local(cid))
					return;
				auto const price_origin = double(state.world.market_get_price(origin, cid));
				auto const price_target = double(state.world.market_get_price(target, cid));
				if(!(price_origin > 0.0) || !(price_target > 0.0) || !std::isfinite(price_origin) || !std::isfinite(price_target))
					return;
				auto const sat = state.world.market_get_actual_probability_to_buy(origin, cid);
				auto const scale = float(trade_import_scale[target_index]);
				auto const capacity_index = origin_index * size_t(total_commodities) + size_t(cid.index());
				auto const origin_scale = capacity_index < trade_origin_scale.size() ? trade_origin_scale[capacity_index] : 0.0;
				auto const requested_moved = double(state.world.trade_route_get_volume(route, cid)) * double(sat) * double(scale) * origin_scale;
				auto const requested_arrived = requested_moved * lost_modifier;
				if(!(requested_moved > 0.0) || !(requested_arrived > 0.0) || !std::isfinite(requested_moved) || !std::isfinite(requested_arrived))
					return;
				auto const potential_value = requested_arrived * price_target;
				if(!(potential_value > 0.0) || !std::isfinite(potential_value))
					return;

				auto const seller_capacity = capacity_index < trade_seller_capacity.size() ? trade_seller_capacity[capacity_index] : 0.0;
				auto const origin_stock = double(std::max(0.f, state.world.market_get_stockpile(origin, cid)));
				auto const unit_buyer_value = lost_modifier * price_target;
				auto const unit_transport = trade_freight_unit_cost(bool(state.world.trade_route_get_is_sea_route(route)), distance, transport_per_km, price_origin);
				auto const unit_costs = price_origin + price_origin * export_rate
					+ unit_transport + unit_buyer_value * import_rate;
				auto const add_blocked = [&](double& reason, double value) {
					if(value > 0.0 && std::isfinite(value)) {
						reason += value;
						trade_escrow_day_blocked += value;
					}
				};
				auto const capacity_moved = std::min(requested_moved, seller_capacity);
				add_blocked(trade_escrow_day_blocked_seller, (requested_moved - capacity_moved) * unit_buyer_value);
				auto const stock_moved = std::min(capacity_moved, origin_stock);
				add_blocked(trade_escrow_day_blocked_stock, (capacity_moved - stock_moved) * unit_buyer_value);
				auto moved = stock_moved;
				if(!(moved > 0.0) || !std::isfinite(moved) || !(unit_buyer_value > 0.0)
					|| !std::isfinite(unit_buyer_value) || !std::isfinite(unit_costs)) {
					add_blocked(trade_escrow_day_blocked_other, moved * unit_buyer_value);
					return;
				}
				if(unit_costs > unit_buyer_value + 1e-12) {
					add_blocked(trade_escrow_day_blocked_unprofitable, moved * unit_buyer_value);
					return;
				}
				auto const available_funding = remaining_cash[target_index] + remaining_credit[target_index];
				auto const affordable_moved = available_funding > 0.0 && std::isfinite(available_funding)
					? available_funding / unit_buyer_value
					: 0.0;
				add_blocked(trade_escrow_day_blocked_budget,
					std::max(0.0, moved - affordable_moved) * unit_buyer_value);
				moved = std::min(moved, affordable_moved);
				if(!(moved > 0.0) || !std::isfinite(moved)) {
					return;
				}

				auto const seller_requested = moved * price_origin;
				double seller_unmatched = 0.0;
				auto const seller_paid = settle_trade_seller_payment(
					state, origin, cid, seller_requested, seller_unmatched);
				trade_escrow_day_unmatched += seller_unmatched;
				if(!(seller_paid > 0.0) || !std::isfinite(seller_paid)) {
					add_blocked(trade_escrow_day_blocked_partial, moved * unit_buyer_value);
					return;
				}

				auto const matched_fraction = seller_requested > 0.0
					? std::clamp(seller_paid / seller_requested, 0.0, 1.0)
					: 0.0;
				add_blocked(trade_escrow_day_blocked_partial,
					moved * (1.0 - matched_fraction) * unit_buyer_value);
				moved *= matched_fraction;
				auto const arrived = moved * lost_modifier;
				if(!(moved > 0.0) || !(arrived > 0.0) || !std::isfinite(moved) || !std::isfinite(arrived)) {
					return;
				}

				auto const export_tariff = moved * price_origin * export_rate;
				auto const import_tariff = arrived * price_target * import_rate;
				auto const transport = moved * unit_transport;
				auto const costs = export_tariff + import_tariff + transport;
				auto const buyer_nominal = arrived * price_target;
				auto const merchant_margin = std::max(0.0, buyer_nominal - costs - seller_paid);
				auto const funding = seller_paid + costs + merchant_margin;
				if(!(funding > 0.0) || !std::isfinite(funding) || !std::isfinite(costs) || !std::isfinite(merchant_margin)) {
					add_blocked(trade_escrow_day_blocked_other, moved * unit_buyer_value);
					return;
				}

				auto cash_part = std::min(funding, remaining_cash[target_index]);
				auto credit_part = funding - cash_part;
				if(!(credit_part >= 0.0) || credit_part > remaining_credit[target_index] + 1e-9) {
					add_blocked(trade_escrow_day_blocked_other, moved * unit_buyer_value);
					return;
				}
				remaining_cash[target_index] -= cash_part;
				remaining_credit[target_index] -= credit_part;
				if(cash_part > 0.0)
					state.world.market_set_stockpile(target, economy::money,
						float(double(state.world.market_get_stockpile(target, economy::money)) - cash_part));
				trade_credit_outstanding[target_index] += credit_part;
				trade_escrow_day_sellers += seller_paid;
				trade_escrow_day_costs += costs;
				trade_escrow_day_merchant += merchant_margin;
				trade_escrow_day_funding += funding;
				trade_escrow_day_cash += cash_part;
				trade_escrow_day_credit += credit_part;
				if(owner == origin)
					trade_escrow_day_margin_origin += merchant_margin;
				else if(owner == target)
					trade_escrow_day_margin_target += merchant_margin;
				else
					trade_escrow_day_margin_third += merchant_margin;

				auto const origin_stock_now = double(std::max(0.f, state.world.market_get_stockpile(origin, cid)));
				state.world.market_set_stockpile(origin, cid, float(std::max(0.0, origin_stock_now - moved)));
				state.world.market_set_stockpile(target, cid,
					state.world.market_get_stockpile(target, cid) + float(arrived));
				state.world.market_set_export(origin, cid,
					state.world.market_get_export(origin, cid) + float(moved));
				state.world.market_set_import(target, cid,
					state.world.market_get_import(target, cid) + float(arrived));
				state.world.market_set_export_cut(origin,
					state.world.market_get_export_cut(origin) + float(moved * price_origin * cut_rate));
				state.world.market_set_import_spending(target,
					state.world.market_get_import_spending(target) + float(buyer_nominal));
				state.world.market_set_arbitrage(owner,
					state.world.market_get_arbitrage(owner) + float(transport + merchant_margin));
				if(capacity_index < trade_seller_capacity.size())
					trade_seller_capacity[capacity_index] = std::max(0.0, seller_capacity - moved);

				state.world.market_set_tariff_collected(origin,
					state.world.market_get_tariff_collected(origin) + float(export_tariff));
				state.world.market_set_tariff_collected(target,
					state.world.market_get_tariff_collected(target) + float(import_tariff));
				credit_market_cash(owner, transport + merchant_margin);
			});
		});
		if(trace_trade_escrow && !presimulation) {
			auto const ymd = state.current_date.to_ymd(state.start_date);
			fprintf(stderr,
				"TRADEESCROW %04d-%02d-%02d sellers=%.6f costs=%.6f merchant=%.6f funding=%.6f "
				"cash=%.6f credit=%.6f unmatched=%.6f blocked=%.6f blocked_seller=%.6f "
				"blocked_stock=%.6f blocked_budget=%.6f blocked_unprofitable=%.6f "
				"blocked_partial=%.6f blocked_other=%.6f margin_origin=%.6f "
				"margin_target=%.6f margin_third=%.6f outstanding=%.6f lender=%.6f\n",
				ymd.year, ymd.month, ymd.day, trade_escrow_day_sellers, trade_escrow_day_costs,
				trade_escrow_day_merchant, trade_escrow_day_funding, trade_escrow_day_cash,
				trade_escrow_day_credit, trade_escrow_day_unmatched, trade_escrow_day_blocked,
				trade_escrow_day_blocked_seller, trade_escrow_day_blocked_stock,
				trade_escrow_day_blocked_budget, trade_escrow_day_blocked_unprofitable,
				trade_escrow_day_blocked_partial, trade_escrow_day_blocked_other,
				trade_escrow_day_margin_origin, trade_escrow_day_margin_target,
				trade_escrow_day_margin_third, trade_credit_total(), trade_credit_lender_balance());
			fflush(stderr);
		}
	}

	dump_trade_ledger_trace(state, presimulation, export_tariff_buffer, import_tariff_buffer);
}

}
