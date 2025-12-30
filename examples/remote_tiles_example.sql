-- Remote Tiles Example
-- Demonstrates using Valhalla tiles from HTTP server

-- Prerequisites:
--   1. Start tile server: python valhalla_tile_server.py --tile-dir ./italy_tiles --port 8080
--   2. Run this script: duckdb -f examples/remote_tiles_example.sql

-- Load extensions
INSTALL spatial; LOAD spatial;
LOAD valhalla_routing;

SELECT '=== Remote Tile Server Example ===' as example;
SELECT '' as separator;

-- Load tiles from HTTP server (downloads and caches locally)
SELECT '1. Loading tiles from HTTP server...' as step;
SET valhalla_config = 'http://localhost:8080/valhalla.json';

-- Load geometry macro
SELECT '2. Loading geometry macro...' as step;
CREATE OR REPLACE MACRO travel_time_route(from_geom, to_geom, costing) AS (
    SELECT struct_pack(
        distance_km := r.distance_km,
        duration_minutes := r.duration_minutes,
        geometry := ST_GeomFromWKB(r.geometry)
    ) FROM (SELECT travel_time_route_wkb(from_geom, to_geom, costing) as r)
);

SELECT '' as separator;

-- Example 1: Single route
SELECT '=== Example 1: Rome to Florence ===' as example;
WITH route AS (
    SELECT travel_time_route(
        ST_Point(12.4964, 41.9028),  -- Rome
        ST_Point(11.2558, 43.7696),  -- Florence
        'auto'
    ) as r
)
SELECT
    'Rome → Florence' as route,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours,
    ST_NPoints(r.geometry) as waypoints
FROM route;

SELECT '' as separator;

-- Example 2: Multiple routes
SELECT '=== Example 2: Routes from Rome ===' as example;
SELECT
    'Rome → ' || city as route,
    round(r.distance_km, 0) as km,
    round(r.duration_minutes / 60.0, 1) as hours
FROM (
    SELECT 'Naples' as city,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(14.2681, 40.8518), 'auto') as r
    UNION ALL
    SELECT 'Florence',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'auto')
    UNION ALL
    SELECT 'Milan',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(9.1900, 45.4642), 'auto')
)
ORDER BY km;

SELECT '' as separator;

-- Example 3: Multi-mode comparison
SELECT '=== Example 3: Travel Modes (Rome to Florence) ===' as example;
SELECT
    mode,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours
FROM (
    SELECT 'Car' as mode,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'auto') as r
    UNION ALL
    SELECT 'Bicycle',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'bicycle')
)
ORDER BY hours;

SELECT '' as separator;
SELECT '✅ Remote tiles working successfully!' as status;

-- Note about cache location
SELECT '' as separator;
SELECT '📁 Cache Location' as info;
SELECT '   Tiles cached to: $TMPDIR/valhalla_cache/tiles_*/' as cache_info;
SELECT '   To clear cache: rm -rf $TMPDIR/valhalla_cache/' as clear_command;
