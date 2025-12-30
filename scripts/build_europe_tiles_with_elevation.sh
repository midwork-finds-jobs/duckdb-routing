#!/usr/bin/env bash
#
# Build Valhalla routing tiles for all of Europe with elevation data
# Uses OSM data from Geofabrik and elevation from AWS Open Data terrain tiles
#
# Requirements:
# - valhalla_build_config
# - valhalla_build_tiles
# - curl or wget
# - aws cli (optional, for faster S3 downloads)
# - ~50GB disk space for OSM data
# - ~100GB disk space for elevation data
# - ~150GB disk space for final tiles
#
# Usage:
#   ./build_europe_tiles_with_elevation.sh [output_dir]
#
# Example:
#   ./build_europe_tiles_with_elevation.sh /data/valhalla/europe
#

set -euo pipefail

# Configuration
OUTPUT_DIR="${1:-./europe_valhalla_tiles}"
OSM_URL="https://download.geofabrik.de/europe-latest.osm.pbf"
ELEVATION_URL="https://s3.amazonaws.com/elevation-tiles-prod/skadi"
NUM_THREADS="${VALHALLA_THREADS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*" >&2
}

# Check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Download with progress
download_file() {
    local url="$1"
    local output="$2"
    local temp_output="${output}.tmp"

    log_info "Downloading $url"

    if command_exists curl; then
        curl -L -C - --progress-bar "$url" -o "$temp_output"
    elif command_exists wget; then
        wget -c --progress=bar:force "$url" -O "$temp_output"
    else
        log_error "Neither curl nor wget found. Please install one."
        return 1
    fi

    mv "$temp_output" "$output"
    log_success "Downloaded: $output"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    local missing=()

    if ! command_exists valhalla_build_config; then
        missing+=("valhalla_build_config")
    fi

    if ! command_exists valhalla_build_tiles; then
        missing+=("valhalla_build_tiles")
    fi

    if ! command_exists curl && ! command_exists wget; then
        missing+=("curl or wget")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        log_error "Missing required commands: ${missing[*]}"
        log_error "Please install Valhalla and required tools"
        exit 1
    fi

    log_success "All prerequisites met"
}

# Download Europe OSM data
download_osm_data() {
    local osm_file="${OUTPUT_DIR}/europe-latest.osm.pbf"

    if [ -f "$osm_file" ]; then
        log_warn "OSM file already exists: $osm_file"
        log_info "File size: $(du -h "$osm_file" | cut -f1)"
        read -p "Re-download? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            log_info "Skipping OSM download"
            return 0
        fi
    fi

    mkdir -p "$OUTPUT_DIR"
    download_file "$OSM_URL" "$osm_file"

    log_info "OSM file size: $(du -h "$osm_file" | cut -f1)"
}

# Download elevation data for Europe
download_elevation_data() {
    local elevation_dir="${OUTPUT_DIR}/elevation"

    log_info "Downloading elevation data for Europe..."
    log_info "This will download ~100GB of data. It may take several hours."

    mkdir -p "$elevation_dir"

    # Europe coverage: roughly N35-N72, W-25-E50
    # Download all SRTM tiles for Europe
    # Format: /skadi/N{lat}/N{lat}E{lon}.hgt.gz or /skadi/N{lat}/N{lat}W{lon}.hgt.gz

    local total_tiles=0
    local downloaded_tiles=0
    local skipped_tiles=0

    log_info "Calculating tiles to download..."

    # Count total tiles
    for lat in $(seq 35 72); do
        for lon in $(seq -25 50); do
            total_tiles=$((total_tiles + 1))
        done
    done

    log_info "Total tiles to process: $total_tiles"

    for lat in $(seq 35 72); do
        local lat_dir="${elevation_dir}/N$(printf "%02d" $lat)"
        mkdir -p "$lat_dir"

        for lon in $(seq -25 50); do
            local lon_abs=$([ $lon -lt 0 ] && echo $((lon * -1)) || echo $lon)
            local lon_prefix=$([ $lon -lt 0 ] && echo "W" || echo "E")
            local tile_name="N$(printf "%02d" $lat)${lon_prefix}$(printf "%03d" $lon_abs).hgt.gz"
            local tile_path="${lat_dir}/${tile_name}"
            local url="${ELEVATION_URL}/N$(printf "%02d" $lat)/${tile_name}"

            if [ -f "${tile_path%.gz}" ]; then
                skipped_tiles=$((skipped_tiles + 1))
                continue
            fi

            # Try to download the tile (may not exist for all coordinates)
            if command_exists curl; then
                if curl -f -s -L "$url" -o "$tile_path" 2>/dev/null; then
                    gunzip -f "$tile_path" 2>/dev/null || true
                    downloaded_tiles=$((downloaded_tiles + 1))

                    # Progress update every 10 tiles
                    if [ $((downloaded_tiles % 10)) -eq 0 ]; then
                        local progress=$((100 * (downloaded_tiles + skipped_tiles) / total_tiles))
                        log_info "Progress: ${progress}% (${downloaded_tiles} downloaded, ${skipped_tiles} skipped)"
                    fi
                else
                    rm -f "$tile_path" 2>/dev/null || true
                fi
            fi
        done
    done

    log_success "Elevation data downloaded: $downloaded_tiles tiles"
    log_info "Elevation directory size: $(du -sh "$elevation_dir" | cut -f1)"
}

# Alternative: Download elevation using AWS CLI (faster)
download_elevation_aws() {
    local elevation_dir="${OUTPUT_DIR}/elevation"

    if ! command_exists aws; then
        log_warn "AWS CLI not found. Falling back to HTTP download."
        download_elevation_data
        return
    fi

    log_info "Downloading elevation data using AWS CLI..."
    mkdir -p "$elevation_dir"

    # Download with AWS CLI (no authentication needed for public data)
    for lat in $(seq 35 72); do
        local lat_dir="N$(printf "%02d" $lat)"
        log_info "Downloading elevation for latitude $lat..."

        aws s3 sync \
            --no-sign-request \
            --exclude "*" \
            --include "*.hgt.gz" \
            "s3://elevation-tiles-prod/skadi/${lat_dir}/" \
            "${elevation_dir}/${lat_dir}/" \
            2>/dev/null || true

        # Decompress
        find "${elevation_dir}/${lat_dir}" -name "*.hgt.gz" -exec gunzip -f {} \; 2>/dev/null || true
    done

    log_success "Elevation data downloaded via AWS CLI"
    log_info "Elevation directory size: $(du -sh "$elevation_dir" | cut -f1)"
}

# Generate Valhalla configuration
generate_valhalla_config() {
    local config_file="${OUTPUT_DIR}/valhalla.json"
    local tiles_dir="${OUTPUT_DIR}/tiles"
    local elevation_dir="${OUTPUT_DIR}/elevation"

    log_info "Generating Valhalla configuration..."

    mkdir -p "$tiles_dir"

    # Generate base config
    valhalla_build_config \
        --mjolnir-tile-dir "$tiles_dir" \
        --mjolnir-tile-extract "${tiles_dir}/tiles.tar" \
        --mjolnir-admin "${OUTPUT_DIR}/admin.sqlite" \
        --mjolnir-timezone "${OUTPUT_DIR}/tz_world.sqlite" \
        --additional-data-elevation "$elevation_dir" \
        > "$config_file"

    log_success "Configuration generated: $config_file"
}

# Build Valhalla tiles
build_valhalla_tiles() {
    local config_file="${OUTPUT_DIR}/valhalla.json"
    local osm_file="${OUTPUT_DIR}/europe-latest.osm.pbf"

    log_info "Building Valhalla tiles with $NUM_THREADS threads..."
    log_info "This will take several hours (possibly 6-12 hours for all of Europe)"

    local start_time=$(date +%s)

    # Build tiles
    valhalla_build_tiles \
        -c "$config_file" \
        -j "$NUM_THREADS" \
        "$osm_file"

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local hours=$((duration / 3600))
    local minutes=$(( (duration % 3600) / 60))

    log_success "Tiles built successfully in ${hours}h ${minutes}m"
    log_info "Tiles directory size: $(du -sh "${OUTPUT_DIR}/tiles" | cut -f1)"
}

# Verify tiles
verify_tiles() {
    local tiles_dir="${OUTPUT_DIR}/tiles"

    log_info "Verifying tiles..."

    local tile_count=$(find "$tiles_dir" -name "*.gph" 2>/dev/null | wc -l)

    if [ "$tile_count" -eq 0 ]; then
        log_error "No tiles found in $tiles_dir"
        return 1
    fi

    log_success "Found $tile_count tile files"

    # Check for expected subdirectories (0, 1, 2 for hierarchy levels)
    for level in 0 1 2; do
        if [ -d "${tiles_dir}/${level}" ]; then
            local level_tiles=$(find "${tiles_dir}/${level}" -name "*.gph" 2>/dev/null | wc -l)
            log_info "Level $level: $level_tiles tiles"
        fi
    done
}

# Create summary
create_summary() {
    local summary_file="${OUTPUT_DIR}/build_summary.txt"

    log_info "Creating build summary..."

    cat > "$summary_file" << EOF
Valhalla Europe Tiles - Build Summary
Generated: $(date)
=====================================

Output Directory: $OUTPUT_DIR

File Sizes:
-----------
OSM Data:       $(du -sh "${OUTPUT_DIR}/europe-latest.osm.pbf" 2>/dev/null | cut -f1 || echo "N/A")
Elevation Data: $(du -sh "${OUTPUT_DIR}/elevation" 2>/dev/null | cut -f1 || echo "N/A")
Tiles:          $(du -sh "${OUTPUT_DIR}/tiles" 2>/dev/null | cut -f1 || echo "N/A")
Total:          $(du -sh "$OUTPUT_DIR" 2>/dev/null | cut -f1 || echo "N/A")

Tile Counts:
------------
$(find "${OUTPUT_DIR}/tiles" -name "*.gph" 2>/dev/null | wc -l) total tile files

Configuration:
--------------
Config file: ${OUTPUT_DIR}/valhalla.json
Threads used: $NUM_THREADS

Usage:
------
To use these tiles with DuckDB valhalla_routing extension:

    LOAD valhalla_routing;
    SET valhalla_tiles = '${OUTPUT_DIR}';

    SELECT travel_time_route(
        ST_Point(2.3522, 48.8566),  -- Paris
        ST_Point(13.4050, 52.5200),  -- Berlin
        'auto'
    );

To use with Valhalla server:

    valhalla_service ${OUTPUT_DIR}/valhalla.json 1

Notes:
------
- Tiles include elevation data for accurate routing
- Coverage: All of Europe
- Data sources:
  - OSM: Geofabrik (https://download.geofabrik.de/)
  - Elevation: AWS Terrain Tiles (https://registry.opendata.aws/terrain-tiles/)
EOF

    cat "$summary_file"
    log_success "Summary written to: $summary_file"
}

# Cleanup on error
cleanup_on_error() {
    log_error "Build failed. Partial files may exist in: $OUTPUT_DIR"
    log_info "You can resume by running this script again."
    exit 1
}

# Main execution
main() {
    trap cleanup_on_error ERR

    log_info "Starting Europe Valhalla tiles build"
    log_info "Output directory: $OUTPUT_DIR"
    log_info "Threads: $NUM_THREADS"
    echo

    # Create output directory
    mkdir -p "$OUTPUT_DIR"

    # Run build steps
    check_prerequisites
    echo

    download_osm_data
    echo

    # Choose elevation download method
    if command_exists aws; then
        log_info "AWS CLI detected. Using faster S3 sync method."
        read -p "Use AWS CLI for elevation download? (Y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Nn]$ ]]; then
            download_elevation_data
        else
            download_elevation_aws
        fi
    else
        download_elevation_data
    fi
    echo

    generate_valhalla_config
    echo

    build_valhalla_tiles
    echo

    verify_tiles
    echo

    create_summary
    echo

    log_success "Build complete!"
    log_success "Tiles are ready in: ${OUTPUT_DIR}/tiles"
    log_success "Configuration: ${OUTPUT_DIR}/valhalla.json"
    echo
    log_info "To use with DuckDB:"
    echo "  LOAD valhalla_routing;"
    echo "  SET valhalla_tiles = '${OUTPUT_DIR}';"
}

# Run main
main "$@"
