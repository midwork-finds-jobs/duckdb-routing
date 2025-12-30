-- S3 Tiles Example
-- Demonstrates loading Valhalla tiles from S3

-- Prerequisites:
--   1. Upload tiles to S3: aws s3 sync ./tiles/ s3://bucket/valhalla/tiles/
--   2. Set AWS credentials (or use IAM role)
--   3. Run this script: duckdb -f examples/s3_tiles_example.sql

-- Load extensions
INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial;
LOAD valhalla_routing;

SELECT '=== S3 Tiles Example ===' as example;
SELECT '' as separator;

-- Configure S3 (skip if using IAM role)
SELECT '1. Configuring S3...' as step;
SET s3_region='us-east-1';
-- SET s3_access_key_id='YOUR_KEY';
-- SET s3_secret_access_key='YOUR_SECRET';

-- Load tiles from S3 (downloads and caches locally)
SELECT '2. Loading tiles from S3...' as step;
SET valhalla_config = 's3://my-bucket/valhalla/valhalla.json';

-- Load geometry macro
SELECT '4. Loading geometry macro...' as step;
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

-- Example 2: Cache info
SELECT '=== Cache Information ===' as example;
SELECT '📁 Tiles cached to: $TMPDIR/valhalla_cache/tiles_*/' as cache_location;
SELECT '💾 Subsequent loads use cache (instant)' as cache_benefit;
SELECT '🗑️  Clear cache: rm -rf $TMPDIR/valhalla_cache/' as clear_command;

SELECT '' as separator;

-- Example 3: Check current config
SELECT '=== Current Configuration ===' as example;
SELECT current_setting('valhalla_config') as config_path;

SELECT '' as separator;

-- Example 4: Multi-region
SELECT '=== Multi-Region Example ===' as example;
SELECT 'Switch between regions by loading different S3 paths:' as info;

-- Europe tiles
-- SET valhalla_config = 's3://tiles-eu/valhalla.json';
-- SELECT * FROM travel_time_route(...);

-- US tiles
-- SET valhalla_config = 's3://tiles-us/valhalla.json';
-- SELECT * FROM travel_time_route(...);

SELECT '' as separator;
SELECT '✅ S3 tiles working successfully!' as status;
