# Italy Routing Dataset Guide

Complete guide for using the DuckDB routing extension with Italy OpenStreetMap data.

## Quick Start

```bash
# 1. Download and build tiles
./examples/build_italy_tiles.sh

# 2. Test the extension
duckdb < examples/italy_route_examples.sql
```

## Building Italy Routing Tiles

### Prerequisites

```bash
# macOS
brew install valhalla

# Linux (Ubuntu/Debian)
sudo apt install valhalla-bin
```

### Manual Build Process

```bash
# Create directory
mkdir -p italy_tiles

# Download Italy OSM data (~2.1 GB)
curl -L -o italy_tiles/italy.osm.pbf \
    https://download.geofabrik.de/europe/italy-latest.osm.pbf

# Generate Valhalla config
valhalla_build_config \
    --mjolnir-tile-dir $(pwd)/italy_tiles/tiles \
    --mjolnir-tile-extract "" \
    --mjolnir-traffic-extract "" \
    --mjolnir-admin "" \
    --mjolnir-timezone "" \
    > italy_tiles/valhalla.json

# Build routing tiles (5-10 minutes)
valhalla_build_tiles -c italy_tiles/valhalla.json italy_tiles/italy.osm.pbf
```

### Build Stats

- **Input**: 2.1 GB OSM PBF file
- **Output**: 2.4 GB routing tiles
- **Build time**: ~7 minutes
- **Nodes**: 9.7M road network nodes
- **Edges**: 24.6M directed edges
- **Hierarchy levels**: 3 (local, arterial, highway)

## Basic Examples

### 1. Simple Route Query

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;

SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Rome to Florence
SELECT
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours,
    ST_GeomFromWKB(r.geometry) as route
FROM (
    SELECT travel_time_route(
        ST_Point(12.4964, 41.9028),  -- Rome (Colosseum)
        ST_Point(11.2558, 43.7696),  -- Florence (Duomo)
        'auto'
    ) as r
);
```

**Result**: 273.2 km, 2.4 hours, LINESTRING with 2857 waypoints

### 2. Multiple Cities

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Create Italian cities table
CREATE TABLE italian_cities AS
SELECT * FROM (VALUES
    ('Rome',       ST_Point(12.4964, 41.9028)),
    ('Milan',      ST_Point(9.1900, 45.4642)),
    ('Naples',     ST_Point(14.2681, 40.8518)),
    ('Turin',      ST_Point(7.6869, 45.0703)),
    ('Florence',   ST_Point(11.2558, 43.7696)),
    ('Venice',     ST_Point(12.3155, 45.4408)),
    ('Bologna',    ST_Point(11.3426, 44.4949)),
    ('Genoa',      ST_Point(8.9463, 44.4056)),
    ('Palermo',    ST_Point(13.3614, 38.1157)),
    ('Verona',     ST_Point(10.9916, 45.4384))
) AS t(city, location);

-- Routes from Rome to all cities
SELECT
    'Rome → ' || c.city as route,
    round(r.distance_km, 0) as km,
    round(r.duration_minutes / 60.0, 1) as hours
FROM italian_cities c
CROSS JOIN LATERAL (
    SELECT travel_time_route(
        (SELECT location FROM italian_cities WHERE city = 'Rome'),
        c.location,
        'auto'
    ) as result
) r(result)
WHERE c.city != 'Rome'
ORDER BY r.result.distance_km;
```

**Expected output:**

```text
┌──────────────────┬──────┬───────┐
│      route       │  km  │ hours │
├──────────────────┼──────┼───────┤
│ Rome → Naples    │  226 │   2.0 │
│ Rome → Florence  │  273 │   2.4 │
│ Rome → Bologna   │  378 │   3.2 │
│ Rome → Verona    │  478 │   4.0 │
│ Rome → Venice    │  526 │   4.4 │
│ Rome → Milan     │  575 │   4.8 │
│ Rome → Genoa     │  501 │   4.2 │
│ Rome → Turin     │  669 │   5.6 │
│ Rome → Palermo   │  934 │   9.5 │
└──────────────────┴──────┴───────┘
```

### 3. Using the GEOMETRY Macro

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Load the geometry macro
.read examples/geometry_macro.sql

-- Rome to Florence with automatic GEOMETRY conversion
WITH route AS (
    SELECT travel_time_route_geom(
        ST_Point(12.4964, 41.9028),  -- Rome
        ST_Point(11.2558, 43.7696),  -- Florence
        'auto'
    ) as r
)
SELECT
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours,
    r.geometry,  -- Already GEOMETRY type!
    ST_NPoints(r.geometry) as waypoints,
    ST_Length(r.geometry) as length_degrees
FROM route;
```

**Result:**

```text
┌────────┬────────┬──────────┬───────────┬─────────────────┐
│   km   │ hours  │ geometry │ waypoints │ length_degrees  │
├────────┼────────┼──────────┼───────────┼─────────────────┤
│  273.2 │    2.4 │ LINES... │      2857 │ 2.7788322902... │
└────────┴────────┴──────────┴───────────┴─────────────────┘
```

### 4. Travel Time Matrix

```sql
LOAD travel_time;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Travel times between 3 northern cities
SELECT
    CASE src_idx
        WHEN 0 THEN 'Milan'
        WHEN 1 THEN 'Venice'
        WHEN 2 THEN 'Turin'
    END as origin,
    CASE dst_idx
        WHEN 0 THEN 'Milan'
        WHEN 1 THEN 'Venice'
        WHEN 2 THEN 'Turin'
    END as destination,
    round(distance_m / 1000.0, 0) as km,
    round(duration_s / 3600.0, 1) as hours
FROM travel_time_matrix(
    [45.4642, 45.4408, 45.0703],  -- source lats (Milan, Venice, Turin)
    [9.1900, 12.3155, 7.6869],    -- source lons
    [45.4642, 45.4408, 45.0703],  -- dest lats
    [9.1900, 12.3155, 7.6869],    -- dest lons
    'auto'
)
WHERE src_idx != dst_idx
ORDER BY src_idx, dst_idx;
```

### 5. Multi-Mode Comparison

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Compare travel modes: Rome to Florence
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
);
```

**Result:**

```text
┌─────────┬────────┬────────┐
│  mode   │   km   │ hours  │
├─────────┼────────┼────────┤
│ Car     │  273.2 │    2.4 │
│ Truck   │  273.2 │    2.5 │
│ Bicycle │  285.4 │   15.8 │
└─────────┴────────┴────────┘
```

## Advanced Examples

### Route Analysis with Spatial Operations

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');
.read examples/geometry_macro.sql

-- Analyze route from Rome to Milan
WITH route AS (
    SELECT travel_time_route_geom(
        ST_Point(12.4964, 41.9028),  -- Rome
        ST_Point(9.1900, 45.4642),   -- Milan
        'auto'
    ) as r
)
SELECT
    'Rome to Milan' as route_name,
    round(r.distance_km, 1) as km,
    round(r.duration_minutes / 60.0, 1) as hours,
    ST_NPoints(r.geometry) as waypoints,
    round(ST_Length(r.geometry), 4) as length_degrees,
    round(r.distance_km / (r.duration_minutes / 60.0), 0) as avg_speed_kmh,
    ST_AsText(ST_StartPoint(r.geometry)) as start_point,
    ST_AsText(ST_EndPoint(r.geometry)) as end_point,
    ST_AsText(ST_Centroid(r.geometry)) as route_center
FROM route;
```

### Finding Routes Through Intermediate Points

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Rome → Florence → Venice (two-leg journey)
WITH leg1 AS (
    SELECT travel_time_route(
        ST_Point(12.4964, 41.9028),  -- Rome
        ST_Point(11.2558, 43.7696),  -- Florence
        'auto'
    ) as r
),
leg2 AS (
    SELECT travel_time_route(
        ST_Point(11.2558, 43.7696),  -- Florence
        ST_Point(12.3155, 45.4408),  -- Venice
        'auto'
    ) as r
)
SELECT
    'Rome → Florence → Venice' as route,
    round(leg1.r.distance_km + leg2.r.distance_km, 1) as total_km,
    round((leg1.r.duration_minutes + leg2.r.duration_minutes) / 60.0, 1) as total_hours
FROM leg1, leg2;
```

### Export Routes as GeoJSON

```sql
LOAD travel_time;
INSTALL spatial; LOAD spatial;
SELECT travel_time_load_config('./italy_tiles/valhalla.json');

-- Export route to GeoJSON
COPY (
    SELECT
        c.city as destination,
        round(r.distance_km, 1) as km,
        round(r.duration_minutes / 60.0, 1) as hours,
        ST_AsGeoJSON(ST_GeomFromWKB(r.geometry)) as route_geojson
    FROM (VALUES
        ('Milan', ST_Point(9.1900, 45.4642)),
        ('Naples', ST_Point(14.2681, 40.8518)),
        ('Florence', ST_Point(11.2558, 43.7696))
    ) AS c(city, location)
    CROSS JOIN LATERAL (
        SELECT travel_time_route(
            ST_Point(12.4964, 41.9028),  -- Rome
            c.location,
            'auto'
        ) as result
    ) r(result)
) TO 'rome_routes.json' (FORMAT JSON, ARRAY true);
```

## Italian Cities Reference

Major cities with coordinates:

| City | Coordinates | Region |
|------|-------------|--------|
| Rome | 12.4964, 41.9028 | Lazio |
| Milan | 9.1900, 45.4642 | Lombardy |
| Naples | 14.2681, 40.8518 | Campania |
| Turin | 7.6869, 45.0703 | Piedmont |
| Florence | 11.2558, 43.7696 | Tuscany |
| Venice | 12.3155, 45.4408 | Veneto |
| Bologna | 11.3426, 44.4949 | Emilia-Romagna |
| Genoa | 8.9463, 44.4056 | Liguria |
| Palermo | 13.3614, 38.1157 | Sicily |
| Verona | 10.9916, 45.4384 | Veneto |
| Bari | 16.8719, 41.1171 | Apulia |
| Catania | 15.0871, 37.5079 | Sicily |
| Pisa | 10.4016, 43.7228 | Tuscany |
| Siena | 11.3308, 43.3188 | Tuscany |

## Automated Build Script

Use the provided script for easy setup:

```bash
./examples/build_italy_tiles.sh
```

The script will:

1. Create `italy_tiles` directory
2. Download Italy OSM data (2.1 GB)
3. Generate Valhalla configuration
4. Build routing tiles (2.4 GB, ~7 minutes)
5. Verify the build

## Performance Tips

1. **Initial load**: First query is slower due to tile loading (~2-3 seconds)
2. **Subsequent queries**: Cached, much faster (<100ms)
3. **Long routes**: Cross-country routes (500+ km) take 1-2 seconds
4. **Matrix queries**: Scale with O(n×m) for n origins and m destinations

## Troubleshooting

### Tiles not found

```text
Error: Config file not found
```

**Solution**: Check path to `valhalla.json`:

```sql
SELECT travel_time_load_config('./italy_tiles/valhalla.json');
```

### Out of memory

If building tiles fails with OOM:

- Close other applications
- Requires ~4-6 GB RAM for Italy dataset
- Consider building smaller regions (e.g., Sicily, Tuscany)

### Spatial extension not loaded

```text
Error: ST_Point not found
```

**Solution**: Install spatial extension:

```sql
INSTALL spatial;
LOAD spatial;
```

## Regional Datasets

For smaller regions, download from Geofabrik:

```bash
# Sicily only (~180 MB)
curl -L -o sicily.osm.pbf \
    https://download.geofabrik.de/europe/italy/sicilia-latest.osm.pbf

# Tuscany only (~120 MB)
curl -L -o tuscany.osm.pbf \
    https://download.geofabrik.de/europe/italy/toscana-latest.osm.pbf

# Lombardy only (~250 MB)
curl -L -o lombardy.osm.pbf \
    https://download.geofabrik.de/europe/italy/lombardia-latest.osm.pbf
```

Then build tiles as usual:

```bash
valhalla_build_tiles -c config.json region.osm.pbf
```

## See Also

- [README.md](README.md) - Main documentation
- [examples/geometry_macro.sql](examples/geometry_macro.sql) - GEOMETRY type macro
- [examples/build_italy_tiles.sh](examples/build_italy_tiles.sh) - Automated build script
- [Geofabrik Italy Downloads](https://download.geofabrik.de/europe/italy.html) - OSM data source
