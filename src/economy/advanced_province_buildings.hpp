#pragma once
#include "constants_dcon.hpp"
#include "system_state_forward.hpp"
#include "dcon_generated_ids.hpp"

// Advanced province buildings are a future replacement for province buildings
// They can hire people, expand and produce services (or, in future, local modifiers)
// They could be owned by government at expense of national budget
// They could produce "free services"
// They have current size which depends on national funding and profitability
// National funding sets size to funding / cost of worker * province weight
// Private funding increases size when operation is profitable and shrinks when it is not
// Currently they are hardcoded as "enums with properties"
// Future work should be directed toward moving their definitions into content files and integration with dcon

namespace services {

namespace list {
inline constexpr int32_t education = 0;
inline constexpr int32_t port_capacity = 1;
inline constexpr int32_t urban_housing = 2;
inline constexpr int32_t total = 3;
}

void initialize_size_of_dcon_arrays(sys::state& state);
void update_price(sys::state& state);
void reset_supply(sys::state& state);
void reset_demand(sys::state& state);
void reset_price(sys::state& state);
void match_supply_and_demand(sys::state& state);
}

namespace advanced_province_buildings {

namespace list {
inline constexpr int32_t schools_and_universities = 0;
inline constexpr int32_t civilian_ports = 1;
inline constexpr int32_t local_cities_and_towns= 2;
inline constexpr int32_t total = 3;
}

struct advanced_building_definition {
	int32_t throughput_labour_type;
	int32_t output;
	float output_amount;
	/*
	Some of APB require maintenance.
	The cost of maintenance is expressed in terms of construction cost.
	*/
	float maintenance_rate = 0.f;
	economy::province_building_type associated_building = economy::province_building_type::last; // to inherit costs
	bool requires_labor = true;
};

const extern advanced_building_definition definitions[services::list::total];
void initialize_size_of_dcon_arrays(sys::state& state);
void update_consumption(sys::state& state);
void update_private_size(sys::state& state);
void update_national_size(sys::state& state);
void update_production(sys::state& state);
void update_profit_and_refund(sys::state& state);

// ===== LOCAL FORK (not upstream) =====
// LOCAL EXPERIMENT (ALICE_EXP_PROPERTY_INCOME_FLOW=1): the daily profit computed by
// update_profit_and_refund(), retained separately from the accumulated private savings.
// The default path does not populate this buffer and callers must not rely on it.
float get_daily_private_profit(dcon::province_id pid, int32_t building);

// LOCAL TRACE (ALICE_TRACE_APB=1): read-only monthly accounting snapshot. No economic effect.
void trace_apb_state(sys::state& state, char const* tag);

}
