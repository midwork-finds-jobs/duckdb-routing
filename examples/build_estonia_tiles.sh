#!/usr/bin/env bash
#
# Build Valhalla routing tiles for Estonia
#
# Usage: ./examples/build_estonia_tiles.sh
#
# Prerequisites: brew install valhalla (macOS) or apt install valhalla-bin (Linux)

set -e

TILES_DIR="${1:-estonia_tiles}"

echo "==> Creating directory: $TILES_DIR"
mkdir -p "$TILES_DIR"

# Download Estonia OSM data if not present
if [ ! -f "$TILES_DIR/estonia.osm.pbf" ]; then
    echo "==> Downloading Estonia OSM data..."
    curl -L -o "$TILES_DIR/estonia.osm.pbf" \
        https://download.geofabrik.de/europe/estonia-latest.osm.pbf
else
    echo "==> Estonia OSM data already exists"
fi

# Generate Valhalla config
echo "==> Generating Valhalla config..."
valhalla_build_config \
    --mjolnir-tile-dir "$(pwd)/$TILES_DIR/tiles" \
    --mjolnir-tile-extract "" \
    --mjolnir-traffic-extract "" \
    --mjolnir-admin "" \
    --mjolnir-timezone "" \
    > "$TILES_DIR/valhalla.json"

# Build tiles
echo "==> Building routing tiles (this takes 2-5 minutes)..."
valhalla_build_tiles -c "$TILES_DIR/valhalla.json" "$TILES_DIR/estonia.osm.pbf"

echo ""
echo "==> Done! Tiles built in $TILES_DIR/tiles/"
echo ""
echo "To test, run:"
echo "  ./build/release/duckdb < examples/tallinn_to_tartu.sql"
