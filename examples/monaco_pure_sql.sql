-- Monaco Pure SQL Workflow (Future Version)
-- This script shows what the complete workflow will look like when
-- valhalla_build_tiles() SQL function is fully implemented

.timer on

-- ============================================================================
-- SETUP
-- ============================================================================

INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial;
LOAD valhalla_routing;

SELECT '╔════════════════════════════════════════════════════════╗';
SELECT '║   Monaco Routing - 100% Pure SQL Workflow             ║';
SELECT '╚════════════════════════════════════════════════════════╝';
SELECT '';

-- ============================================================================
-- STEP 1 & 2: Download and Build Tiles (Pure SQL!)
-- ============================================================================

SELECT '📥🔨 Step 1: Download Monaco and build tiles';
SELECT '';

-- Download directly from Geofabrik and build tiles in one command!
-- httpfs extension allows reading from HTTP URLs directly
SELECT valhalla_build_tiles(
    'https://download.geofabrik.de/europe/monaco-latest.osm.pbf',
    './monaco_tiles'
) as config_path;

SELECT '   ✓ Downloaded and built tiles';
SELECT '';

-- ============================================================================
-- STEP 2: Load Tiles (Pure SQL)
-- ============================================================================

SELECT '⚙️  Step 2: Load Valhalla tiles';
SELECT '';

-- Smart path detection: auto-appends /valhalla.json
SET valhalla_tiles = './monaco_tiles';

SELECT '   ✓ Config loaded';
SELECT '';

-- ============================================================================
-- STEP 4: Create Helper Macro (Pure SQL)
-- ============================================================================

CREATE OR REPLACE MACRO travel_time_route(from_geom, to_geom, costing) AS (
    SELECT struct_pack(
        distance_km := r.distance_km,
        duration_minutes := r.duration_minutes,
        geometry := ST_GeomFromWKB(r.geometry)
    ) FROM (SELECT travel_time_route_wkb(from_geom, to_geom, costing) as r)
);

-- ============================================================================
-- STEP 5: Route from Casino Royale to Oceanographic Museum (Pure SQL)
-- ============================================================================

SELECT '🎰 → 🏛️  Step 5: Route Casino → Museum';
SELECT '';

WITH route AS (
    SELECT travel_time_route(
        ST_Point(7.4275, 43.7397),  -- Casino de Monte-Carlo
        ST_Point(7.4254, 43.7308),  -- Musée Océanographique
        'auto'
    ) as r
)
SELECT
    '🎰 Casino Royale → 🏛️ Oceanographic Museum' as route,
    round(r.distance_km, 3) as distance_km,
    round(r.duration_minutes, 2) as duration_minutes,
    round(r.duration_minutes * 60, 0) as duration_seconds,
    ST_NPoints(r.geometry) as waypoints
FROM route;

SELECT '';

-- ============================================================================
-- STEP 6: Export to GeoJSON (Pure SQL)
-- ============================================================================

SELECT '💾 Step 6: Export route';
SELECT '';

COPY (
    WITH route AS (
        SELECT travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            'auto'
        ) as r
    )
    SELECT json_object(
        'type', 'FeatureCollection',
        'features', json_array(
            json_object(
                'type', 'Feature',
                'properties', json_object(
                    'name', 'Casino to Museum',
                    'distance_km', round(r.distance_km, 3),
                    'duration_min', round(r.duration_minutes, 2)
                ),
                'geometry', ST_AsGeoJSON(r.geometry)::JSON
            )
        )
    )
    FROM route
) TO 'monaco_route.geojson';

SELECT '   ✓ Saved: monaco_route.geojson';
SELECT '';

-- ============================================================================
-- BONUS: Additional Routes (Pure SQL)
-- ============================================================================

SELECT '🗺️  Bonus: More Monaco routes';
SELECT '';

WITH routes AS (
    SELECT '🎰 Casino → 🚂 Station' as route,
           ST_Point(7.4275, 43.7397) as from_pt,
           ST_Point(7.4164, 43.7304) as to_pt
    UNION ALL
    SELECT '🎰 Casino → 🏰 Palace',
           ST_Point(7.4275, 43.7397),
           ST_Point(7.4199, 43.7314)
    UNION ALL
    SELECT '🏛️ Museum → ⚓ Port',
           ST_Point(7.4254, 43.7308),
           ST_Point(7.4195, 43.7338)
)
SELECT
    route,
    round(r.distance_km, 2) as km,
    round(r.duration_minutes, 1) as min
FROM (
    SELECT route, travel_time_route(from_pt, to_pt, 'auto') as r
    FROM routes
)
ORDER BY km;

SELECT '';

-- ============================================================================
-- BONUS: Multi-Modal (Pure SQL)
-- ============================================================================

SELECT '🚗 🚶 🚲 Bonus: Travel modes';
SELECT '';

WITH modes AS (
    SELECT '🚗 Car' as mode, 'auto' as costing
    UNION ALL SELECT '🚶 Walk', 'pedestrian'
    UNION ALL SELECT '🚲 Bike', 'bicycle'
)
SELECT
    mode,
    round(r.distance_km, 2) as km,
    round(r.duration_minutes, 1) as min,
    round(r.distance_km / (r.duration_minutes / 60.0), 1) as kmh
FROM (
    SELECT
        mode,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            costing
        ) as r
    FROM modes
)
ORDER BY min;

SELECT '';
SELECT '✅ Complete! Pure SQL workflow from download to route.';
