/**
 * Valhalla Wrapper Implementation
 *
 * Uses Valhalla's Actor API to provide routing functionality
 * through a pure C interface.
 */

#include "valhalla_wrapper.h"

#include <valhalla/tyr/actor.h>
#include <valhalla/baldr/rapidjson_utils.h>
#include <valhalla/midgard/encoded.h>
#include <valhalla/midgard/pointll.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <memory>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>
#include <mutex>
#include <vector>

// Thread-local error message
static thread_local std::string g_last_error;

// Router structure
struct ValhallaRouter {
	std::unique_ptr<valhalla::tyr::actor_t> actor;
	boost::property_tree::ptree config;
};

// Helper: set error and return error code
static int set_error(const std::string &msg) {
	g_last_error = msg;
	return -1;
}

// Helper: parse WKT to get centroid (simple implementation for POINT)
static bool wkt_to_centroid(const char *wkt, double &lat, double &lon) {
	std::string s(wkt);

	// Simple POINT parser
	if (s.find("POINT") != std::string::npos) {
		size_t start = s.find('(');
		size_t end = s.find(')');
		if (start != std::string::npos && end != std::string::npos) {
			std::string coords = s.substr(start + 1, end - start - 1);
			std::istringstream iss(coords);
			double x, y;
			if (iss >> x >> y) {
				lon = x; // WKT is lon lat
				lat = y;
				return true;
			}
		}
	}

	// TODO: Add support for POLYGON, LINESTRING centroids
	// For now, only POINT is supported

	g_last_error = "Unsupported WKT geometry type (only POINT supported)";
	return false;
}

// Helper: parse WKB to get centroid
static bool wkb_to_centroid(const unsigned char *wkb, int len, double &lat, double &lon) {
	if (len < 21) { // Minimum WKB POINT size
		g_last_error = "WKB too short";
		return false;
	}

	// Check byte order (1 = little endian, 0 = big endian)
	bool little_endian = (wkb[0] == 1);

	// Read geometry type (4 bytes at offset 1)
	uint32_t geom_type;
	if (little_endian) {
		geom_type = wkb[1] | (wkb[2] << 8) | (wkb[3] << 16) | (wkb[4] << 24);
	} else {
		geom_type = (wkb[1] << 24) | (wkb[2] << 16) | (wkb[3] << 8) | wkb[4];
	}

	// Type 1 = Point
	if ((geom_type & 0xFF) == 1) {
		// Read X (lon) and Y (lat) - 8 bytes each at offset 5
		double x, y;
		if (little_endian) {
			memcpy(&x, wkb + 5, 8);
			memcpy(&y, wkb + 13, 8);
		} else {
			// Swap bytes for big endian
			unsigned char buf[8];
			for (int i = 0; i < 8; i++)
				buf[i] = wkb[12 - i];
			memcpy(&x, buf, 8);
			for (int i = 0; i < 8; i++)
				buf[i] = wkb[20 - i];
			memcpy(&y, buf, 8);
		}
		lon = x;
		lat = y;
		return true;
	}

	// TODO: Add support for POLYGON, LINESTRING centroids
	g_last_error = "Unsupported WKB geometry type (only POINT supported)";
	return false;
}

// Helper: decode polyline to points
static std::vector<valhalla::midgard::PointLL> decode_polyline(const std::string &encoded) {
	return valhalla::midgard::decode<std::vector<valhalla::midgard::PointLL>>(encoded);
}

extern "C" {

ValhallaRouter *valhalla_init(const char *config_path) {
	try {
		auto router = new ValhallaRouter();

		std::ifstream file(config_path);
		if (!file.is_open()) {
			g_last_error = "Cannot open config file: " + std::string(config_path);
			delete router;
			return nullptr;
		}

		boost::property_tree::read_json(file, router->config);
		router->actor = std::make_unique<valhalla::tyr::actor_t>(router->config);

		return router;
	} catch (const std::exception &e) {
		g_last_error = e.what();
		return nullptr;
	}
}

ValhallaRouter *valhalla_init_from_json(const char *config_json) {
	try {
		auto router = new ValhallaRouter();

		std::istringstream iss(config_json);
		boost::property_tree::read_json(iss, router->config);
		router->actor = std::make_unique<valhalla::tyr::actor_t>(router->config);

		return router;
	} catch (const std::exception &e) {
		g_last_error = e.what();
		return nullptr;
	}
}

int valhalla_is_ready(ValhallaRouter *router) {
	return (router && router->actor) ? 1 : 0;
}

int valhalla_route(ValhallaRouter *router, double lat1, double lon1, double lat2, double lon2, const char *costing,
                   ValhallaRouteResult *out_result, ValhallaPoint *out_points, int max_points) {
	if (!router || !router->actor) {
		return set_error("Router not initialized");
	}

	try {
		// Build JSON request
		std::ostringstream request;
		request << R"({"locations":[)"
		        << R"({"lat":)" << lat1 << R"(,"lon":)" << lon1 << R"(},)"
		        << R"({"lat":)" << lat2 << R"(,"lon":)" << lon2 << R"(}],)"
		        << R"("costing":")" << costing << R"(",)"
		        << R"("directions_options":{"units":"kilometers"}})";

		std::string response = router->actor->route(request.str());

		// Parse response
		rapidjson::Document doc;
		doc.Parse(response.c_str());

		if (doc.HasParseError() || !doc.IsObject()) {
			return set_error("Failed to parse route response");
		}

		if (doc.HasMember("error")) {
			return set_error(doc["error"].GetString());
		}

		// Extract trip summary
		if (!doc.HasMember("trip")) {
			return set_error("No trip in response");
		}

		const auto &trip = doc["trip"];
		const auto &summary = trip["summary"];

		out_result->distance_m = summary["length"].GetDouble() * 1000.0; // km to m
		out_result->duration_s = summary["time"].GetDouble();

		// Extract shape (encoded polyline)
		int total_points = 0;
		if (trip.HasMember("legs") && trip["legs"].IsArray()) {
			for (const auto &leg : trip["legs"].GetArray()) {
				if (leg.HasMember("shape")) {
					std::string shape = leg["shape"].GetString();
					auto points = decode_polyline(shape);

					for (const auto &pt : points) {
						if (total_points >= max_points)
							break;
						out_points[total_points].lat = pt.lat();
						out_points[total_points].lon = pt.lng();
						total_points++;
					}
				}
			}
		}

		out_result->num_points = total_points;
		return total_points;

	} catch (const std::exception &e) {
		return set_error(e.what());
	}
}

int valhalla_route_wkt(ValhallaRouter *router, const char *from_wkt, const char *to_wkt, const char *costing,
                       ValhallaRouteResult *out_result, ValhallaPoint *out_points, int max_points) {
	double lat1, lon1, lat2, lon2;

	if (!wkt_to_centroid(from_wkt, lat1, lon1)) {
		return -1;
	}
	if (!wkt_to_centroid(to_wkt, lat2, lon2)) {
		return -1;
	}

	return valhalla_route(router, lat1, lon1, lat2, lon2, costing, out_result, out_points, max_points);
}

int valhalla_route_wkb(ValhallaRouter *router, const unsigned char *from_wkb, int from_wkb_len,
                       const unsigned char *to_wkb, int to_wkb_len, const char *costing,
                       ValhallaRouteResult *out_result, ValhallaPoint *out_points, int max_points) {
	double lat1, lon1, lat2, lon2;

	if (!wkb_to_centroid(from_wkb, from_wkb_len, lat1, lon1)) {
		return -1;
	}
	if (!wkb_to_centroid(to_wkb, to_wkb_len, lat2, lon2)) {
		return -1;
	}

	return valhalla_route(router, lat1, lon1, lat2, lon2, costing, out_result, out_points, max_points);
}

int valhalla_matrix(ValhallaRouter *router, const double *src_lats, const double *src_lons, int src_count,
                    const double *dst_lats, const double *dst_lons, int dst_count, const char *costing,
                    ValhallaMatrixEntry *out_entries) {
	if (!router || !router->actor) {
		return set_error("Router not initialized");
	}

	try {
		// Build JSON request
		std::ostringstream request;
		request << R"({"sources":[)";
		for (int i = 0; i < src_count; i++) {
			if (i > 0)
				request << ",";
			request << R"({"lat":)" << src_lats[i] << R"(,"lon":)" << src_lons[i] << "}";
		}
		request << R"(],"targets":[)";
		for (int i = 0; i < dst_count; i++) {
			if (i > 0)
				request << ",";
			request << R"({"lat":)" << dst_lats[i] << R"(,"lon":)" << dst_lons[i] << "}";
		}
		request << R"(],"costing":")" << costing << R"("})";

		std::string response = router->actor->matrix(request.str());

		// Parse response
		rapidjson::Document doc;
		doc.Parse(response.c_str());

		if (doc.HasParseError() || !doc.IsObject()) {
			return set_error("Failed to parse matrix response");
		}

		if (doc.HasMember("error")) {
			return set_error(doc["error"].GetString());
		}

		// Extract matrix
		int idx = 0;
		if (doc.HasMember("sources_to_targets")) {
			const auto &matrix = doc["sources_to_targets"];
			for (int i = 0; i < src_count && i < (int)matrix.Size(); i++) {
				const auto &row = matrix[i];
				for (int j = 0; j < dst_count && j < (int)row.Size(); j++) {
					const auto &cell = row[j];
					out_entries[idx].from_index = i;
					out_entries[idx].to_index = j;
					out_entries[idx].distance_m =
					    cell.HasMember("distance") ? cell["distance"].GetDouble() * 1000.0 : -1;
					out_entries[idx].duration_s = cell.HasMember("time") ? cell["time"].GetDouble() : -1;
					idx++;
				}
			}
		}

		return idx;

	} catch (const std::exception &e) {
		return set_error(e.what());
	}
}

int valhalla_isochrone(ValhallaRouter *router, double lat, double lon, const double *contour_minutes, int contour_count,
                       const char *costing, ValhallaIsochroneContour *out_contours) {
	if (!router || !router->actor) {
		return set_error("Router not initialized");
	}

	try {
		// Build JSON request
		std::ostringstream request;
		request << R"({"locations":[{"lat":)" << lat << R"(,"lon":)" << lon << R"(}],)";
		request << R"("costing":")" << costing << R"(",)";
		request << R"("contours":[)";
		for (int i = 0; i < contour_count; i++) {
			if (i > 0)
				request << ",";
			request << R"({"time":)" << contour_minutes[i] << "}";
		}
		request << R"(],"polygons":true})";

		std::string response = router->actor->isochrone(request.str());

		// Parse response (GeoJSON)
		rapidjson::Document doc;
		doc.Parse(response.c_str());

		if (doc.HasParseError() || !doc.IsObject()) {
			return set_error("Failed to parse isochrone response");
		}

		if (doc.HasMember("error")) {
			return set_error(doc["error"].GetString());
		}

		// Extract features
		int idx = 0;
		if (doc.HasMember("features") && doc["features"].IsArray()) {
			for (const auto &feature : doc["features"].GetArray()) {
				if (idx >= contour_count)
					break;

				out_contours[idx].minutes = contour_minutes[idx];

				// Convert GeoJSON geometry to WKT
				// For now, just store the raw GeoJSON
				rapidjson::StringBuffer buffer;
				rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
				feature["geometry"].Accept(writer);

				std::string geojson = buffer.GetString();
				out_contours[idx].geometry_wkt = strdup(geojson.c_str());

				idx++;
			}
		}

		return idx;

	} catch (const std::exception &e) {
		return set_error(e.what());
	}
}

int valhalla_locate(ValhallaRouter *router, double lat, double lon, const char *costing, double *out_lat,
                    double *out_lon) {
	if (!router || !router->actor) {
		return set_error("Router not initialized");
	}

	try {
		std::ostringstream request;
		request << R"({"locations":[{"lat":)" << lat << R"(,"lon":)" << lon << R"(}],)";
		request << R"("costing":")" << costing << R"("})";

		std::string response = router->actor->locate(request.str());

		rapidjson::Document doc;
		doc.Parse(response.c_str());

		if (doc.HasParseError() || !doc.IsArray() || doc.Empty()) {
			return set_error("Failed to parse locate response");
		}

		const auto &loc = doc[0];
		if (loc.HasMember("edges") && loc["edges"].IsArray() && !loc["edges"].Empty()) {
			const auto &edge = loc["edges"][0];
			if (edge.HasMember("correlated_lat") && edge.HasMember("correlated_lon")) {
				*out_lat = edge["correlated_lat"].GetDouble();
				*out_lon = edge["correlated_lon"].GetDouble();
				return 0;
			}
		}

		return set_error("No edges found for location");

	} catch (const std::exception &e) {
		return set_error(e.what());
	}
}

char *valhalla_request(ValhallaRouter *router, const char *action, const char *request_json) {
	if (!router || !router->actor) {
		g_last_error = "Router not initialized";
		return nullptr;
	}

	try {
		std::string response;
		std::string act(action);

		if (act == "route") {
			response = router->actor->route(request_json);
		} else if (act == "matrix" || act == "sources_to_targets") {
			response = router->actor->matrix(request_json);
		} else if (act == "isochrone") {
			response = router->actor->isochrone(request_json);
		} else if (act == "locate") {
			response = router->actor->locate(request_json);
		} else if (act == "trace_route") {
			response = router->actor->trace_route(request_json);
		} else if (act == "trace_attributes") {
			response = router->actor->trace_attributes(request_json);
		} else if (act == "optimized_route") {
			response = router->actor->optimized_route(request_json);
		} else if (act == "height") {
			response = router->actor->height(request_json);
		} else if (act == "status") {
			response = router->actor->status(request_json);
		} else {
			g_last_error = "Unknown action: " + act;
			return nullptr;
		}

		return strdup(response.c_str());

	} catch (const std::exception &e) {
		g_last_error = e.what();
		return nullptr;
	}
}

void valhalla_free_string(char *str) {
	free(str);
}

const char *valhalla_last_error(void) {
	return g_last_error.c_str();
}

void valhalla_free(ValhallaRouter *router) {
	delete router;
}

const char *valhalla_version(void) {
	// TODO: Get actual Valhalla version
	return "valhalla-wrapper 1.0";
}

} // extern "C"
