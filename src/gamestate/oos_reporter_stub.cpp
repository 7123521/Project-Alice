#include <string>

#include "dcon_oos_reporter_generated.hpp"

// The generated reporter is the multiplayer desync checker plus the load-game reset. A
// headless simulation reaches neither: generate_oos_report is only called from the
// network resync path, and reset_data only from state::clear_unsaved_data(), which runs
// when a save is loaded. The generated implementation is 284 functions over ~28k lines
// and costs about 9 minutes of compiler time by itself, which is most of the build, so
// the headless targets link this stub instead of compiling it.
namespace dcon {

std::string generate_oos_report(const dcon::data_container&, const dcon::data_container&, const dcon::load_record&) {
	return std::string{};
}

void reset_data(dcon::data_container&, const dcon::load_record&) {
}

} // namespace dcon
