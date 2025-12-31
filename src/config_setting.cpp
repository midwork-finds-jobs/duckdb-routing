// Configuration setting for Valhalla config path
// Allows using: SET valhalla_tiles = 'path/to/valhalla.json';

#include "duckdb.hpp"
#include "duckdb/main/config.hpp"
#include <fstream>

// External functions from valhalla wrapper
extern "C" {
void *valhalla_init(const char *config_path);
void valhalla_free(void *router);
const char *valhalla_last_error();
}

namespace duckdb {

// Global state for Valhalla router (defined in travel_time_extension.cpp)
extern void *g_router;
extern std::mutex g_router_mutex;
extern std::string g_config_path;

/**
 * Callback when valhalla_tiles setting is changed
 */
static void SetValhallaTiles(ClientContext &context, SetScope scope, Value &parameter) {
	auto path = parameter.ToString();

	// Smart path detection:
	// If path is a directory, append /valhalla.json
	// If path ends with .json, use as-is
	std::string config_path = path;

	if (config_path.find(".json") == std::string::npos) {
		// No .json extension - assume it's a directory
		if (config_path.back() != '/') {
			config_path += '/';
		}
		config_path += "valhalla.json";
	}

	std::lock_guard<std::mutex> lock(g_router_mutex);

	try {
		// Check if file exists (C++11 compatible)
		std::ifstream test_file(config_path);
		if (!test_file.good()) {
			throw InvalidInputException("Config file not found: " + config_path);
		}
		test_file.close();

		// Free existing router if different config
		if (g_router && g_config_path != config_path) {
			valhalla_free(g_router);
			g_router = nullptr;
		}

		// Load if not already loaded
		if (!g_router) {
			g_router = valhalla_init(config_path.c_str());
			if (!g_router) {
				throw InvalidInputException("Failed to load Valhalla config: " + config_path + " - " +
				                            std::string(valhalla_last_error()));
			}
			g_config_path = config_path;
		}

	} catch (std::exception &e) {
		throw InvalidInputException("Failed to load config from " + config_path + ": " + std::string(e.what()));
	}
}

/**
 * Getter for valhalla_tiles setting
 */
static Value GetValhallaTiles(ClientContext &context) {
	std::lock_guard<std::mutex> lock(g_router_mutex);

	if (g_config_path.empty()) {
		return Value(); // NULL if not set
	}

	return Value(g_config_path);
}

/**
 * Register the valhalla_tiles setting
 */
void RegisterValhallaTilesSetting(DatabaseInstance &instance) {
	auto &config = DBConfig::GetConfig(instance);

	config.AddExtensionOption("valhalla_tiles", "Path to Valhalla tiles directory or config file", LogicalType::VARCHAR,
	                          Value(),          // default_value
	                          SetValhallaTiles, // set callback
	                          SetScope::SESSION // default scope
	);
}

} // namespace duckdb
