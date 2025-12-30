-- Monaco End-to-End Example
-- Downloads Monaco OSM data, builds tiles, and routes from Casino to Museum

-- This script demonstrates the complete workflow:
-- 1. Download Monaco PBF from Geofabrik
-- 2. Build Valhalla tiles
-- 3. Load config
-- 4. Route from Casino Royale to Oceanographic Museum

.timer on

SELECT '=== Monaco End-to-End Routing Example ===' as title;
SELECT '' as separator;

-- Load extensions
INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial;
LOAD valhalla_routing;

SELECT '1. Downloading Monaco OSM data from Geofabrik...' as step;

-- Download Monaco PBF from Geofabrik to local file
-- Geofabrik URL: https://download.geofabrik.de/europe/monaco-latest.osm.pbf
COPY (
    SELECT * FROM read_blob('https://download.geofabrik.de/europe/monaco-latest.osm.pbf')
) TO 'monaco.osm.pbf';

SELECT '   Downloaded monaco.osm.pbf' as status;
SELECT '' as separator;

-- Build Valhalla tiles
SELECT '2. Building Valhalla tiles...' as step;
SELECT '   (This will take 1-2 minutes)' as note;

-- Using valhalla_build_tiles function (when implemented)
SELECT valhalla_build_tiles('monaco.osm.pbf', 'monaco_tiles') as config_path;

SELECT '   ✓ Tiles built successfully' as status;
SELECT '' as separator;

-- Load Valhalla config
SELECT '3. Loading Valhalla configuration...' as step;
SET valhalla_config = 'monaco_tiles/valhalla.json';
SELECT '   ✓ Config loaded' as status;
SELECT '' as separator;

-- Define geometry macro
CREATE OR REPLACE MACRO travel_time_route(from_geom, to_geom, costing) AS (
    SELECT struct_pack(
        distance_km := r.distance_km,
        duration_minutes := r.duration_minutes,
        geometry := ST_GeomFromWKB(r.geometry)
    ) FROM (SELECT travel_time_route_wkb(from_geom, to_geom, costing) as r)
);

-- Route from Casino Royale to Oceanographic Museum
SELECT '4. Routing from Casino Royale to Oceanographic Museum...' as step;
SELECT '' as separator;

SELECT '=== Route Details ===' as section;

WITH route AS (
    SELECT travel_time_route(
        ST_Point(7.4275, 43.7397),  -- Casino de Monte-Carlo
        ST_Point(7.4254, 43.7308),  -- Musée Océanographique
        'auto'
    ) as r
)
SELECT
    'Casino Royale → Oceanographic Museum' as route,
    round(r.distance_km, 2) as distance_km,
    round(r.duration_minutes, 1) as duration_minutes,
    round(r.duration_minutes * 60, 0) as duration_seconds,
    ST_NPoints(r.geometry) as waypoints,
    ST_AsText(ST_StartPoint(r.geometry)) as start_point,
    ST_AsText(ST_EndPoint(r.geometry)) as end_point,
    ST_AsText(r.geometry) as route_geometry
FROM route;

SELECT '' as separator;

-- Export route to GeoJSON for visualization
SELECT '5. Exporting route to GeoJSON...' as step;

COPY (
    WITH route AS (
        SELECT travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            'auto'
        ) as r
    )
    SELECT json_object(
        'type', 'Feature',
        'properties', json_object(
            'route', 'Casino Royale → Oceanographic Museum',
            'distance_km', round(r.distance_km, 2),
            'duration_minutes', round(r.duration_minutes, 1)
        ),
        'geometry', ST_AsGeoJSON(r.geometry)::JSON
    ) as feature
    FROM route
) TO 'monaco_route.geojson';

SELECT '   ✓ Route exported to monaco_route.geojson' as status;
SELECT '' as separator;

-- Additional routes in Monaco
SELECT '=== Additional Monaco Routes ===' as section;

SELECT
    origin || ' → ' || destination as route,
    round(r.distance_km, 2) as km,
    round(r.duration_minutes, 1) as minutes
FROM (
    -- Casino to Train Station
    SELECT
        'Casino' as origin,
        'Train Station' as destination,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4164, 43.7304),
            'auto'
        ) as r

    UNION ALL

    -- Casino to Prince's Palace
    SELECT
        'Casino' as origin,
        'Prince''s Palace' as destination,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4199, 43.7314),
            'auto'
        ) as r

    UNION ALL

    -- Oceanographic Museum to Port Hercules
    SELECT
        'Oceanographic Museum' as origin,
        'Port Hercules' as destination,
        travel_time_route(
            ST_Point(7.4254, 43.7308),
            ST_Point(7.4195, 43.7338),
            'auto'
        ) as r
)
ORDER BY km;

SELECT '' as separator;

-- Test different transportation modes
SELECT '=== Multi-Modal Comparison (Casino → Museum) ===' as section;

SELECT
    mode,
    round(r.distance_km, 2) as km,
    round(r.duration_minutes, 1) as minutes,
    round(r.distance_km / (r.duration_minutes / 60.0), 1) as avg_speed_kmh
FROM (
    SELECT
        'Car' as mode,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            'auto'
        ) as r

    UNION ALL

    SELECT
        'Pedestrian' as mode,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            'pedestrian'
        ) as r

    UNION ALL

    SELECT
        'Bicycle' as mode,
        travel_time_route(
            ST_Point(7.4275, 43.7397),
            ST_Point(7.4254, 43.7308),
            'bicycle'
        ) as r
)
ORDER BY minutes;

SELECT '' as separator;
SELECT '✅ Monaco routing demonstration complete!' as status;
SELECT '' as separator;

-- Summary
SELECT '=== Summary ===' as section;
SELECT 'Data source: Geofabrik (https://download.geofabrik.de/)' as info;
SELECT 'Tile format: Valhalla routing tiles' as info;
SELECT 'Coverage: Monaco (2.02 km²)' as info;
SELECT 'Routes tested: 4 different routes, 3 travel modes' as info;
SELECT 'Output: monaco_route.geojson (viewable in QGIS, kepler.gl, etc.)' as info;
