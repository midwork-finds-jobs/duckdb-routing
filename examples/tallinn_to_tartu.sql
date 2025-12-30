-- Route from Tallinn to Tartu (Estonia)
--
-- Coordinates:
--   Tallinn (city center): 59.4370, 24.7536
--   Tartu (city center):   58.3780, 26.7290
--
-- Prerequisites:
--   1. Build tiles: See README.md "Building Routing Tiles" section
--   2. Adjust config path below to match your setup
--
-- Run: duckdb < examples/tallinn_to_tartu.sql

-- Load extensions
INSTALL spatial; LOAD spatial;

-- Load routing tiles (adjust path as needed)
SELECT travel_time_load_config('./estonia_tiles/valhalla.json') as loaded;

-- =============================================================================
-- Example 1: Simple point-to-point route with GEOMETRY type
-- =============================================================================
SELECT
    'Tallinn → Tartu' as route,
    round(r.distance_km, 1) as distance_km,
    round(r.duration_minutes, 0) as duration_min
FROM (
    SELECT travel_time_route(
        ST_Point(24.7536, 59.4370),  -- Tallinn (lon, lat)
        ST_Point(26.7290, 58.3780),  -- Tartu
        'auto'
    ) as r
);

-- =============================================================================
-- Example 2: Route with geometry preview
-- =============================================================================
SELECT
    'Tallinn → Tartu (with geometry)' as description,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes,
    substr(r.geometry, 1, 80) || '...' as route_preview
FROM (
    SELECT travel_time_route(
        ST_Point(24.7536, 59.4370),
        ST_Point(26.7290, 58.3780),
        'auto'
    ) as r
);

-- =============================================================================
-- Example 3: Multi-city routes from Tallinn
-- =============================================================================
WITH cities AS (
    SELECT 'Tartu' as name, 26.7290 as lon, 58.3780 as lat UNION ALL
    SELECT 'Pärnu', 24.5035, 58.3859 UNION ALL
    SELECT 'Narva', 28.1790, 59.3797 UNION ALL
    SELECT 'Viljandi', 25.5900, 58.3639 UNION ALL
    SELECT 'Haapsalu', 23.5378, 58.9431
)
SELECT
    'Tallinn → ' || c.name as route,
    round(travel_time_route(
        ST_Point(24.7536, 59.4370),
        ST_Point(c.lon, c.lat),
        'auto'
    ).distance_km, 1) as km,
    round(travel_time_route(
        ST_Point(24.7536, 59.4370),
        ST_Point(c.lon, c.lat),
        'auto'
    ).duration_minutes, 0) as minutes
FROM cities c
ORDER BY km;

-- =============================================================================
-- Example 4: Compare travel modes
-- =============================================================================
SELECT
    mode,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes
FROM (
    SELECT 'auto' as mode, travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'auto') as r
    UNION ALL
    SELECT 'truck', travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'truck')
    UNION ALL
    SELECT 'bicycle', travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'bicycle')
);

-- =============================================================================
-- Example 5: Using WKT strings (no spatial extension needed)
-- =============================================================================
SELECT
    'WKT string route' as method,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes
FROM (
    SELECT travel_time_route_wkb(
        ST_Point(24.7536, 59.4370),  -- Tallinn (lon, lat)
        ST_Point(26.7290, 58.3780),  -- Tartu
        'auto'
    ) as r
);

SELECT 'Done!' as status;
