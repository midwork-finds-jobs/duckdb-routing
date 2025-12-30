-- Travel time from San Marino to Fortress of San Leo
--
-- Coordinates:
--   San Marino (city center): 43.9424, 12.4578
--   Fortress of San Leo:      43.8967, 12.3436
--
-- Run: duckdb < examples/san_marino_to_san_leo.sql

-- Load extensions
INSTALL spatial; LOAD spatial;

-- Load routing tiles (adjust path as needed)
SELECT travel_time_load_config('./valhalla_data/valhalla.json') as loaded;

-- =============================================================================
-- Route with full details: distance, duration, geometry
-- =============================================================================
SELECT
    'San Marino → San Leo' as route,
    round(r.distance_km, 2) as distance_km,
    round(r.duration_minutes, 1) as duration_min,
    substr(r.geometry, 1, 80) || '...' as route_geometry
FROM (
    SELECT travel_time_route(
        ST_Point(12.4578, 43.9424),   -- San Marino (lon, lat)
        ST_Point(12.3436, 43.8967),   -- San Leo
        'auto'
    ) as r
);

-- =============================================================================
-- Compare using different input formats (all produce same result)
-- =============================================================================
SELECT
    method,
    round(r.distance_km, 2) as km,
    round(r.duration_minutes, 1) as minutes
FROM (
    -- Option 1: Direct GEOMETRY type
    SELECT 'GEOMETRY type' as method,
           travel_time_route(ST_Point(12.4578, 43.9424), ST_Point(12.3436, 43.8967), 'auto') as r
    UNION ALL
    -- Option 2: WKT strings (no spatial extension needed)
    SELECT 'WKT string',
           travel_time_route('POINT(12.4578 43.9424)', 'POINT(12.3436 43.8967)', 'auto')
    UNION ALL
    -- Option 3: WKB format (explicit conversion)
    SELECT 'WKB blob',
           travel_time_route(ST_AsWKB(ST_Point(12.4578, 43.9424)), ST_AsWKB(ST_Point(12.3436, 43.8967)), 'auto')
);

SELECT 'Done!' as status;
