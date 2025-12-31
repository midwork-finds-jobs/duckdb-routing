// Simplified valhalla_build_tiles implementation
// Downloads PBF and generates config - tiles must be built externally

#include "duckdb.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <fstream>
#include <sstream>

namespace duckdb {

/**
 * Generate complete Valhalla config for given tile directory
 * Based on valhalla_build_config output with all required sections
 */
static std::string GenerateValhallaConfig(const std::string &tile_dir) {
	std::ostringstream config;
	config << "{\n";

	// additional_data
	config << "  \"additional_data\": {\n";
	config << "    \"elevation\": \"" << tile_dir << "/elevation/\"\n";
	config << "  },\n";

	// httpd
	config << "  \"httpd\": {\n";
	config << "    \"service\": {\n";
	config << "      \"listen\": \"tcp://*:8002\",\n";
	config << "      \"loopback\": \"ipc:///tmp/loopback\"\n";
	config << "    }\n";
	config << "  },\n";

	// loki
	config << "  \"loki\": {\n";
	config << "    \"actions\": [\"locate\", \"route\", \"height\", \"sources_to_targets\", \"optimized_route\", "
	          "\"isochrone\", \"trace_route\", \"trace_attributes\", \"transit_available\", \"expansion\", "
	          "\"centroid\", \"status\"],\n";
	config << "    \"logging\": { \"type\": \"std_out\", \"color\": true },\n";
	config << "    \"service\": { \"proxy\": \"ipc:///tmp/loki\" },\n";
	config << "    \"service_defaults\": {\n";
	config << "      \"heading_tolerance\": 60,\n";
	config << "      \"min_zoom_road_class\": [7, 7, 8, 10, 11, 11, 13, 14],\n";
	config << "      \"minimum_reachability\": 50,\n";
	config << "      \"node_snap_tolerance\": 5,\n";
	config << "      \"radius\": 0,\n";
	config << "      \"search_cutoff\": 35000,\n";
	config << "      \"street_side_max_distance\": 1000,\n";
	config << "      \"street_side_tolerance\": 5\n";
	config << "    },\n";
	config << "    \"use_connectivity\": true\n";
	config << "  },\n";

	// meili
	config << "  \"meili\": {\n";
	config << "    \"auto\": { \"search_radius\": 50, \"turn_penalty_factor\": 200 },\n";
	config << "    \"default\": {\n";
	config << "      \"beta\": 3,\n";
	config << "      \"breakage_distance\": 2000,\n";
	config << "      \"geometry\": false,\n";
	config << "      \"gps_accuracy\": 5.0,\n";
	config << "      \"interpolation_distance\": 10,\n";
	config << "      \"max_route_distance_factor\": 5,\n";
	config << "      \"max_search_radius\": 100,\n";
	config << "      \"search_radius\": 50,\n";
	config << "      \"sigma_z\": 4.07\n";
	config << "    },\n";
	config << "    \"grid\": { \"cache_size\": 100240, \"size\": 500 },\n";
	config << "    \"logging\": { \"type\": \"std_out\", \"color\": true },\n";
	config << "    \"mode\": \"auto\",\n";
	config << "    \"service\": { \"proxy\": \"ipc:///tmp/meili\" }\n";
	config << "  },\n";

	// mjolnir
	config << "  \"mjolnir\": {\n";
	config << "    \"tile_dir\": \"" << tile_dir << "\",\n";
	config << "    \"tile_extract\": \"" << tile_dir << "/tiles.tar\",\n";
	config << "    \"admin\": \"" << tile_dir << "/admin.sqlite\",\n";
	config << "    \"timezone\": \"" << tile_dir << "/tz_world.sqlite\",\n";
	config << "    \"traffic_extract\": \"" << tile_dir << "/traffic.tar\",\n";
	config << "    \"max_cache_size\": 1000000000,\n";
	config << "    \"id_table_size\": 1300000000,\n";
	config << "    \"hierarchy\": true,\n";
	config << "    \"shortcuts\": true,\n";
	config << "    \"include_driving\": true,\n";
	config << "    \"include_bicycle\": true,\n";
	config << "    \"include_pedestrian\": true,\n";
	config << "    \"data_processing\": {\n";
	config << "      \"infer_internal_intersections\": true,\n";
	config << "      \"infer_turn_channels\": true,\n";
	config << "      \"apply_country_overrides\": true,\n";
	config << "      \"use_admin_db\": true\n";
	config << "    },\n";
	config << "    \"logging\": { \"type\": \"std_out\", \"color\": true }\n";
	config << "  },\n";

	// odin
	config << "  \"odin\": {\n";
	config << "    \"logging\": { \"type\": \"std_out\", \"color\": true },\n";
	config << "    \"markup_formatter\": { \"markup_enabled\": false },\n";
	config << "    \"service\": { \"proxy\": \"ipc:///tmp/odin\" }\n";
	config << "  },\n";

	// service_limits
	config << "  \"service_limits\": {\n";
	config << "    \"allow_hard_exclusions\": false,\n";
	config << "    \"auto\": { \"max_distance\": 5000000.0, \"max_locations\": 20, \"max_matrix_distance\": 400000.0, "
	          "\"max_matrix_location_pairs\": 2500 },\n";
	config << "    \"bicycle\": { \"max_distance\": 500000.0, \"max_locations\": 50, \"max_matrix_distance\": "
	          "200000.0, \"max_matrix_location_pairs\": 2500 },\n";
	config << "    \"pedestrian\": { \"max_distance\": 250000.0, \"max_locations\": 50, \"max_matrix_distance\": "
	          "200000.0, \"max_matrix_location_pairs\": 2500, \"max_transit_walking_distance\": 10000, "
	          "\"min_transit_walking_distance\": 1 },\n";
	config << "    \"isochrone\": { \"max_contours\": 4, \"max_distance\": 25000.0, \"max_distance_contour\": 200, "
	          "\"max_locations\": 1, \"max_time_contour\": 120 },\n";
	config << "    \"status\": { \"allow_verbose\": false },\n";
	config << "    \"trace\": { \"max_alternates\": 3, \"max_alternates_shape\": 100, \"max_distance\": 200000.0, "
	          "\"max_gps_accuracy\": 100.0, \"max_search_radius\": 100.0, \"max_shape\": 16000 },\n";
	config << "    \"skadi\": { \"max_shape\": 750000, \"min_resample\": 10.0 },\n";
	config << "    \"max_alternates\": 2,\n";
	config << "    \"max_distance_disable_hierarchy_culling\": 0,\n";
	config << "    \"max_exclude_locations\": 50,\n";
	config << "    \"max_exclude_polygons_length\": 10000,\n";
	config << "    \"max_linear_cost_edges\": 50000,\n";
	config << "    \"max_radius\": 200,\n";
	config << "    \"max_reachability\": 100,\n";
	config << "    \"max_timedep_distance\": 500000,\n";
	config << "    \"max_timedep_distance_matrix\": 0,\n";
	config << "    \"min_linear_cost_factor\": 1\n";
	config << "  },\n";

	// statsd
	config << "  \"statsd\": {\n";
	config << "    \"port\": 8125,\n";
	config << "    \"prefix\": \"valhalla\"\n";
	config << "  },\n";

	// thor
	config << "  \"thor\": {\n";
	config << "    \"logging\": { \"type\": \"std_out\", \"color\": true, \"long_request\": 110.0 },\n";
	config << "    \"service\": { \"proxy\": \"ipc:///tmp/thor\" },\n";
	config << "    \"source_to_target_algorithm\": \"select_optimal\"\n";
	config << "  }\n";

	config << "}\n";
	return config.str();
}

/**
 * valhalla_build_tiles scalar function
 * Downloads PBF if remote, generates config
 * Returns path to config file
 */
static void ValhallaBuildTilesFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto &fs = FileSystem::GetFileSystem(context);

	auto count = args.size();
	UnifiedVectorFormat pbf_format, output_format;
	args.data[0].ToUnifiedFormat(count, pbf_format);
	args.data[1].ToUnifiedFormat(count, output_format);

	auto pbf_vec = UnifiedVectorFormat::GetData<string_t>(pbf_format);
	auto output_vec = UnifiedVectorFormat::GetData<string_t>(output_format);

	for (idx_t i = 0; i < count; i++) {
		auto pbf_idx = pbf_format.sel->get_index(i);
		auto output_idx = output_format.sel->get_index(i);

		if (pbf_format.validity.RowIsValid(pbf_idx) && output_format.validity.RowIsValid(output_idx)) {
			auto pbf_path = pbf_vec[pbf_idx].GetString();
			auto output_dir = output_vec[output_idx].GetString();

			try {
				// Create output directory
				if (!fs.DirectoryExists(output_dir)) {
					fs.CreateDirectory(output_dir);
				}

				// Check if input is remote
				bool is_remote = (pbf_path.substr(0, 7) == "http://" || pbf_path.substr(0, 8) == "https://");

				std::string local_pbf_path;
				if (is_remote) {
					// Download to output directory
					local_pbf_path = std::string(output_dir) + "/input.osm.pbf";

					auto handle = fs.OpenFile(pbf_path, FileFlags::FILE_FLAGS_READ);
					auto file_size = fs.GetFileSize(*handle);
					auto buffer = unique_ptr<char[]>(new char[file_size]);
					fs.Read(*handle, buffer.get(), file_size);

					std::ofstream out(local_pbf_path, std::ios::binary);
					if (!out) {
						throw IOException("Failed to write PBF file: " + local_pbf_path);
					}
					out.write(buffer.get(), file_size);
					out.close();
				} else {
					local_pbf_path = pbf_path;
				}

				// Generate config
				std::string config_path = std::string(output_dir) + "/valhalla.json";
				std::string config_content = GenerateValhallaConfig(output_dir);

				std::ofstream config_file(config_path);
				if (!config_file) {
					throw IOException("Failed to write config file: " + config_path);
				}
				config_file << config_content;
				config_file.close();

				// Build tiles using valhalla CLI
				// NOTE: Using std::system() here because Valhalla mjolnir C++ API changes
				// significantly between versions, making direct C++ integration fragile.
				// The CLI tool provides a stable interface.
				std::ostringstream cmd;
				cmd << "valhalla_build_tiles -c \"" << config_path << "\" \"" << local_pbf_path << "\" 2>&1";

				int ret = std::system(cmd.str().c_str());
				if (ret != 0) {
					throw IOException("valhalla_build_tiles command failed with exit code " + std::to_string(ret));
				}

				// Return config path
				result.SetValue(i, Value(config_path));

			} catch (std::exception &e) {
				throw IOException("valhalla_build_tiles failed: " + std::string(e.what()));
			}
		} else {
			FlatVector::SetNull(result, i, true);
		}
	}
}

/**
 * Register the valhalla_build_tiles function
 */
void RegisterValhallaBuildTilesFunction(ExtensionLoader &loader) {
	ScalarFunction build_tiles("valhalla_build_tiles",
	                           {LogicalType::VARCHAR, LogicalType::VARCHAR}, // (pbf_path, output_dir)
	                           LogicalType::VARCHAR,                         // Returns config path
	                           ValhallaBuildTilesFun);

	build_tiles.null_handling = FunctionNullHandling::SPECIAL_HANDLING;

	loader.RegisterFunction(build_tiles);
}

} // namespace duckdb
