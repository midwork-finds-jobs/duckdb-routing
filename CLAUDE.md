# travel_time - DuckDB Routing Extension with Valhalla

DuckDB extension for travel time calculations using Valhalla routing engine.

## Architecture

```text
┌─────────────────────────┐     ┌──────────────────────────┐
│  DuckDB Extension       │     │  Valhalla Wrapper        │
│  (C++11)                │────▶│  (C++20)                 │
│                         │  C  │                          │
│  travel_time_extension  │ ABI │  libvalhalla_wrapper     │
└─────────────────────────┘     └──────────────────────────┘
                                           │
                                           ▼
                                ┌──────────────────────────┐
                                │  Valhalla (C++20)        │
                                │  libvalhalla             │
                                └──────────────────────────┘
```

## Build

### 1. Build Valhalla Wrapper (requires Valhalla installed)

```bash
cd valhalla-wrapper
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ../..
```

### 2. Build DuckDB Extension

```bash
make release GEN=ninja
```

## Test

```bash
make test
```

## Directory Structure

- `valhalla-wrapper/` - C wrapper for Valhalla (C++20)
  - `include/valhalla_wrapper.h` - Pure C API
  - `src/wrapper.cpp` - Implementation using Valhalla Actor
- `src/travel_time_extension.cpp` - DuckDB extension (C++11)
- `src/include/` - C++ headers

## SQL Functions

```sql
-- Load Valhalla config
SELECT travel_time_load_config('/path/to/valhalla.json');

-- Route with WKT
SELECT travel_time_route('POINT(12.45 43.94)', 'POINT(12.34 43.89)', 'auto');

-- Route with WKB (spatial extension)
SELECT travel_time_route(ST_AsWKB(geom1), ST_AsWKB(geom2), 'auto');

-- Raw JSON API
SELECT travel_time_request('route', '{"locations":[...]}');
```

## Costing Models

- `auto`, `bicycle`, `pedestrian`, `truck`, `motorcycle`

## Data Preparation

Valhalla requires preprocessed tiles:

```bash
valhalla_build_config --mjolnir-tile-dir ./tiles > valhalla.json
valhalla_build_tiles -c valhalla.json region.osm.pbf
```

## Don't

- Don't call shell commands from C++
- Don't use emojis in code
