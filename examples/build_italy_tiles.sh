#!/usr/bin/env bash
#
# Build Valhalla routing tiles for Italy
#
# Usage: ./examples/build_italy_tiles.sh [output_directory]
#
# Prerequisites:
#   - macOS: brew install valhalla
#   - Linux: apt install valhalla-bin
#
# Output:
#   - italy_tiles/italy.osm.pbf (2.1 GB)
#   - italy_tiles/valhalla.json (config)
#   - italy_tiles/tiles/ (2.4 GB routing tiles)
#
# Build time: ~7 minutes
# Disk space: ~5 GB total

set -e

TILES_DIR="${1:-italy_tiles}"

echo "========================================"
echo "Building Italy Routing Tiles"
echo "========================================"
echo ""
echo "Output directory: $TILES_DIR"
echo "Build time: ~7 minutes"
echo "Disk space needed: ~5 GB"
echo ""

# Create directory
echo "==> Creating directory: $TILES_DIR"
mkdir -p "$TILES_DIR"

# Download Italy OSM data if not present
if [ ! -f "$TILES_DIR/italy.osm.pbf" ]; then
    echo "==> Downloading Italy OSM data (~2.1 GB)..."
    echo "    Source: https://download.geofabrik.de/europe/italy-latest.osm.pbf"
    curl -L --progress-bar -o "$TILES_DIR/italy.osm.pbf" \
        https://download.geofabrik.de/europe/italy-latest.osm.pbf

    FILE_SIZE=$(du -h "$TILES_DIR/italy.osm.pbf" | cut -f1)
    echo "    Downloaded: $FILE_SIZE"
else
    echo "==> Italy OSM data already exists, skipping download"
    FILE_SIZE=$(du -h "$TILES_DIR/italy.osm.pbf" | cut -f1)
    echo "    Size: $FILE_SIZE"
fi

# Generate Valhalla config
echo ""
echo "==> Generating Valhalla configuration..."
valhalla_build_config \
    --mjolnir-tile-dir "$(pwd)/$TILES_DIR/tiles" \
    --mjolnir-tile-extract "" \
    --mjolnir-traffic-extract "" \
    --mjolnir-admin "" \
    --mjolnir-timezone "" \
    > "$TILES_DIR/valhalla.json"

echo "    Created: $TILES_DIR/valhalla.json"

# Build tiles
echo ""
echo "==> Building routing tiles (this takes ~7 minutes)..."
echo "    Progress indicators:"
echo "    - Level 0: Local roads"
echo "    - Level 1: Arterial roads"
echo "    - Level 2: Highways"
echo ""

valhalla_build_tiles -c "$TILES_DIR/valhalla.json" "$TILES_DIR/italy.osm.pbf" 2>&1 | \
    grep -E "(Level|tiles|nodes|edges)" || true

# Verify tiles were created
if [ ! -d "$TILES_DIR/tiles" ]; then
    echo ""
    echo "ERROR: Tile directory not created!"
    exit 1
fi

TILE_SIZE=$(du -sh "$TILES_DIR/tiles" | cut -f1)
TOTAL_SIZE=$(du -sh "$TILES_DIR" | cut -f1)

echo ""
echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""
echo "Routing tiles: $TILES_DIR/tiles/ ($TILE_SIZE)"
echo "Total size:    $TOTAL_SIZE"
echo ""
echo "Tile hierarchy:"
ls -d "$TILES_DIR/tiles"/* 2>/dev/null | while read level; do
    LEVEL_SIZE=$(du -sh "$level" | cut -f1)
    LEVEL_NAME=$(basename "$level")
    echo "  - Level $LEVEL_NAME: $LEVEL_SIZE"
done
echo ""
echo "Next steps:"
echo "  1. Test the extension:"
echo "     duckdb < examples/italy_route_examples.sql"
echo ""
echo "  2. Or run manually:"
echo "     duckdb -c \"LOAD travel_time; SELECT travel_time_load_config('./$TILES_DIR/valhalla.json');\""
echo ""
echo "Documentation: See ITALY.md for usage examples"
