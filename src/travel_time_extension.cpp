#define DUCKDB_EXTENSION_MAIN

#include "travel_time_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

// Valhalla wrapper C API
#include "valhalla_wrapper.h"

#include <string>
#include <vector>
#include <mutex>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace duckdb {

// Global router instance (thread-safe via mutex)
static std::mutex g_router_mutex;
static ValhallaRouter *g_router = nullptr;
static std::string g_config_path;

// ============ Helper Functions ============

static void EnsureRouterLoaded(const char *costing) {
	if (!g_router || !valhalla_is_ready(g_router)) {
		throw InvalidInputException("Valhalla router not loaded. Call travel_time_load_config() first.");
	}
}

// Check if a logical type is a GEOMETRY type (from spatial extension)
static bool IsGeometryType(const LogicalType &type) {
	// Check type alias
	if (type.HasAlias()) {
		auto alias = type.GetAlias();
		if (alias == "GEOMETRY" || alias == "geometry" || alias == "POINT" || alias == "LINESTRING" ||
		    alias == "POLYGON" || alias == "WKB_BLOB" || alias == "wkb_blob") {
			return true;
		}
	}
	// Check if it's an extension type with GEOMETRY-like name
	if (type.id() == LogicalTypeId::USER) {
		auto type_name = type.ToString();
		if (type_name == "GEOMETRY" || type_name == "geometry" || type_name == "WKB_BLOB" || type_name == "wkb_blob") {
			return true;
		}
	}
	return false;
}

// DuckDB spatial geometry_t internal format parser
// Extracts centroid coordinates from GEOMETRY type without spatial extension dependency
//
// Observed format for ST_Point (32 bytes):
//   - 12 bytes: header/padding (all zeros for simple POINT)
//   - 4 bytes: geometry type (uint32_t little-endian, 1 = POINT)
//   - 8 bytes: X coordinate (double, longitude)
//   - 8 bytes: Y coordinate (double, latitude)
//
struct DuckDBGeometryParser {
	// Geometry types from spatial extension
	enum GeomType : uint32_t {
		POINT = 1,
		LINESTRING = 2,
		POLYGON = 3,
		MULTIPOINT = 4,
		MULTILINESTRING = 5,
		MULTIPOLYGON = 6,
		GEOMETRYCOLLECTION = 7
	};

	// Try to extract lat/lon from a GEOMETRY blob
	// Returns true if successful, false otherwise
	static bool ExtractPointCoords(const char *data, idx_t size, double &lat, double &lon) {
		// POINT geometry is exactly 32 bytes
		if (size < 32) {
			return ExtractFirstCoordinate(data, size, lat, lon);
		}

		const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);

		// Read geometry type at offset 12 (little-endian uint32)
		uint32_t geom_type = ptr[12] | (ptr[13] << 8) | (ptr[14] << 16) | (ptr[15] << 24);

		// Check if it's a POINT
		if (geom_type != POINT) {
			// For non-point geometries, try to find first coordinate pair
			return ExtractFirstCoordinate(data, size, lat, lon);
		}

		// Read coordinates at offset 16 (X = lon, Y = lat)
		const double *coords = reinterpret_cast<const double *>(ptr + 16);
		lon = coords[0];
		lat = coords[1];

		// Validate coordinates
		if (lon >= -180.0 && lon <= 180.0 && lat >= -90.0 && lat <= 90.0) {
			return true;
		}

		return false;
	}

	// Fallback: try to find first double pair that looks like coordinates
	static bool ExtractFirstCoordinate(const char *data, idx_t size, double &lat, double &lon) {
		if (size < 24)
			return false; // Need at least some header + coords

		const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);

		// Try different offsets to find valid coordinate pairs
		for (size_t offset = 8; offset + 16 <= size; offset += 8) {
			const double *coords = reinterpret_cast<const double *>(ptr + offset);
			double x = coords[0];
			double y = coords[1];

			// Check if values look like valid coordinates
			// Longitude: -180 to 180, Latitude: -90 to 90
			if (x >= -180.0 && x <= 180.0 && y >= -90.0 && y <= 90.0) {
				lon = x;
				lat = y;
				return true;
			}
		}
		return false;
	}
};

// Extract geometry data for routing - returns true if WKB, false if WKT
// For WKB: data points to binary data, size is set
// For WKT: data points to string, size is string length
struct GeometryData {
	const char *data;
	idx_t size;
	bool is_wkb;
	// For parsed GEOMETRY type, store extracted WKT
	std::string parsed_wkt;
	bool uses_parsed_wkt;
};

// Check if data looks like standard WKB format
// WKB format: 1 byte order + 4 bytes type + coordinates
// Byte order: 0x00 = big endian, 0x01 = little endian
// For POINT: type = 1, followed by 16 bytes (2 doubles)
static bool LooksLikeWkb(const char *data, idx_t size) {
	if (size < 21)
		return false; // Minimum for POINT WKB

	const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);
	unsigned char byte_order = ptr[0];

	// Must be 0x00 (big endian) or 0x01 (little endian)
	if (byte_order != 0x00 && byte_order != 0x01)
		return false;

	// Read geometry type (bytes 1-4)
	uint32_t geom_type;
	if (byte_order == 0x01) {
		// Little endian
		geom_type = ptr[1] | (ptr[2] << 8) | (ptr[3] << 16) | (ptr[4] << 24);
	} else {
		// Big endian
		geom_type = (ptr[1] << 24) | (ptr[2] << 16) | (ptr[3] << 8) | ptr[4];
	}

	// Valid WKB geometry types are 1-7 (or with SRID flag 0x20000000)
	uint32_t base_type = geom_type & 0x0FFFFFFF;
	if (base_type < 1 || base_type > 7)
		return false;

	// Additional check: DuckDB internal format has 12 zero bytes at start
	// If bytes 1-11 are all zeros, it's likely internal format, not WKB
	bool all_zeros = true;
	for (int i = 1; i < 12 && i < (int)size; i++) {
		if (ptr[i] != 0) {
			all_zeros = false;
			break;
		}
	}
	if (all_zeros && size >= 32)
		return false; // Likely internal format

	return true;
}

// Check if data looks like DuckDB spatial GEOMETRY internal format
// Format: 12 bytes header + 4 bytes type (at offset 12) + coordinates
static bool LooksLikeGeometryInternal(const char *data, idx_t size) {
	// POINT is 32 bytes, other types are larger
	if (size < 32)
		return false;

	const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);

	// Check that first 12 bytes look like zeros or valid header
	// (simple geometries often have zeros here)
	bool header_looks_ok = true;
	for (int i = 0; i < 12 && header_looks_ok; i++) {
		// Allow zeros or small values in header
		if (ptr[i] > 16)
			header_looks_ok = false;
	}

	if (!header_looks_ok)
		return false;

	// Read geometry type at offset 12 (little-endian uint32)
	uint32_t geom_type = ptr[12] | (ptr[13] << 8) | (ptr[14] << 16) | (ptr[15] << 24);

	// DuckDB spatial uses geometry types 1-7
	return geom_type >= 1 && geom_type <= 7;
}

static bool ExtractGeometryData(Vector &vec, idx_t row, const LogicalType &type, GeometryData &out) {
	out.uses_parsed_wkt = false;

	if (FlatVector::IsNull(vec, row)) {
		return false;
	}

	// VARCHAR = WKT string
	if (type.id() == LogicalTypeId::VARCHAR) {
		string_t val = FlatVector::GetData<string_t>(vec)[row];
		out.data = val.GetData();
		out.size = val.GetSize();
		out.is_wkb = false;
		return true;
	}

	// BLOB = could be WKB or internal GEOMETRY format
	if (type.id() == LogicalTypeId::BLOB) {
		string_t val = FlatVector::GetData<string_t>(vec)[row];
		const char *raw_data = val.GetData();
		idx_t raw_size = val.GetSize();

		// Check if it's standard WKB
		if (LooksLikeWkb(raw_data, raw_size)) {
			out.data = raw_data;
			out.size = raw_size;
			out.is_wkb = true;
			return true;
		}

		// Check if it's DuckDB internal GEOMETRY format
		if (LooksLikeGeometryInternal(raw_data, raw_size)) {
			double lat, lon;
			if (DuckDBGeometryParser::ExtractPointCoords(raw_data, raw_size, lat, lon)) {
				out.parsed_wkt = "POINT(" + std::to_string(lon) + " " + std::to_string(lat) + ")";
				out.data = out.parsed_wkt.c_str();
				out.size = out.parsed_wkt.size();
				out.is_wkb = false;
				out.uses_parsed_wkt = true;
				return true;
			}
		}

		// Fallback: try to find coordinates
		double lat, lon;
		if (DuckDBGeometryParser::ExtractFirstCoordinate(raw_data, raw_size, lat, lon)) {
			out.parsed_wkt = "POINT(" + std::to_string(lon) + " " + std::to_string(lat) + ")";
			out.data = out.parsed_wkt.c_str();
			out.size = out.parsed_wkt.size();
			out.is_wkb = false;
			out.uses_parsed_wkt = true;
			return true;
		}

		// Last resort: assume it's WKB anyway
		out.data = raw_data;
		out.size = raw_size;
		out.is_wkb = true;
		return true;
	}

	// GEOMETRY or WKB_BLOB type from spatial extension
	if (IsGeometryType(type)) {
		string_t val = FlatVector::GetData<string_t>(vec)[row];
		const char *raw_data = val.GetData();
		idx_t raw_size = val.GetSize();

		// WKB_BLOB is standard WKB format
		auto type_name = type.HasAlias() ? type.GetAlias() : type.ToString();
		if (type_name == "WKB_BLOB" || type_name == "wkb_blob") {
			out.data = raw_data;
			out.size = raw_size;
			out.is_wkb = true;
			return true;
		}

		// Check if it's standard WKB (some GEOMETRY types might be)
		if (LooksLikeWkb(raw_data, raw_size)) {
			out.data = raw_data;
			out.size = raw_size;
			out.is_wkb = true;
			return true;
		}

		// Try to parse as DuckDB spatial internal format
		if (LooksLikeGeometryInternal(raw_data, raw_size)) {
			double lat, lon;
			if (DuckDBGeometryParser::ExtractPointCoords(raw_data, raw_size, lat, lon)) {
				// Convert to WKT POINT
				out.parsed_wkt = "POINT(" + std::to_string(lon) + " " + std::to_string(lat) + ")";
				out.data = out.parsed_wkt.c_str();
				out.size = out.parsed_wkt.size();
				out.is_wkb = false;
				out.uses_parsed_wkt = true;
				return true;
			}
		}

		// Fallback: try to find coordinates in the blob
		double lat, lon;
		if (DuckDBGeometryParser::ExtractFirstCoordinate(raw_data, raw_size, lat, lon)) {
			out.parsed_wkt = "POINT(" + std::to_string(lon) + " " + std::to_string(lat) + ")";
			out.data = out.parsed_wkt.c_str();
			out.size = out.parsed_wkt.size();
			out.is_wkb = false;
			out.uses_parsed_wkt = true;
			return true;
		}

		return false;
	}

	// Extension types (like GEOMETRY) - try to read as blob-like data
	if (type.id() == LogicalTypeId::USER || type.id() == LogicalTypeId::STRUCT) {
		if (type.InternalType() == PhysicalType::VARCHAR) {
			string_t val = FlatVector::GetData<string_t>(vec)[row];
			const char *raw_data = val.GetData();
			idx_t raw_size = val.GetSize();

			// Check for standard WKB
			if (LooksLikeWkb(raw_data, raw_size)) {
				out.data = raw_data;
				out.size = raw_size;
				out.is_wkb = true;
				return true;
			}

			// Try internal GEOMETRY format
			if (LooksLikeGeometryInternal(raw_data, raw_size)) {
				double lat, lon;
				if (DuckDBGeometryParser::ExtractPointCoords(raw_data, raw_size, lat, lon)) {
					out.parsed_wkt = "POINT(" + std::to_string(lon) + " " + std::to_string(lat) + ")";
					out.data = out.parsed_wkt.c_str();
					out.size = out.parsed_wkt.size();
					out.is_wkb = false;
					out.uses_parsed_wkt = true;
					return true;
				}
			}

			// Assume WKT string
			out.data = raw_data;
			out.size = raw_size;
			out.is_wkb = false;
			return true;
		}
	}

	// Last resort - try as string-like data
	if (type.InternalType() == PhysicalType::VARCHAR) {
		string_t val = FlatVector::GetData<string_t>(vec)[row];
		out.data = val.GetData();
		out.size = val.GetSize();
		out.is_wkb = LooksLikeWkb(out.data, out.size);
		return true;
	}

	return false;
}

// ============ Helper: WKB Geometry Builder ============

// Build WKB LINESTRING from route points
// WKB format: byte_order(1) + type(4) + num_points(4) + points(16*n)
static std::string BuildWkbLinestring(const ValhallaPoint *points, int num_points) {
	if (num_points <= 0) {
		return "";
	}

	// Calculate size: 1 + 4 + 4 + (16 * num_points)
	size_t wkb_size = 9 + (16 * num_points);
	std::string wkb;
	wkb.resize(wkb_size);

	uint8_t *ptr = reinterpret_cast<uint8_t *>(&wkb[0]);
	size_t offset = 0;

	// Byte order: 0x01 = little-endian
	ptr[offset++] = 0x01;

	// Geometry type: 2 = LINESTRING (little-endian uint32)
	uint32_t geom_type = 2;
	std::memcpy(ptr + offset, &geom_type, 4);
	offset += 4;

	// Number of points (little-endian uint32)
	uint32_t num_pts = static_cast<uint32_t>(num_points);
	std::memcpy(ptr + offset, &num_pts, 4);
	offset += 4;

	// Coordinates (X=lon, Y=lat as doubles)
	for (int i = 0; i < num_points; i++) {
		double lon = points[i].lon;
		double lat = points[i].lat;
		std::memcpy(ptr + offset, &lon, 8);
		offset += 8;
		std::memcpy(ptr + offset, &lat, 8);
		offset += 8;
	}

	return wkb;
}

// ============ Scalar Functions ============

// travel_time_load_config(config_path VARCHAR) -> BOOLEAN
static void TravelTimeLoadConfigFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &path_vec = args.data[0];
	idx_t count = args.size();

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(path_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		string_t path = FlatVector::GetData<string_t>(path_vec)[i];
		std::string path_str = path.GetString();

		std::lock_guard<std::mutex> lock(g_router_mutex);

		// Free existing router if different config
		if (g_router && g_config_path != path_str) {
			valhalla_free(g_router);
			g_router = nullptr;
		}

		// Load if not already loaded
		if (!g_router) {
			g_router = valhalla_init(path_str.c_str());
			if (!g_router) {
				throw InvalidInputException("Failed to load Valhalla config: %s - %s", path_str.c_str(),
				                            valhalla_last_error());
			}
			g_config_path = path_str;
		}

		FlatVector::GetData<bool>(result)[i] = true;
	}
}

// travel_time_is_loaded() -> BOOLEAN
static void TravelTimeIsLoadedFun(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t count = args.size();

	std::lock_guard<std::mutex> lock(g_router_mutex);
	bool is_loaded = g_router && valhalla_is_ready(g_router);

	for (idx_t i = 0; i < count; i++) {
		FlatVector::GetData<bool>(result)[i] = is_loaded;
	}
}

// travel_time(lat1, lon1, lat2, lon2, costing) -> DOUBLE (seconds)
static void TravelTimeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &lat1_vec = args.data[0];
	auto &lon1_vec = args.data[1];
	auto &lat2_vec = args.data[2];
	auto &lon2_vec = args.data[3];
	auto &costing_vec = args.data[4];
	idx_t count = args.size();

	constexpr int MAX_POINTS = 10000;
	std::vector<ValhallaPoint> points(MAX_POINTS);
	ValhallaRouteResult route_result;

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(lat1_vec, i) || FlatVector::IsNull(lon1_vec, i) || FlatVector::IsNull(lat2_vec, i) ||
		    FlatVector::IsNull(lon2_vec, i) || FlatVector::IsNull(costing_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		double lat1 = FlatVector::GetData<double>(lat1_vec)[i];
		double lon1 = FlatVector::GetData<double>(lon1_vec)[i];
		double lat2 = FlatVector::GetData<double>(lat2_vec)[i];
		double lon2 = FlatVector::GetData<double>(lon2_vec)[i];
		string_t costing = FlatVector::GetData<string_t>(costing_vec)[i];

		std::lock_guard<std::mutex> lock(g_router_mutex);
		EnsureRouterLoaded(costing.GetData());

		int num_points = valhalla_route(g_router, lat1, lon1, lat2, lon2, costing.GetData(), &route_result,
		                                points.data(), MAX_POINTS);

		if (num_points < 0) {
			FlatVector::SetNull(result, i, true);
		} else {
			FlatVector::GetData<double>(result)[i] = route_result.duration_s;
		}
	}
}

// travel_time_route(from_wkt, to_wkt, costing) -> STRUCT
static void TravelTimeRouteWktFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &from_vec = args.data[0];
	auto &to_vec = args.data[1];
	auto &costing_vec = args.data[2];
	idx_t count = args.size();

	auto &struct_entries = StructVector::GetEntries(result);
	auto dist_data = FlatVector::GetData<double>(*struct_entries[0]);
	auto time_data = FlatVector::GetData<double>(*struct_entries[1]);

	constexpr int MAX_POINTS = 50000;
	std::vector<ValhallaPoint> points(MAX_POINTS);
	ValhallaRouteResult route_result;

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(from_vec, i) || FlatVector::IsNull(to_vec, i) || FlatVector::IsNull(costing_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		string_t from_wkt = FlatVector::GetData<string_t>(from_vec)[i];
		string_t to_wkt = FlatVector::GetData<string_t>(to_vec)[i];
		string_t costing = FlatVector::GetData<string_t>(costing_vec)[i];

		std::lock_guard<std::mutex> lock(g_router_mutex);
		EnsureRouterLoaded(costing.GetData());

		int num_points = valhalla_route_wkt(g_router, from_wkt.GetData(), to_wkt.GetData(), costing.GetData(),
		                                    &route_result, points.data(), MAX_POINTS);

		if (num_points < 0) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		dist_data[i] = route_result.distance_m / 1000.0; // km
		time_data[i] = route_result.duration_s / 60.0;   // minutes

		// Build WKB LINESTRING geometry
		std::string wkb = BuildWkbLinestring(points.data(), num_points);

		auto &geom_vec = *struct_entries[2];
		FlatVector::GetData<string_t>(geom_vec)[i] = StringVector::AddStringOrBlob(geom_vec, wkb);
	}
}

// travel_time_route(from_wkb BLOB, to_wkb BLOB, costing) -> STRUCT
static void TravelTimeRouteWkbFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &from_vec = args.data[0];
	auto &to_vec = args.data[1];
	auto &costing_vec = args.data[2];
	idx_t count = args.size();

	auto &struct_entries = StructVector::GetEntries(result);
	auto dist_data = FlatVector::GetData<double>(*struct_entries[0]);
	auto time_data = FlatVector::GetData<double>(*struct_entries[1]);

	constexpr int MAX_POINTS = 50000;
	std::vector<ValhallaPoint> points(MAX_POINTS);
	ValhallaRouteResult route_result;

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(from_vec, i) || FlatVector::IsNull(to_vec, i) || FlatVector::IsNull(costing_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		string_t from_wkb = FlatVector::GetData<string_t>(from_vec)[i];
		string_t to_wkb = FlatVector::GetData<string_t>(to_vec)[i];
		string_t costing = FlatVector::GetData<string_t>(costing_vec)[i];

		std::lock_guard<std::mutex> lock(g_router_mutex);
		EnsureRouterLoaded(costing.GetData());

		int num_points = valhalla_route_wkb(
		    g_router, reinterpret_cast<const unsigned char *>(from_wkb.GetData()), static_cast<int>(from_wkb.GetSize()),
		    reinterpret_cast<const unsigned char *>(to_wkb.GetData()), static_cast<int>(to_wkb.GetSize()),
		    costing.GetData(), &route_result, points.data(), MAX_POINTS);

		if (num_points < 0) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		dist_data[i] = route_result.distance_m / 1000.0;
		time_data[i] = route_result.duration_s / 60.0;

		// Build WKB LINESTRING geometry
		std::string wkb = BuildWkbLinestring(points.data(), num_points);

		auto &geom_vec = *struct_entries[2];
		FlatVector::GetData<string_t>(geom_vec)[i] = StringVector::AddStringOrBlob(geom_vec, wkb);
	}
}

// Unified travel_time_route(from_geom ANY, to_geom ANY, costing) -> STRUCT
// Handles VARCHAR (WKT), BLOB (WKB), and GEOMETRY types
static void TravelTimeRouteUnifiedFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &from_vec = args.data[0];
	auto &to_vec = args.data[1];
	auto &costing_vec = args.data[2];
	idx_t count = args.size();

	auto from_type = from_vec.GetType();
	auto to_type = to_vec.GetType();

	auto &struct_entries = StructVector::GetEntries(result);
	auto dist_data = FlatVector::GetData<double>(*struct_entries[0]);
	auto time_data = FlatVector::GetData<double>(*struct_entries[1]);

	constexpr int MAX_POINTS = 50000;
	std::vector<ValhallaPoint> points(MAX_POINTS);
	ValhallaRouteResult route_result;

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(costing_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		GeometryData from_geom, to_geom;
		if (!ExtractGeometryData(from_vec, i, from_type, from_geom) ||
		    !ExtractGeometryData(to_vec, i, to_type, to_geom)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		string_t costing = FlatVector::GetData<string_t>(costing_vec)[i];

		std::lock_guard<std::mutex> lock(g_router_mutex);
		EnsureRouterLoaded(costing.GetData());

		int num_points;
		if (from_geom.is_wkb && to_geom.is_wkb) {
			// Both WKB
			num_points = valhalla_route_wkb(
			    g_router, reinterpret_cast<const unsigned char *>(from_geom.data), static_cast<int>(from_geom.size),
			    reinterpret_cast<const unsigned char *>(to_geom.data), static_cast<int>(to_geom.size),
			    costing.GetData(), &route_result, points.data(), MAX_POINTS);
		} else if (!from_geom.is_wkb && !to_geom.is_wkb) {
			// Both WKT - need null-terminated strings
			std::string from_str(from_geom.data, from_geom.size);
			std::string to_str(to_geom.data, to_geom.size);
			num_points = valhalla_route_wkt(g_router, from_str.c_str(), to_str.c_str(), costing.GetData(),
			                                &route_result, points.data(), MAX_POINTS);
		} else {
			// Mixed - convert WKT to use, prefer WKB path if one is WKB
			// For simplicity, use WKT if either is WKT
			std::string from_str(from_geom.data, from_geom.size);
			std::string to_str(to_geom.data, to_geom.size);
			num_points = valhalla_route_wkt(g_router, from_str.c_str(), to_str.c_str(), costing.GetData(),
			                                &route_result, points.data(), MAX_POINTS);
		}

		if (num_points < 0) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		dist_data[i] = route_result.distance_m / 1000.0;
		time_data[i] = route_result.duration_s / 60.0;

		// Build WKB LINESTRING geometry
		std::string wkb = BuildWkbLinestring(points.data(), num_points);

		auto &geom_vec = *struct_entries[2];
		FlatVector::GetData<string_t>(geom_vec)[i] = StringVector::AddStringOrBlob(geom_vec, wkb);
	}
}

// travel_time_locate(lat, lon, costing) -> STRUCT(lat, lon)
static void TravelTimeLocateFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &lat_vec = args.data[0];
	auto &lon_vec = args.data[1];
	auto &costing_vec = args.data[2];
	idx_t count = args.size();

	auto &struct_entries = StructVector::GetEntries(result);
	auto lat_data = FlatVector::GetData<double>(*struct_entries[0]);
	auto lon_data = FlatVector::GetData<double>(*struct_entries[1]);

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(lat_vec, i) || FlatVector::IsNull(lon_vec, i) || FlatVector::IsNull(costing_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		double lat = FlatVector::GetData<double>(lat_vec)[i];
		double lon = FlatVector::GetData<double>(lon_vec)[i];
		string_t costing = FlatVector::GetData<string_t>(costing_vec)[i];

		double out_lat, out_lon;

		std::lock_guard<std::mutex> lock(g_router_mutex);
		EnsureRouterLoaded(costing.GetData());

		int result_code = valhalla_locate(g_router, lat, lon, costing.GetData(), &out_lat, &out_lon);

		if (result_code < 0) {
			FlatVector::SetNull(result, i, true);
		} else {
			lat_data[i] = out_lat;
			lon_data[i] = out_lon;
		}
	}
}

// travel_time_request(action, json) -> VARCHAR (raw JSON response)
static void TravelTimeRequestFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &action_vec = args.data[0];
	auto &json_vec = args.data[1];
	idx_t count = args.size();

	for (idx_t i = 0; i < count; i++) {
		if (FlatVector::IsNull(action_vec, i) || FlatVector::IsNull(json_vec, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		string_t action = FlatVector::GetData<string_t>(action_vec)[i];
		string_t json = FlatVector::GetData<string_t>(json_vec)[i];

		std::lock_guard<std::mutex> lock(g_router_mutex);
		if (!g_router || !valhalla_is_ready(g_router)) {
			throw InvalidInputException("Valhalla router not loaded");
		}

		char *response = valhalla_request(g_router, action.GetData(), json.GetData());

		if (!response) {
			FlatVector::SetNull(result, i, true);
		} else {
			FlatVector::GetData<string_t>(result)[i] = StringVector::AddString(result, response);
			valhalla_free_string(response);
		}
	}
}

// ============ Matrix Table Function ============

struct MatrixBindData : public TableFunctionData {
	std::vector<double> src_lats;
	std::vector<double> src_lons;
	std::vector<double> dst_lats;
	std::vector<double> dst_lons;
	std::string costing;
};

struct MatrixGlobalState : public GlobalTableFunctionState {
	std::vector<ValhallaMatrixEntry> results;
	idx_t current_idx;
	bool done;

	MatrixGlobalState() : current_idx(0), done(false) {
	}
};

static unique_ptr<FunctionData> MatrixBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<MatrixBindData>();

	auto &src_lats_list = input.inputs[0];
	auto &src_lons_list = input.inputs[1];
	auto &dst_lats_list = input.inputs[2];
	auto &dst_lons_list = input.inputs[3];
	auto &costing_val = input.inputs[4];

	bind_data->costing = costing_val.GetValue<string>();

	// Extract source coordinates
	auto &src_lats_children = ListValue::GetChildren(src_lats_list);
	auto &src_lons_children = ListValue::GetChildren(src_lons_list);
	if (src_lats_children.size() != src_lons_children.size()) {
		throw InvalidInputException("Source lat/lon arrays must have same length");
	}
	for (idx_t i = 0; i < src_lats_children.size(); i++) {
		bind_data->src_lats.push_back(src_lats_children[i].GetValue<double>());
		bind_data->src_lons.push_back(src_lons_children[i].GetValue<double>());
	}

	// Extract destination coordinates
	auto &dst_lats_children = ListValue::GetChildren(dst_lats_list);
	auto &dst_lons_children = ListValue::GetChildren(dst_lons_list);
	if (dst_lats_children.size() != dst_lons_children.size()) {
		throw InvalidInputException("Destination lat/lon arrays must have same length");
	}
	for (idx_t i = 0; i < dst_lats_children.size(); i++) {
		bind_data->dst_lats.push_back(dst_lats_children[i].GetValue<double>());
		bind_data->dst_lons.push_back(dst_lons_children[i].GetValue<double>());
	}

	names.emplace_back("from_idx");
	names.emplace_back("to_idx");
	names.emplace_back("distance_m");
	names.emplace_back("duration_s");
	return_types.emplace_back(LogicalType::INTEGER);
	return_types.emplace_back(LogicalType::INTEGER);
	return_types.emplace_back(LogicalType::DOUBLE);
	return_types.emplace_back(LogicalType::DOUBLE);

	return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> MatrixInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<MatrixGlobalState>();
	auto &bind_data = input.bind_data->Cast<MatrixBindData>();

	idx_t total = bind_data.src_lats.size() * bind_data.dst_lats.size();
	state->results.resize(total);

	std::lock_guard<std::mutex> lock(g_router_mutex);
	if (!g_router || !valhalla_is_ready(g_router)) {
		throw InvalidInputException("Valhalla router not loaded");
	}

	int count = valhalla_matrix(g_router, bind_data.src_lats.data(), bind_data.src_lons.data(),
	                            static_cast<int>(bind_data.src_lats.size()), bind_data.dst_lats.data(),
	                            bind_data.dst_lons.data(), static_cast<int>(bind_data.dst_lats.size()),
	                            bind_data.costing.c_str(), state->results.data());

	if (count < 0) {
		throw InvalidInputException("Matrix calculation failed: %s", valhalla_last_error());
	}
	state->results.resize(count);

	return std::move(state);
}

static void MatrixFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<MatrixGlobalState>();

	if (state.done) {
		output.SetCardinality(0);
		return;
	}

	idx_t count = 0;
	auto from_idx_data = FlatVector::GetData<int32_t>(output.data[0]);
	auto to_idx_data = FlatVector::GetData<int32_t>(output.data[1]);
	auto dist_data = FlatVector::GetData<double>(output.data[2]);
	auto time_data = FlatVector::GetData<double>(output.data[3]);

	while (count < STANDARD_VECTOR_SIZE && state.current_idx < state.results.size()) {
		auto &entry = state.results[state.current_idx];
		from_idx_data[count] = entry.from_index;
		to_idx_data[count] = entry.to_index;
		dist_data[count] = entry.distance_m;
		time_data[count] = entry.duration_s;
		count++;
		state.current_idx++;
	}

	output.SetCardinality(count);
	if (state.current_idx >= state.results.size()) {
		state.done = true;
	}
}

// ============ Extension Registration ============

static void LoadInternal(ExtensionLoader &loader) {
	// travel_time_load_config
	auto load_config_function = ScalarFunction("travel_time_load_config", {LogicalType::VARCHAR}, LogicalType::BOOLEAN,
	                                           TravelTimeLoadConfigFun);
	loader.RegisterFunction(load_config_function);

	// travel_time_is_loaded
	auto is_loaded_function = ScalarFunction("travel_time_is_loaded", {}, LogicalType::BOOLEAN, TravelTimeIsLoadedFun);
	loader.RegisterFunction(is_loaded_function);

	// travel_time
	auto travel_time_function = ScalarFunction(
	    "travel_time",
	    {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::VARCHAR},
	    LogicalType::DOUBLE, TravelTimeFun);
	loader.RegisterFunction(travel_time_function);

	// travel_time_route_wkb return type
	child_list_t<LogicalType> route_struct_children;
	route_struct_children.push_back(make_pair("distance_km", LogicalType::DOUBLE));
	route_struct_children.push_back(make_pair("duration_minutes", LogicalType::DOUBLE));
	route_struct_children.push_back(make_pair("geometry", LogicalType::BLOB));
	auto route_return_type = LogicalType::STRUCT(route_struct_children);

	// travel_time_route_wkb - unified function accepting ANY geometry type
	// Returns WKB BLOB geometry (use travel_time_route for automatic GEOMETRY conversion)
	// Works with: VARCHAR (WKT), BLOB (WKB), GEOMETRY (spatial extension)
	auto route_function =
	    ScalarFunction("travel_time_route_wkb", {LogicalType::ANY, LogicalType::ANY, LogicalType::VARCHAR},
	                   route_return_type, TravelTimeRouteUnifiedFun);
	loader.RegisterFunction(route_function);

	// travel_time_locate
	child_list_t<LogicalType> locate_struct_children;
	locate_struct_children.push_back(make_pair("lat", LogicalType::DOUBLE));
	locate_struct_children.push_back(make_pair("lon", LogicalType::DOUBLE));
	auto locate_return_type = LogicalType::STRUCT(locate_struct_children);

	auto locate_function =
	    ScalarFunction("travel_time_locate", {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::VARCHAR},
	                   locate_return_type, TravelTimeLocateFun);
	loader.RegisterFunction(locate_function);

	// travel_time_request (raw JSON API)
	auto request_function = ScalarFunction("travel_time_request", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       LogicalType::VARCHAR, TravelTimeRequestFun);
	loader.RegisterFunction(request_function);

	// travel_time_matrix table function
	TableFunction matrix_func("travel_time_matrix",
	                          {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                           LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                           LogicalType::VARCHAR},
	                          MatrixFunction, MatrixBind, MatrixInitGlobal);
	loader.RegisterFunction(matrix_func);
}

void TravelTimeExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string TravelTimeExtension::Name() {
	return "travel_time";
}

std::string TravelTimeExtension::Version() const {
#ifdef EXT_VERSION_TRAVEL_TIME
	return EXT_VERSION_TRAVEL_TIME;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(travel_time, loader) {
	duckdb::LoadInternal(loader);
}
}
