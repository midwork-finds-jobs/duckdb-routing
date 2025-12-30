#ifndef ROUTING_H
#define ROUTING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load routing data from an OSM PBF file for a specific mode.
 *
 * @param pbf_path Path to the OSM PBF file
 * @param mode Transport mode: "auto", "bicycle", or "pedestrian"
 * @return 0 on success, -1 on error
 */
int routing_load(const char *pbf_path, const char *mode);

/**
 * Calculate travel time between two points.
 *
 * @param lat1 Start latitude
 * @param lon1 Start longitude
 * @param lat2 End latitude
 * @param lon2 End longitude
 * @param mode Transport mode: "auto", "bicycle", or "pedestrian"
 * @return Travel time in seconds, -1.0 if no route found, -2.0 if not loaded
 */
double routing_travel_time(double lat1, double lon1, double lat2, double lon2, const char *mode);

/**
 * Batch calculate travel times between pairs of points.
 *
 * @param lats1 Array of start latitudes
 * @param lons1 Array of start longitudes
 * @param lats2 Array of end latitudes
 * @param lons2 Array of end longitudes
 * @param results Output array for travel times in seconds (must be pre-allocated)
 * @param count Number of pairs to calculate
 * @param mode Transport mode
 * @return Number of successful calculations, -1 on error, -2 if not loaded
 */
int routing_batch(const double *lats1, const double *lons1, const double *lats2, const double *lons2, double *results,
                  int count, const char *mode);

/**
 * Snap a coordinate to the nearest road network node.
 *
 * @param lat Input latitude
 * @param lon Input longitude
 * @param mode Transport mode
 * @param out_lat Output: snapped latitude
 * @param out_lon Output: snapped longitude
 * @param out_distance_m Output: distance to snapped point in meters
 * @return 0 on success, -1 on error, -2 if not loaded
 */
int routing_snap(double lat, double lon, const char *mode, double *out_lat, double *out_lon, double *out_distance_m);

/**
 * Get count of nodes in the routing graph.
 *
 * @param mode Transport mode
 * @return Number of nodes, -1 on error, -2 if not loaded
 */
int routing_node_count(const char *mode);

/**
 * Check if routing data is loaded for a mode.
 *
 * @param mode Transport mode
 * @return 1 if loaded, 0 if not
 */
int routing_is_loaded(const char *mode);

/**
 * Free routing data for a mode.
 *
 * @param mode Transport mode
 */
void routing_free(const char *mode);

/**
 * Isochrone result struct.
 */
typedef struct {
	double lat;
	double lon;
	double seconds;
} IsochroneResult;

/**
 * Route point struct.
 */
typedef struct {
	double lat;
	double lon;
} RoutePoint;

/**
 * Route result struct.
 */
typedef struct {
	double distance_m; /* Total road distance in meters */
	double duration_s; /* Travel time in seconds */
	int num_points;    /* Number of points in geometry */
} RouteResult;

/**
 * Calculate isochrone - all reachable points within max_seconds.
 *
 * @param lat Origin latitude
 * @param lon Origin longitude
 * @param max_seconds Maximum travel time in seconds
 * @param mode Transport mode
 * @param out_results Output array for results (must be pre-allocated)
 * @param max_results Maximum number of results to return
 * @return Number of results written, -1 on error, -2 if not loaded
 */
int routing_isochrone(double lat, double lon, double max_seconds, const char *mode, IsochroneResult *out_results,
                      int max_results);

/**
 * Calculate route with full geometry.
 *
 * @param lat1 Start latitude
 * @param lon1 Start longitude
 * @param lat2 End latitude
 * @param lon2 End longitude
 * @param mode Transport mode
 * @param out_result Output: route summary (distance, duration, point count)
 * @param out_points Output: array for path coordinates (must be pre-allocated)
 * @param max_points Maximum number of points buffer can hold
 * @return Number of points written, -1 on error, -2 if not loaded
 */
int routing_route(double lat1, double lon1, double lat2, double lon2, const char *mode, RouteResult *out_result,
                  RoutePoint *out_points, int max_points);

/**
 * Calculate route using WKT geometries as input.
 * Uses centroid of each geometry as the routing point.
 *
 * @param from_wkt WKT geometry string for start (e.g., "POINT(12.45 43.94)" or "POLYGON(...)")
 * @param to_wkt WKT geometry string for end
 * @param mode Transport mode
 * @param out_result Output: route summary (distance, duration, point count)
 * @param out_points Output: array for path coordinates (must be pre-allocated)
 * @param max_points Maximum number of points buffer can hold
 * @return Number of points written, -1 on error, -2 if not loaded
 */
int routing_route_geom(const char *from_wkt, const char *to_wkt, const char *mode, RouteResult *out_result,
                       RoutePoint *out_points, int max_points);

/**
 * Calculate route using WKB (Well-Known Binary) geometries as input.
 * Uses centroid of each geometry as the routing point.
 * Use with ST_AsWKB(geometry) from DuckDB spatial extension.
 *
 * @param from_wkb WKB bytes for start geometry
 * @param from_wkb_len Length of from_wkb in bytes
 * @param to_wkb WKB bytes for end geometry
 * @param to_wkb_len Length of to_wkb in bytes
 * @param mode Transport mode
 * @param out_result Output: route summary (distance, duration, point count)
 * @param out_points Output: array for path coordinates (must be pre-allocated)
 * @param max_points Maximum number of points buffer can hold
 * @return Number of points written, -1 on error, -2 if not loaded
 */
int routing_route_wkb(const unsigned char *from_wkb, int from_wkb_len, const unsigned char *to_wkb, int to_wkb_len,
                      const char *mode, RouteResult *out_result, RoutePoint *out_points, int max_points);

#ifdef __cplusplus
}
#endif

#endif /* ROUTING_H */
