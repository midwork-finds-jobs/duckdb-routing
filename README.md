# travel_time - DuckDB Routing Extension

DuckDB extension for travel time and route calculations using [Valhalla](https://github.com/valhalla/valhalla) routing engine.

## Features

- Point-to-point routing with distance, duration, and route geometry
- Supports `auto`, `bicycle`, `pedestrian`, `truck`, and more travel modes
- Matrix queries: N×M origin-destination travel times
- Raw JSON API for advanced Valhalla features
- Direct GEOMETRY type support (works with DuckDB spatial extension)
- Accepts WKT strings, WKB blobs, or GEOMETRY types

## Quick Example: Tallinn to Tartu

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;

-- Load routing tiles
SELECT travel_time_load_config('./estonia_tiles/valhalla.json');

-- Route from Tallinn to Tartu
SELECT
    round(r.distance_km, 1) as distance_km,
    round(r.duration_minutes, 0) as duration_min,
    ST_GeomFromWKB(r.geometry) as route
FROM (
    SELECT travel_time_route(
        ST_Point(24.7536, 59.4370),  -- Tallinn
        ST_Point(26.7290, 58.3780),  -- Tartu
        'auto'
    ) as r
);
-- Result: 186.4 km, 127 min, LINESTRING GEOMETRY with 1500+ points
```

## Dataset-Specific Guides

📖 **[Italy Dataset Guide](ITALY.md)** - Complete guide for routing across Italy with examples:

- Automated tile building script
- Routes between major cities (Rome, Milan, Venice, etc.)
- Multi-mode travel comparisons
- Tuscany regional examples
- GeoJSON export examples

## Building Routing Tiles

Before using the extension, you need to build Valhalla routing tiles from OpenStreetMap data.

### Prerequisites

```bash
# macOS
brew install valhalla

# Linux (Ubuntu/Debian)
sudo apt install valhalla-bin
```

### Step 1: Download OSM Data

```bash
# Create data directory
mkdir -p estonia_tiles

# Download Estonia OSM extract (~150MB)
curl -o estonia_tiles/estonia.osm.pbf \
  https://download.geofabrik.de/europe/estonia-latest.osm.pbf
```

### Step 2: Generate Valhalla Config

```bash
# Generate config with tile directory
valhalla_build_config \
  --mjolnir-tile-dir $(pwd)/estonia_tiles/tiles \
  --mjolnir-tile-extract "" \
  --mjolnir-traffic-extract "" \
  --mjolnir-admin "" \
  --mjolnir-timezone "" \
  > estonia_tiles/valhalla.json
```

### Step 3: Build Routing Tiles

```bash
# Build tiles (takes 2-5 minutes for Estonia)
valhalla_build_tiles -c estonia_tiles/valhalla.json estonia_tiles/estonia.osm.pbf

# Verify tiles were created
ls estonia_tiles/tiles/
```

### Step 4: Test the Extension

```bash
./build/release/duckdb -c "
LOAD './build/release/extension/travel_time/travel_time.duckdb_extension';
INSTALL spatial; LOAD spatial;

SELECT travel_time_load_config('./estonia_tiles/valhalla.json');

-- Tallinn to Tartu driving route
SELECT
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes,
    substr(r.geometry, 1, 100) || '...' as route_preview
FROM (
    SELECT travel_time_route(
        ST_Point(24.7536, 59.4370),
        ST_Point(26.7290, 58.3780),
        'auto'
    ) as r
);
"
```

## API Reference

### `travel_time_load_config(config_path VARCHAR) -> BOOLEAN`

Load Valhalla routing tiles from a config file.

```sql
SELECT travel_time_load_config('./estonia_tiles/valhalla.json');
```

### `travel_time_route(from, to, costing) -> STRUCT` ⭐ Recommended

**Requires spatial extension and geometry macro.**

Calculate route between two points with GEOMETRY type output. Returns struct with:

- `distance_km` - Total distance in kilometers
- `duration_minutes` - Estimated travel time in minutes
- `geometry` - Route path as GEOMETRY type (ready for spatial operations)

```sql
-- Load geometry macro (creates travel_time_route from travel_time_route_wkb)
.read examples/geometry_macro.sql

-- Use with GEOMETRY type (recommended)
WITH route AS (
    SELECT travel_time_route(
        ST_Point(12.4964, 41.9028),  -- Rome
        ST_Point(11.2558, 43.7696),  -- Florence
        'auto'
    ) as r
)
SELECT
    r.distance_km,
    r.duration_minutes,
    r.geometry,  -- Already GEOMETRY type!
    ST_NPoints(r.geometry) as waypoints
FROM route;
```

Costing modes: `auto`, `bicycle`, `pedestrian`, `truck`, `taxi`, `bus`, `motor_scooter`

### `travel_time_route_wkb(from, to, costing) -> STRUCT`

**Lower-level function. Use `travel_time_route` instead for GEOMETRY output.**

Calculate route between two points with WKB BLOB output. Returns struct with:

- `distance_km` - Total distance in kilometers
- `duration_minutes` - Estimated travel time in minutes
- `geometry` - Route path as WKB BLOB (use `ST_GeomFromWKB()` to convert to GEOMETRY)

Accepts any geometry format:

```sql
-- WKT strings
SELECT travel_time_route_wkb('POINT(24.75 59.43)', 'POINT(26.72 58.37)', 'auto');

-- GEOMETRY type (spatial extension)
SELECT travel_time_route_wkb(ST_Point(24.75, 59.43), ST_Point(26.72, 58.37), 'auto');

-- WKB blobs
SELECT travel_time_route_wkb(ST_AsWKB(origin), ST_AsWKB(dest), 'auto') FROM points;
```

**Converting WKB to GEOMETRY:**

```sql
WITH route AS (
    SELECT travel_time_route_wkb(
        ST_Point(12.4964, 41.9028),
        ST_Point(11.2558, 43.7696),
        'auto'
    ) as r
)
SELECT
    r.distance_km,
    r.duration_minutes,
    ST_GeomFromWKB(r.geometry) as route_geom,  -- Convert to GEOMETRY
    ST_NPoints(ST_GeomFromWKB(r.geometry)) as num_points
FROM route;
```

### `travel_time(lat1, lon1, lat2, lon2, costing) -> DOUBLE`

Quick travel time calculation in seconds.

```sql
SELECT travel_time(59.4370, 24.7536, 58.3780, 26.7290, 'auto') / 60 as minutes;
```

### `travel_time_matrix(src_lats, src_lons, dst_lats, dst_lons, costing) -> TABLE`

Compute distance/duration matrix between multiple origins and destinations.

```sql
SELECT * FROM travel_time_matrix(
    [59.4370, 59.4300],  -- source latitudes
    [24.7536, 24.7400],  -- source longitudes
    [58.3780, 58.3800],  -- destination latitudes
    [26.7290, 26.7300],  -- destination longitudes
    'auto'
);
```

Returns: `from_idx`, `to_idx`, `distance_m`, `duration_s`

### `travel_time_locate(lat, lon, costing) -> STRUCT`

Snap coordinates to nearest road network.

```sql
SELECT travel_time_locate(59.4370, 24.7536, 'auto');
-- Returns: {lat: 59.4371, lon: 24.7538}
```

### `travel_time_request(action, json) -> VARCHAR`

Raw Valhalla JSON API access for advanced features.

```sql
SELECT travel_time_request('route', '{
    "locations": [
        {"lat": 59.4370, "lon": 24.7536},
        {"lat": 58.3780, "lon": 26.7290}
    ],
    "costing": "auto",
    "directions_options": {"units": "kilometers"}
}');
```

### `travel_time_is_loaded() -> BOOLEAN`

Check if routing tiles are loaded.

```sql
SELECT travel_time_is_loaded();
```

## Complete Example: Estonia Routes

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;

SELECT travel_time_load_config('./estonia_tiles/valhalla.json');

-- Create Estonian cities table
CREATE TABLE cities AS
SELECT * FROM (VALUES
    ('Tallinn',  ST_Point(24.7536, 59.4370)),
    ('Tartu',    ST_Point(26.7290, 58.3780)),
    ('Narva',    ST_Point(28.1790, 59.3797)),
    ('Pärnu',    ST_Point(24.5035, 58.3859)),
    ('Viljandi', ST_Point(25.5900, 58.3639))
) AS t(name, geom);

-- Route from Tallinn to all other cities
SELECT
    'Tallinn' as origin,
    c.name as destination,
    round(r.route.distance_km, 1) as km,
    round(r.route.duration_minutes, 0) as minutes,
    ST_GeomFromWKB(r.route.geometry) as route_geom
FROM cities c
CROSS JOIN LATERAL (
    SELECT travel_time_route(
        (SELECT geom FROM cities WHERE name = 'Tallinn'),
        c.geom,
        'auto'
    ) as route
) r
WHERE c.name != 'Tallinn'
ORDER BY r.route.distance_km;
```

Expected output:

```text
┌─────────┬─────────────┬───────┬─────────┐
│ origin  │ destination │  km   │ minutes │
├─────────┼─────────────┼───────┼─────────┤
│ Tallinn │ Pärnu       │ 128.5 │      89 │
│ Tallinn │ Viljandi    │ 161.2 │     108 │
│ Tallinn │ Tartu       │ 186.4 │     127 │
│ Tallinn │ Narva       │ 211.3 │     149 │
└─────────┴─────────────┴───────┴─────────┘
```

## Multi-Mode Comparison

```sql
-- Compare travel modes: Tallinn to Tartu
SELECT
    mode,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes
FROM (
    SELECT 'auto' as mode, travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'auto') as r
    UNION ALL
    SELECT 'bicycle', travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'bicycle')
    UNION ALL
    SELECT 'truck', travel_time_route(ST_Point(24.7536, 59.4370), ST_Point(26.7290, 58.3780), 'truck')
);
```

## Building from Source

```bash
# 1. Install dependencies
brew install valhalla  # macOS

# 2. Build Valhalla wrapper
cd valhalla-wrapper
mkdir -p build && cd build
cmake .. && make -j
cd ../..

# 3. Build DuckDB extension
make release GEN=ninja

# 4. Run tests
make test
```

## OSM Data Sources

| Region | Size | Download |
|--------|------|----------|
| Estonia | ~150 MB | [geofabrik.de](https://download.geofabrik.de/europe/estonia-latest.osm.pbf) |
| Monaco | ~1 MB | [geofabrik.de](https://download.geofabrik.de/europe/monaco-latest.osm.pbf) |
| Latvia | ~200 MB | [geofabrik.de](https://download.geofabrik.de/europe/latvia-latest.osm.pbf) |
| Lithuania | ~250 MB | [geofabrik.de](https://download.geofabrik.de/europe/lithuania-latest.osm.pbf) |
| Germany | ~4 GB | [geofabrik.de](https://download.geofabrik.de/europe/germany-latest.osm.pbf) |

For more regions: [download.geofabrik.de](https://download.geofabrik.de/)

## License

MIT
