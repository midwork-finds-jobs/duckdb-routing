# Release v0.1.0 - DuckDB Valhalla Routing Extension

First public release of the DuckDB Valhalla routing extension!

## 🎯 Features

### Core Routing Functions

- **Point-to-point routing** with distance, duration, and route geometry
- **Travel time matrices** for N×M origin-destination calculations
- **Road network snapping** to find nearest routable locations
- **Raw JSON API** access to Valhalla's full capabilities

### Input/Output Flexibility

- **Multiple input formats**: WKT strings, WKB blobs, or GEOMETRY types
- **GEOMETRY type output** via SQL macro (recommended)
- **WKB BLOB output** for custom processing
- Seamless integration with DuckDB spatial extension

### Travel Modes

Supports multiple routing profiles:

- `auto` - Car/automobile routing
- `bicycle` - Bicycle routing
- `pedestrian` - Walking routes
- `truck` - Truck routing with restrictions
- `bus`, `taxi`, `motor_scooter` - Additional modes

## 📚 API Reference

### Recommended Functions

```sql
-- Load routing tiles
SELECT travel_time_load_config('./valhalla_data/valhalla.json');

-- Route with GEOMETRY output (recommended)
.read examples/geometry_macro.sql
SELECT * FROM travel_time_route(
    ST_Point(12.4964, 41.9028),  -- Rome
    ST_Point(11.2558, 43.7696),  -- Florence
    'auto'
);
-- Returns: {distance_km, duration_minutes, geometry: GEOMETRY}
```

### All Functions

- `travel_time_route(from, to, costing)` → STRUCT{distance_km, duration_minutes, geometry: GEOMETRY}
- `travel_time_route_wkb(from, to, costing)` → STRUCT{distance_km, duration_minutes, geometry: BLOB}
- `travel_time(lat1, lon1, lat2, lon2, costing)` → DOUBLE (seconds)
- `travel_time_matrix(src_lats, src_lons, dst_lats, dst_lons, costing)` → TABLE
- `travel_time_locate(lat, lon, costing)` → STRUCT{lat, lon}
- `travel_time_request(action, json)` → VARCHAR

## 📖 Documentation

- **[README.md](README.md)** - Quick start, API reference, Estonia examples
- **[ITALY.md](ITALY.md)** - Complete Italy dataset guide with 5 examples
- **[examples/](examples/)** - Ready-to-use SQL examples
  - `geometry_macro.sql` - GEOMETRY type macro
  - `italy_route_examples.sql` - 5 Italy routing examples
  - `build_italy_tiles.sh` - Automated tile building
  - `tallinn_to_tartu.sql` - Estonia example

## 🧪 Tests

- **30+ comprehensive test cases** in `test/sql/monaco_routing.test`
- Tests for WKT, WKB, and GEOMETRY inputs
- Multi-mode routing tests
- NULL handling and edge cases
- Matrix query tests

## 🗺️ Example Datasets

### Italy (2.4 GB)

- Routes between major cities: Rome, Milan, Venice, Florence, Naples
- Complete guide in ITALY.md
- Automated build script included

### Estonia

- Tallinn to Tartu examples
- Smaller dataset for quick testing

### Monaco

- Test data for unit tests
- Minimal size for CI/CD

## 🏗️ Building from Source

```bash
# 1. Install Valhalla
brew install valhalla  # macOS
# or
sudo apt install valhalla-bin  # Linux

# 2. Build Valhalla C wrapper
cd valhalla-wrapper
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ../..

# 3. Build DuckDB extension
make release GEN=ninja

# 4. Run tests
make test
```

## 📦 Preparing Routing Data

```bash
# Download OSM extract
curl -o italy.osm.pbf https://download.geofabrik.de/europe/italy-latest.osm.pbf

# Build Valhalla config
valhalla_build_config --mjolnir-tile-dir ./tiles > valhalla.json

# Build routing tiles
valhalla_build_tiles -c valhalla.json italy.osm.pbf
```

## 🔧 Technical Details

### Architecture

- **DuckDB Extension**: C++11
- **Valhalla Wrapper**: C++20 with C ABI
- **Routing Engine**: Valhalla (C++20)

### Return Types

- **WKB Format**: Standard Well-Known Binary (LINESTRING)
- **GEOMETRY Format**: DuckDB spatial extension types
- **Coordinate System**: WGS84 (EPSG:4326)

## 🙏 Acknowledgments

Built on top of:

- [DuckDB](https://duckdb.org/) - High-performance analytical database
- [Valhalla](https://github.com/valhalla/valhalla) - Open-source routing engine
- [OpenStreetMap](https://www.openstreetmap.org/) - Collaborative map data

## 📝 License

MIT License - see LICENSE file for details
