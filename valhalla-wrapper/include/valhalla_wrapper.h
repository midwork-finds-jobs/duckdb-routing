/**
 * Valhalla Wrapper - Pure C API for DuckDB integration
 *
 * This wrapper provides a C ABI interface to Valhalla's routing engine,
 * allowing C++11 code (like DuckDB) to link with C++20 Valhalla.
 */

#ifndef VALHALLA_WRAPPER_H
#define VALHALLA_WRAPPER_H

#include <stdint.h>
#include <stddef.h>

/* Export macro for symbol visibility */
#ifdef _WIN32
#ifdef VALHALLA_WRAPPER_EXPORTS
#define VALHALLA_EXPORT __declspec(dllexport)
#else
#define VALHALLA_EXPORT __declspec(dllimport)
#endif
#else
#define VALHALLA_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to Valhalla router instance */
typedef struct ValhallaRouter ValhallaRouter;

/* Route result structure */
typedef struct {
	double distance_m; /* Total distance in meters */
	double duration_s; /* Total duration in seconds */
	int num_points;    /* Number of points in geometry */
} ValhallaRouteResult;

/* Route point for geometry */
typedef struct {
	double lat;
	double lon;
} ValhallaPoint;

/* Matrix result entry */
typedef struct {
	int from_index;
	int to_index;
	double distance_m;
	double duration_s;
} ValhallaMatrixEntry;

/* Isochrone contour */
typedef struct {
	double minutes;
	char *geometry_wkt; /* WKT polygon, caller must free with valhalla_free_string */
} ValhallaIsochroneContour;

/**
 * Initialize Valhalla router from config file path.
 *
 * @param config_path Path to valhalla.json config file
 * @return Router handle, or NULL on error (check valhalla_last_error)
 */
VALHALLA_EXPORT ValhallaRouter *valhalla_init(const char *config_path);

/**
 * Initialize Valhalla router from config JSON string.
 *
 * @param config_json JSON config string
 * @return Router handle, or NULL on error
 */
VALHALLA_EXPORT ValhallaRouter *valhalla_init_from_json(const char *config_json);

/**
 * Check if router is ready.
 *
 * @param router Router handle
 * @return 1 if ready, 0 if not
 */
VALHALLA_EXPORT int valhalla_is_ready(ValhallaRouter *router);

/**
 * Calculate route between two points.
 *
 * @param router Router handle
 * @param lat1 Start latitude
 * @param lon1 Start longitude
 * @param lat2 End latitude
 * @param lon2 End longitude
 * @param costing Costing model: "auto", "bicycle", "pedestrian", "truck", etc.
 * @param out_result Output: route summary
 * @param out_points Output: array for path coordinates (must be pre-allocated)
 * @param max_points Maximum number of points buffer can hold
 * @return Number of points written, -1 on error
 */
VALHALLA_EXPORT int valhalla_route(ValhallaRouter *router, double lat1, double lon1, double lat2, double lon2,
                                   const char *costing, ValhallaRouteResult *out_result, ValhallaPoint *out_points,
                                   int max_points);

/**
 * Calculate route using WKT geometry strings (uses centroid).
 *
 * @param router Router handle
 * @param from_wkt WKT geometry for start
 * @param to_wkt WKT geometry for end
 * @param costing Costing model
 * @param out_result Output: route summary
 * @param out_points Output: array for path coordinates
 * @param max_points Maximum points buffer size
 * @return Number of points written, -1 on error
 */
VALHALLA_EXPORT int valhalla_route_wkt(ValhallaRouter *router, const char *from_wkt, const char *to_wkt,
                                       const char *costing, ValhallaRouteResult *out_result, ValhallaPoint *out_points,
                                       int max_points);

/**
 * Calculate route using WKB geometry (uses centroid).
 *
 * @param router Router handle
 * @param from_wkb WKB bytes for start geometry
 * @param from_wkb_len Length of from_wkb
 * @param to_wkb WKB bytes for end geometry
 * @param to_wkb_len Length of to_wkb
 * @param costing Costing model
 * @param out_result Output: route summary
 * @param out_points Output: array for path coordinates
 * @param max_points Maximum points buffer size
 * @return Number of points written, -1 on error
 */
VALHALLA_EXPORT int valhalla_route_wkb(ValhallaRouter *router, const unsigned char *from_wkb, int from_wkb_len,
                                       const unsigned char *to_wkb, int to_wkb_len, const char *costing,
                                       ValhallaRouteResult *out_result, ValhallaPoint *out_points, int max_points);

/**
 * Calculate distance/duration matrix.
 *
 * @param router Router handle
 * @param src_lats Source latitudes
 * @param src_lons Source longitudes
 * @param src_count Number of sources
 * @param dst_lats Destination latitudes
 * @param dst_lons Destination longitudes
 * @param dst_count Number of destinations
 * @param costing Costing model
 * @param out_entries Output: matrix entries (must be pre-allocated, size = src_count * dst_count)
 * @return Number of entries written, -1 on error
 */
VALHALLA_EXPORT int valhalla_matrix(ValhallaRouter *router, const double *src_lats, const double *src_lons,
                                    int src_count, const double *dst_lats, const double *dst_lons, int dst_count,
                                    const char *costing, ValhallaMatrixEntry *out_entries);

/**
 * Calculate isochrone contours.
 *
 * @param router Router handle
 * @param lat Origin latitude
 * @param lon Origin longitude
 * @param contour_minutes Array of contour times in minutes (e.g., [5, 10, 15])
 * @param contour_count Number of contours
 * @param costing Costing model
 * @param out_contours Output: contour geometries (must be pre-allocated)
 * @return Number of contours written, -1 on error
 */
VALHALLA_EXPORT int valhalla_isochrone(ValhallaRouter *router, double lat, double lon, const double *contour_minutes,
                                       int contour_count, const char *costing, ValhallaIsochroneContour *out_contours);

/**
 * Snap coordinate to nearest road.
 *
 * @param router Router handle
 * @param lat Input latitude
 * @param lon Input longitude
 * @param costing Costing model
 * @param out_lat Output: snapped latitude
 * @param out_lon Output: snapped longitude
 * @return 0 on success, -1 on error
 */
VALHALLA_EXPORT int valhalla_locate(ValhallaRouter *router, double lat, double lon, const char *costing,
                                    double *out_lat, double *out_lon);

/**
 * Raw JSON API access - send any Valhalla request.
 *
 * @param router Router handle
 * @param action Action name: "route", "matrix", "isochrone", "locate", etc.
 * @param request_json JSON request string
 * @return JSON response string (caller must free with valhalla_free_string), NULL on error
 */
VALHALLA_EXPORT char *valhalla_request(ValhallaRouter *router, const char *action, const char *request_json);

/**
 * Free a string returned by valhalla_request or isochrone geometry.
 */
VALHALLA_EXPORT void valhalla_free_string(char *str);

/**
 * Get last error message.
 *
 * @return Error message string (do not free)
 */
VALHALLA_EXPORT const char *valhalla_last_error(void);

/**
 * Free router instance.
 *
 * @param router Router handle
 */
VALHALLA_EXPORT void valhalla_free(ValhallaRouter *router);

/**
 * Get Valhalla version string.
 *
 * @return Version string (do not free)
 */
VALHALLA_EXPORT const char *valhalla_version(void);

#ifdef __cplusplus
}
#endif

#endif /* VALHALLA_WRAPPER_H */
