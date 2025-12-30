# Build Valhalla Tiles for Europe with Elevation Data

Self-contained script to build complete Valhalla routing tiles for all of Europe with elevation data.

## Quick Start

```bash
# Copy script to server
scp build_europe_tiles_with_elevation.sh user@server:/path/to/

# SSH to server
ssh user@server

# Run the build (uses current directory)
./build_europe_tiles_with_elevation.sh

# Or specify output directory
./build_europe_tiles_with_elevation.sh /data/valhalla/europe
```

## What It Does

1. **Downloads OSM data** (~30GB) for all of Europe from Geofabrik
2. **Downloads elevation data** (~100GB) from AWS Open Data terrain tiles
3. **Generates Valhalla config** with proper elevation settings
4. **Builds routing tiles** (~150GB) with elevation-aware routing
5. **Verifies output** and creates summary report

## Requirements

### Software
- **Valhalla** (valhalla_build_config, valhalla_build_tiles)
- **curl** or **wget** (for downloads)
- **aws cli** (optional, for faster S3 downloads)
- **bash** 4.0+

### Hardware Recommendations

#### Minimum
- **CPU**: 4 cores
- **RAM**: 16GB
- **Disk**: 300GB free space
- **Time**: 12-24 hours

#### Recommended
- **CPU**: 16+ cores
- **RAM**: 64GB
- **Disk**: 500GB SSD
- **Time**: 6-8 hours

### Disk Space Breakdown

```
europe-latest.osm.pbf    ~30GB   (OSM data)
elevation/               ~100GB  (Terrain tiles)
tiles/                   ~150GB  (Valhalla tiles)
admin.sqlite             ~2GB    (Admin boundaries)
tz_world.sqlite          ~100MB  (Timezones)
------------------------------------------
Total:                   ~280GB
```

## Installation

### Install Valhalla (Ubuntu/Debian)

```bash
# Add PPA
sudo add-apt-repository ppa:valhalla-core/valhalla
sudo apt update

# Install Valhalla
sudo apt install valhalla-bin valhalla-dev
```

### Install Valhalla (macOS)

```bash
brew install valhalla
```

### Install Valhalla (from source)

```bash
git clone https://github.com/valhalla/valhalla.git
cd valhalla
git submodule update --init --recursive
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Install AWS CLI (optional, for faster downloads)

```bash
# Ubuntu/Debian
sudo apt install awscli

# macOS
brew install awscli

# Or use pip
pip install awscli
```

## Usage

### Basic Usage

```bash
./build_europe_tiles_with_elevation.sh
```

Output will be in `./europe_valhalla_tiles/`

### Custom Output Directory

```bash
./build_europe_tiles_with_elevation.sh /data/valhalla/europe
```

### Custom Thread Count

```bash
# Use 32 threads
VALHALLA_THREADS=32 ./build_europe_tiles_with_elevation.sh
```

### Resume Failed Build

The script is resumable. If it fails or is interrupted, simply run it again:

```bash
./build_europe_tiles_with_elevation.sh /data/valhalla/europe
```

It will skip already downloaded files.

## Output Structure

```
europe_valhalla_tiles/
├── europe-latest.osm.pbf       # OSM data
├── elevation/                  # Terrain tiles
│   ├── N35/
│   ├── N36/
│   └── ...
├── tiles/                      # Valhalla routing tiles
│   ├── 0/                      # Level 0 (local roads)
│   ├── 1/                      # Level 1 (arterial roads)
│   └── 2/                      # Level 2 (highways)
├── admin.sqlite                # Admin boundaries
├── tz_world.sqlite             # Timezone data
├── valhalla.json               # Configuration file
└── build_summary.txt           # Build report
```

## Using the Tiles

### With DuckDB valhalla_routing Extension

```sql
LOAD valhalla_routing;
SET valhalla_tiles = '/data/valhalla/europe';

-- Route from Paris to Berlin
SELECT
    round(r.distance_km, 1) as km,
    round(r.duration_minutes, 0) as minutes,
    ST_NPoints(r.geometry) as waypoints
FROM (
    SELECT travel_time_route(
        ST_Point(2.3522, 48.8566),   -- Paris
        ST_Point(13.4050, 52.5200),  -- Berlin
        'auto'
    ) as r
);
```

### With Valhalla HTTP Server

```bash
# Start Valhalla server
valhalla_service /data/valhalla/europe/valhalla.json 1

# Test with curl
curl http://localhost:8002/route \
  --data '{"locations":[{"lat":48.8566,"lon":2.3522},{"lat":52.5200,"lon":13.4050}],"costing":"auto"}' \
  | jq .
```

### With Valhalla Command Line

```bash
# Calculate route
valhalla_path_comparison \
  --config /data/valhalla/europe/valhalla.json \
  --from "48.8566,2.3522" \
  --to "52.5200,13.4050" \
  --type route
```

## Coverage

### Geographic Coverage

- **Region**: All of Europe
- **Latitude**: 35°N to 72°N
- **Longitude**: 25°W to 50°E

### Countries Included

All European countries including:
- Western Europe: France, Germany, UK, Spain, Italy, etc.
- Eastern Europe: Poland, Czech Republic, Hungary, etc.
- Northern Europe: Norway, Sweden, Finland, etc.
- Southern Europe: Greece, Portugal, etc.
- Balkans: Croatia, Serbia, Romania, etc.

### Routing Modes

- **Auto**: Car routing with traffic rules
- **Bicycle**: Bike-friendly routes with elevation consideration
- **Pedestrian**: Walking routes
- **Truck**: Commercial vehicle routing
- **Bus**: Public transit routing
- **Motorcycle**: Motorcycle-specific routing

## Data Sources

### OSM Data
- **Source**: Geofabrik
- **URL**: https://download.geofabrik.de/europe-latest.osm.pbf
- **Update**: Daily
- **License**: ODbL

### Elevation Data
- **Source**: AWS Terrain Tiles (Mapzen)
- **URL**: https://registry.opendata.aws/terrain-tiles/
- **Format**: SRTM HGT files (1 arc-second resolution)
- **Coverage**: Global
- **License**: Public Domain

## Performance Tips

### Faster Downloads

Use AWS CLI for elevation data (10x faster):
```bash
# Install AWS CLI first
./build_europe_tiles_with_elevation.sh
# Script will auto-detect and offer to use it
```

### More Threads

```bash
# Use all available cores
VALHALLA_THREADS=$(nproc) ./build_europe_tiles_with_elevation.sh
```

### SSD Storage

Using SSD instead of HDD can reduce build time by 50%:
```bash
./build_europe_tiles_with_elevation.sh /mnt/ssd/valhalla/europe
```

### Tmux/Screen

Run in background session to prevent interruption:
```bash
tmux new -s valhalla
./build_europe_tiles_with_elevation.sh
# Detach with Ctrl+B, D
# Re-attach: tmux attach -t valhalla
```

## Troubleshooting

### Out of Memory

If build fails with OOM:
```bash
# Reduce thread count
VALHALLA_THREADS=2 ./build_europe_tiles_with_elevation.sh
```

### Disk Space

Check available space before starting:
```bash
df -h /data/valhalla
```

Need at least 300GB free.

### Elevation Download Timeout

If elevation download is slow/fails:
```bash
# Install AWS CLI for faster downloads
sudo apt install awscli
./build_europe_tiles_with_elevation.sh
```

### Valhalla Not Found

```bash
# Check if Valhalla is installed
which valhalla_build_tiles

# If not found, install:
sudo apt install valhalla-bin
```

### Resume After Failure

Simply re-run the script. It will skip already downloaded files:
```bash
./build_europe_tiles_with_elevation.sh /data/valhalla/europe
```

## Updating Tiles

To update with latest OSM data:

```bash
# Remove old OSM file
rm /data/valhalla/europe/europe-latest.osm.pbf

# Re-run build (keeps elevation data)
./build_europe_tiles_with_elevation.sh /data/valhalla/europe
```

Elevation data doesn't change often, so it can be reused.

## Monitoring Progress

The script outputs progress logs:

```
[INFO] 2025-12-30 18:00:00 - Starting Europe Valhalla tiles build
[INFO] 2025-12-30 18:00:01 - Downloading europe-latest.osm.pbf
[INFO] 2025-12-30 18:30:00 - OSM file size: 29.8GB
[INFO] 2025-12-30 18:30:01 - Downloading elevation data...
[INFO] 2025-12-30 20:00:00 - Progress: 50% (1234 downloaded, 456 skipped)
[SUCCESS] 2025-12-30 22:00:00 - Elevation data downloaded: 2468 tiles
[INFO] 2025-12-30 22:00:01 - Building Valhalla tiles with 16 threads...
[SUCCESS] 2025-12-31 04:00:00 - Tiles built successfully in 6h 0m
```

## Example Build Timeline

For a **16-core server with SSD**:

```
00:00 - Start
00:30 - OSM download complete (30GB)
04:00 - Elevation download complete (100GB, AWS CLI)
04:10 - Config generation complete
10:00 - Tile building complete (150GB)
10:01 - Verification complete
10:02 - Build finished
------
Total: ~10 hours
```

For **4-core server with HDD**:

```
00:00 - Start
01:00 - OSM download complete
12:00 - Elevation download complete (HTTP)
12:10 - Config generation complete
24:00 - Tile building complete
------
Total: ~24 hours
```

## License

This script is provided as-is under MIT license.

Data licenses:
- OSM data: ODbL (https://www.openstreetmap.org/copyright)
- Elevation data: Public Domain
- Valhalla: MIT License

## Support

For issues with:

- **Script**: Create issue at <https://github.com/midwork-finds-jobs/duckdb-valhalla-routing>
- **Valhalla**: <https://github.com/valhalla/valhalla/issues>
- **OSM data**: <https://www.openstreetmap.org/>
- **Elevation data**: <https://registry.opendata.aws/terrain-tiles/>
