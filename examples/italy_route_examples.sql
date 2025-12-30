-- Italy Routing Examples
--
-- Demonstrates routing between major Italian cities
--
-- Prerequisites:
--   1. Build Italy tiles: ./examples/build_italy_tiles.sh
--   2. Or follow manual steps in ITALY.md
--
-- Run: duckdb < examples/italy_route_examples.sql

-- Load extensions
INSTALL spatial; LOAD spatial;

-- Load routing tiles
SELECT travel_time_load_config('./italy_tiles/valhalla.json') as loaded;

-- Load geometry macro for cleaner queries
.read examples/geometry_macro.sql

-- =============================================================================
-- Example 1: Single route with full details
-- =============================================================================
SELECT '=== Example 1: Rome to Florence ===' as example;

WITH route AS (
    SELECT travel_time_route(
        ST_Point(12.4964, 41.9028),  -- Rome (Colosseum)
        ST_Point(11.2558, 43.7696),  -- Florence (Duomo)
        'auto'
    ) as r
)
SELECT
    'Rome → Florence' as route,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours,
    ST_NPoints(r.geometry) as waypoints,
    round(r.distance_km / (r.duration_minutes / 60.0), 0) as avg_speed_kmh
FROM route;

SELECT '' as separator;

-- =============================================================================
-- Example 2: Routes from Rome to major cities
-- =============================================================================
SELECT '=== Example 2: Routes from Rome ===' as example;

SELECT
    'Rome → ' || name as route,
    round(km, 0) as km,
    round(hours, 1) as hours
FROM (
    SELECT 'Naples' as name,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(14.2681, 40.8518), 'auto').distance_km as km,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(14.2681, 40.8518), 'auto').duration_minutes / 60.0 as hours
    UNION ALL
    SELECT 'Florence',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'auto').distance_km,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'auto').duration_minutes / 60.0
    UNION ALL
    SELECT 'Bologna',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.3426, 44.4949), 'auto').distance_km,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.3426, 44.4949), 'auto').duration_minutes / 60.0
    UNION ALL
    SELECT 'Venice',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(12.3155, 45.4408), 'auto').distance_km,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(12.3155, 45.4408), 'auto').duration_minutes / 60.0
    UNION ALL
    SELECT 'Milan',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(9.1900, 45.4642), 'auto').distance_km,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(9.1900, 45.4642), 'auto').duration_minutes / 60.0
)
ORDER BY km;

SELECT '' as separator;

-- =============================================================================
-- Example 3: Travel mode comparison
-- =============================================================================
SELECT '=== Example 3: Travel Modes (Rome to Florence) ===' as example;

SELECT
    mode,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours
FROM (
    SELECT 'Car' as mode,
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'auto') as r
    UNION ALL
    SELECT 'Truck',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'truck')
    UNION ALL
    SELECT 'Bicycle',
           travel_time_route(ST_Point(12.4964, 41.9028), ST_Point(11.2558, 43.7696), 'bicycle')
)
ORDER BY hours;

SELECT '' as separator;

-- =============================================================================
-- Example 4: Northern Italy triangle
-- =============================================================================
SELECT '=== Example 4: Northern Italy Triangle ===' as example;

SELECT
    route,
    round(r.distance_km, 0) as km,
    round(r.duration_minutes / 60.0, 1) as hours
FROM (
    SELECT 'Milan → Venice' as route,
           travel_time_route(ST_Point(9.1900, 45.4642), ST_Point(12.3155, 45.4408), 'auto') as r
    UNION ALL
    SELECT 'Venice → Turin',
           travel_time_route(ST_Point(12.3155, 45.4408), ST_Point(7.6869, 45.0703), 'auto')
    UNION ALL
    SELECT 'Turin → Milan',
           travel_time_route(ST_Point(7.6869, 45.0703), ST_Point(9.1900, 45.4642), 'auto')
);

SELECT '' as separator;

-- =============================================================================
-- Example 5: Tuscany routes (Florence hub)
-- =============================================================================
SELECT '=== Example 5: Tuscany Routes from Florence ===' as example;

SELECT
    'Florence → ' || name as route,
    round(km, 0) as km,
    round(minutes, 0) as minutes
FROM (
    SELECT 'Pisa' as name,
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(10.4016, 43.7228), 'auto').distance_km as km,
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(10.4016, 43.7228), 'auto').duration_minutes as minutes
    UNION ALL
    SELECT 'Siena',
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(11.3308, 43.3188), 'auto').distance_km,
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(11.3308, 43.3188), 'auto').duration_minutes
    UNION ALL
    SELECT 'Bologna',
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(11.3426, 44.4949), 'auto').distance_km,
           travel_time_route(ST_Point(11.2558, 43.7696), ST_Point(11.3426, 44.4949), 'auto').duration_minutes
)
ORDER BY km;

SELECT '' as separator;
SELECT 'Done! All examples completed successfully.' as status;
