use anyhow::{Context, Result};
use fast_paths::{FastGraph, InputGraph, PathCalculator};
use geo::algorithm::centroid::Centroid;
use geo::prelude::*;
use geo::{Geometry, Point};
use osmpbfreader::{OsmObj, OsmPbfReader};
use rayon::prelude::*;
use rstar::{PointDistance, RTree, RTreeObject, AABB};
use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};
use std::ffi::CStr;
use std::fs::File;
use std::io::{BufReader, BufWriter};
use std::os::raw::c_char;
use std::path::Path;
use std::sync::Mutex;
use wkt::TryFromWkt;
use geozero::wkb::Wkb;
use geozero::ToGeo;

// Dijkstra priority queue state
#[derive(Clone, Eq, PartialEq)]
struct DijkstraState {
    cost: u32,  // milliseconds
    node: usize,
}

impl Ord for DijkstraState {
    fn cmp(&self, other: &Self) -> Ordering {
        // Min-heap: reverse ordering
        other.cost.cmp(&self.cost)
    }
}

impl PartialOrd for DijkstraState {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

// Speed in km/h for different transport modes and road types
fn get_speed_kmh(highway_type: &str, mode: &str) -> Option<f64> {
    match mode {
        "auto" => match highway_type {
            "motorway" => Some(120.0),
            "motorway_link" => Some(80.0),
            "trunk" => Some(100.0),
            "trunk_link" => Some(60.0),
            "primary" => Some(80.0),
            "primary_link" => Some(50.0),
            "secondary" => Some(60.0),
            "secondary_link" => Some(40.0),
            "tertiary" => Some(50.0),
            "tertiary_link" => Some(30.0),
            "residential" => Some(30.0),
            "living_street" => Some(20.0),
            "service" => Some(20.0),
            "unclassified" => Some(40.0),
            _ => None,
        },
        "bicycle" => match highway_type {
            "cycleway" => Some(20.0),
            "path" => Some(15.0),
            "track" => Some(12.0),
            "bridleway" => Some(10.0),
            "residential" => Some(18.0),
            "living_street" => Some(15.0),
            "service" => Some(15.0),
            "tertiary" | "tertiary_link" => Some(20.0),
            "secondary" | "secondary_link" => Some(18.0),
            "primary" | "primary_link" => Some(15.0),
            "unclassified" => Some(18.0),
            "trunk" | "trunk_link" => Some(12.0),
            "motorway" | "motorway_link" => Some(5.0),
            "footway" => Some(10.0),
            "pedestrian" => Some(8.0),
            "steps" => Some(3.0),
            _ => None,
        },
        "pedestrian" => match highway_type {
            "footway" => Some(5.0),
            "path" => Some(4.5),
            "pedestrian" => Some(5.0),
            "steps" => Some(3.0),
            "track" | "bridleway" => Some(4.0),
            "residential" | "living_street" | "service" | "cycleway" => Some(5.0),
            "tertiary" | "tertiary_link" => Some(5.0),
            "secondary" | "secondary_link" => Some(5.0),
            "primary" | "primary_link" => Some(5.0),
            "unclassified" => Some(5.0),
            "trunk" | "trunk_link" => Some(5.0),
            "motorway" | "motorway_link" => Some(3.0),
            _ => None,
        },
        _ => None,
    }
}

fn is_main_road(highway_type: &str) -> bool {
    matches!(
        highway_type,
        "motorway"
            | "motorway_link"
            | "trunk"
            | "trunk_link"
            | "primary"
            | "primary_link"
            | "secondary"
            | "secondary_link"
            | "tertiary"
            | "tertiary_link"
            | "residential"
            | "living_street"
            | "service"
            | "unclassified"
    )
}

// R-tree point with node index
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
struct IndexedPoint {
    lon: f64,
    lat: f64,
    idx: usize,
}

impl RTreeObject for IndexedPoint {
    type Envelope = AABB<[f64; 2]>;

    fn envelope(&self) -> Self::Envelope {
        AABB::from_point([self.lon, self.lat])
    }
}

impl PointDistance for IndexedPoint {
    fn distance_2(&self, point: &[f64; 2]) -> f64 {
        let dx = self.lon - point[0];
        let dy = self.lat - point[1];
        dx * dx + dy * dy
    }
}

// Adjacency list entry: (to_node, weight_ms)
type AdjList = Vec<Vec<(usize, u32)>>;

#[derive(Serialize, Deserialize)]
struct RoutingData {
    node_positions: Vec<(f64, f64)>,
    fast_graph: FastGraph,
    spatial_index: RTree<IndexedPoint>,
    adj_list: AdjList,  // For Dijkstra-based isochrone
}

struct Router {
    data: RoutingData,
    calculator: PathCalculator,
}

static ROUTER_AUTO: Mutex<Option<Router>> = Mutex::new(None);
static ROUTER_BICYCLE: Mutex<Option<Router>> = Mutex::new(None);
static ROUTER_PEDESTRIAN: Mutex<Option<Router>> = Mutex::new(None);

fn cache_path(pbf_path: &str, mode: &str) -> String {
    format!("{}.{}.routing", pbf_path, mode)
}

fn build_graph_for_mode(pbf_path: &str, mode: &str) -> Result<RoutingData> {
    let file = File::open(pbf_path).context("Could not open PBF file")?;
    let mut pbf = OsmPbfReader::new(file);

    let objs = pbf.get_objs_and_deps(|obj| {
        obj.is_node() || (obj.is_way() && obj.tags().contains_key("highway"))
    })?;

    let mut osm_nodes: HashMap<i64, (f64, f64)> = HashMap::new();
    for obj in objs.values() {
        if let OsmObj::Node(n) = obj {
            osm_nodes.insert(n.id.0, (n.lon(), n.lat()));
        }
    }

    let mut edges: Vec<(i64, i64, u32)> = Vec::new();
    let mut used_nodes: std::collections::HashSet<i64> = std::collections::HashSet::new();
    let mut main_road_node_ids: std::collections::HashSet<i64> = std::collections::HashSet::new();

    for obj in objs.values() {
        if let OsmObj::Way(w) = obj {
            let highway = w.tags.get("highway").map(|s| s.as_str()).unwrap_or("");
            let is_main = is_main_road(highway);

            if let Some(speed_kmh) = get_speed_kmh(highway, mode) {
                let oneway = w.tags.get("oneway").map(|s| s.as_str()) == Some("yes");

                for window in w.nodes.windows(2) {
                    let from_id = window[0].0;
                    let to_id = window[1].0;

                    if let (Some(&(lon1, lat1)), Some(&(lon2, lat2))) =
                        (osm_nodes.get(&from_id), osm_nodes.get(&to_id))
                    {
                        let p1 = Point::new(lon1, lat1);
                        let p2 = Point::new(lon2, lat2);
                        let dist_m = p1.haversine_distance(&p2);
                        let time_ms = ((dist_m / 1000.0 / speed_kmh) * 3600.0 * 1000.0) as u32;

                        if time_ms > 0 {
                            edges.push((from_id, to_id, time_ms));
                            used_nodes.insert(from_id);
                            used_nodes.insert(to_id);
                            if is_main {
                                main_road_node_ids.insert(from_id);
                                main_road_node_ids.insert(to_id);
                            }
                            if !oneway {
                                edges.push((to_id, from_id, time_ms));
                            }
                        }
                    }
                }
            }
        }
    }

    let mut node_id_to_index: HashMap<i64, usize> = HashMap::new();
    let mut node_positions: Vec<(f64, f64)> = Vec::new();
    let mut rtree_points: Vec<IndexedPoint> = Vec::new();

    for &node_id in &used_nodes {
        if let Some(&pos) = osm_nodes.get(&node_id) {
            let index = node_positions.len();
            node_id_to_index.insert(node_id, index);
            node_positions.push(pos);
            // Only index main road nodes for reliable connectivity
            if main_road_node_ids.contains(&node_id) {
                rtree_points.push(IndexedPoint {
                    lon: pos.0,
                    lat: pos.1,
                    idx: index,
                });
            }
        }
    }

    // Build adjacency list and input graph
    let num_nodes = node_positions.len();
    let mut adj_list: AdjList = vec![Vec::new(); num_nodes];
    let mut input_graph = InputGraph::new();

    for (from_id, to_id, weight) in edges {
        if let (Some(&from_idx), Some(&to_idx)) =
            (node_id_to_index.get(&from_id), node_id_to_index.get(&to_id))
        {
            input_graph.add_edge(from_idx, to_idx, weight as usize);
            adj_list[from_idx].push((to_idx, weight));
        }
    }
    input_graph.freeze();

    let fast_graph = fast_paths::prepare(&input_graph);
    let spatial_index = RTree::bulk_load(rtree_points);

    Ok(RoutingData {
        node_positions,
        fast_graph,
        spatial_index,
        adj_list,
    })
}

fn save_graph(data: &RoutingData, path: &str) -> Result<()> {
    let file = File::create(path)?;
    let writer = BufWriter::new(file);
    bincode::serialize_into(writer, data)?;
    Ok(())
}

fn load_graph(path: &str) -> Result<RoutingData> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let data: RoutingData = bincode::deserialize_from(reader)?;
    Ok(data)
}

fn find_nearest_node(data: &RoutingData, lon: f64, lat: f64) -> Option<usize> {
    data.spatial_index
        .nearest_neighbor(&[lon, lat])
        .map(|p| p.idx)
}

fn get_router_for_mode(mode: &str) -> &'static Mutex<Option<Router>> {
    match mode {
        "bicycle" => &ROUTER_BICYCLE,
        "pedestrian" => &ROUTER_PEDESTRIAN,
        _ => &ROUTER_AUTO,
    }
}

/// Parse WKT geometry and return centroid as (lon, lat)
/// For POINT, returns the point itself
/// For other geometries, returns the centroid
fn wkt_to_centroid(wkt_str: &str) -> Option<(f64, f64)> {
    let geom: Geometry<f64> = Geometry::try_from_wkt_str(wkt_str).ok()?;
    geometry_to_centroid(&geom)
}

/// Parse WKB geometry and return centroid as (lon, lat)
fn wkb_to_centroid(wkb: &[u8]) -> Option<(f64, f64)> {
    let wkb = Wkb(wkb.to_vec());
    let geom: Geometry<f64> = wkb.to_geo().ok()?;
    geometry_to_centroid(&geom)
}

/// Extract centroid from a geo::Geometry
fn geometry_to_centroid(geom: &Geometry<f64>) -> Option<(f64, f64)> {
    match geom {
        Geometry::Point(p) => Some((p.x(), p.y())),
        Geometry::Line(l) => {
            let c = l.centroid();
            Some((c.x(), c.y()))
        }
        Geometry::LineString(ls) => ls.centroid().map(|c| (c.x(), c.y())),
        Geometry::Polygon(p) => p.centroid().map(|c| (c.x(), c.y())),
        Geometry::MultiPoint(mp) => mp.centroid().map(|c| (c.x(), c.y())),
        Geometry::MultiLineString(mls) => mls.centroid().map(|c| (c.x(), c.y())),
        Geometry::MultiPolygon(mp) => mp.centroid().map(|c| (c.x(), c.y())),
        Geometry::GeometryCollection(gc) => gc.centroid().map(|c| (c.x(), c.y())),
        Geometry::Rect(r) => {
            let c = r.centroid();
            Some((c.x(), c.y()))
        }
        Geometry::Triangle(t) => {
            let c = t.centroid();
            Some((c.x(), c.y()))
        }
    }
}

// ============ C FFI ============

/// Load routing data - uses cache if available, builds and caches otherwise
#[no_mangle]
pub extern "C" fn routing_load(pbf_path: *const c_char, mode: *const c_char) -> i32 {
    let pbf_path = match unsafe { CStr::from_ptr(pbf_path) }.to_str() {
        Ok(s) if !pbf_path.is_null() => s,
        _ => return -1,
    };
    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let cache = cache_path(pbf_path, mode);
    let data = if Path::new(&cache).exists() {
        match load_graph(&cache) {
            Ok(d) => d,
            Err(_) => match build_graph_for_mode(pbf_path, mode) {
                Ok(d) => {
                    let _ = save_graph(&d, &cache);
                    d
                }
                Err(_) => return -1,
            },
        }
    } else {
        match build_graph_for_mode(pbf_path, mode) {
            Ok(d) => {
                let _ = save_graph(&d, &cache);
                d
            }
            Err(_) => return -1,
        }
    };

    let calculator = fast_paths::create_calculator(&data.fast_graph);
    let router = Router { data, calculator };

    if let Ok(mut guard) = get_router_for_mode(mode).lock() {
        *guard = Some(router);
        0
    } else {
        -1
    }
}

/// Calculate travel time in seconds between two points
#[no_mangle]
pub extern "C" fn routing_travel_time(
    lat1: f64,
    lon1: f64,
    lat2: f64,
    lon2: f64,
    mode: *const c_char,
) -> f64 {
    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1.0,
    };

    let mutex = get_router_for_mode(mode);
    let mut guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1.0,
    };

    let router = match guard.as_mut() {
        Some(r) => r,
        None => return -2.0,
    };

    let from_idx = match find_nearest_node(&router.data, lon1, lat1) {
        Some(idx) => idx,
        None => return -1.0,
    };

    let to_idx = match find_nearest_node(&router.data, lon2, lat2) {
        Some(idx) => idx,
        None => return -1.0,
    };

    match router
        .calculator
        .calc_path(&router.data.fast_graph, from_idx, to_idx)
    {
        Some(path) => path.get_weight() as f64 / 1000.0,
        None => -1.0,
    }
}

/// Check if routing data is loaded
#[no_mangle]
pub extern "C" fn routing_is_loaded(mode: *const c_char) -> i32 {
    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return 0,
    };

    match get_router_for_mode(mode).lock() {
        Ok(guard) => i32::from(guard.is_some()),
        Err(_) => 0,
    }
}

/// Free routing data
#[no_mangle]
pub extern "C" fn routing_free(mode: *const c_char) {
    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return,
    };

    if let Ok(mut guard) = get_router_for_mode(mode).lock() {
        *guard = None;
    }
}

/// Batch calculate travel times between pairs of points (parallel)
/// results array must have space for `count` doubles
/// Returns number of successful calculations, or -1 on error
#[no_mangle]
pub extern "C" fn routing_batch(
    lats1: *const f64,
    lons1: *const f64,
    lats2: *const f64,
    lons2: *const f64,
    results: *mut f64,
    count: i32,
    mode: *const c_char,
) -> i32 {
    if lats1.is_null() || lons1.is_null() || lats2.is_null() || lons2.is_null() || results.is_null()
    {
        return -1;
    }

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_ref() {
        Some(r) => r,
        None => return -2,
    };

    let count = count as usize;
    let lats1 = unsafe { std::slice::from_raw_parts(lats1, count) };
    let lons1 = unsafe { std::slice::from_raw_parts(lons1, count) };
    let lats2 = unsafe { std::slice::from_raw_parts(lats2, count) };
    let lons2 = unsafe { std::slice::from_raw_parts(lons2, count) };
    let results = unsafe { std::slice::from_raw_parts_mut(results, count) };

    // Use thread-local calculators for parallel processing
    use std::cell::RefCell;
    thread_local! {
        static CALC: RefCell<Option<PathCalculator>> = const { RefCell::new(None) };
    }

    // Parallel calculation using rayon
    let success_count: i32 = (0..count)
        .into_par_iter()
        .map(|i| {
            let from_idx = find_nearest_node(&router.data, lons1[i], lats1[i]);
            let to_idx = find_nearest_node(&router.data, lons2[i], lats2[i]);

            let result = match (from_idx, to_idx) {
                (Some(from), Some(to)) => {
                    CALC.with(|calc_cell| {
                        let mut calc_ref = calc_cell.borrow_mut();
                        if calc_ref.is_none() {
                            *calc_ref = Some(fast_paths::create_calculator(&router.data.fast_graph));
                        }
                        match calc_ref.as_mut().unwrap().calc_path(&router.data.fast_graph, from, to) {
                            Some(path) => (path.get_weight() as f64 / 1000.0, 1),
                            None => (-1.0, 0),
                        }
                    })
                }
                _ => (-1.0, 0),
            };

            // SAFETY: each thread writes to a unique index
            unsafe {
                *results.as_ptr().add(i).cast_mut() = result.0;
            }
            result.1
        })
        .sum();

    success_count
}

/// Snap a coordinate to the nearest road network node
/// Returns snapped lat/lon and distance in meters, or -1 values on error
#[no_mangle]
pub extern "C" fn routing_snap(
    lat: f64,
    lon: f64,
    mode: *const c_char,
    out_lat: *mut f64,
    out_lon: *mut f64,
    out_distance_m: *mut f64,
) -> i32 {
    if out_lat.is_null() || out_lon.is_null() || out_distance_m.is_null() {
        return -1;
    }

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_ref() {
        Some(r) => r,
        None => return -2,
    };

    match router.data.spatial_index.nearest_neighbor(&[lon, lat]) {
        Some(point) => {
            let (node_lon, node_lat) = router.data.node_positions[point.idx];
            let p1 = Point::new(lon, lat);
            let p2 = Point::new(node_lon, node_lat);
            let dist = p1.haversine_distance(&p2);

            unsafe {
                *out_lat = node_lat;
                *out_lon = node_lon;
                *out_distance_m = dist;
            }
            0
        }
        None => {
            unsafe {
                *out_lat = -1.0;
                *out_lon = -1.0;
                *out_distance_m = -1.0;
            }
            -1
        }
    }
}

/// Get count of nodes in the routing graph
#[no_mangle]
pub extern "C" fn routing_node_count(mode: *const c_char) -> i32 {
    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let mutex = get_router_for_mode(mode);
    match mutex.lock() {
        Ok(guard) => match guard.as_ref() {
            Some(r) => r.data.node_positions.len() as i32,
            None => -2,
        },
        Err(_) => -1,
    }
}

/// Isochrone result struct for FFI
#[repr(C)]
pub struct IsochroneResult {
    pub lat: f64,
    pub lon: f64,
    pub seconds: f64,
}

/// Route point struct for FFI
#[repr(C)]
pub struct RoutePoint {
    pub lat: f64,
    pub lon: f64,
}

/// Route result struct for FFI
#[repr(C)]
pub struct RouteResult {
    pub distance_m: f64,
    pub duration_s: f64,
    pub num_points: i32,
}

/// Calculate isochrone - all reachable points within max_seconds
/// Returns count of results written, or -1 on error
/// Results are written to out_results array (caller provides buffer)
#[no_mangle]
pub extern "C" fn routing_isochrone(
    lat: f64,
    lon: f64,
    max_seconds: f64,
    mode: *const c_char,
    out_results: *mut IsochroneResult,
    max_results: i32,
) -> i32 {
    if out_results.is_null() || max_results <= 0 {
        return -1;
    }

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_ref() {
        Some(r) => r,
        None => return -2,
    };

    // Find starting node
    let start_idx = match find_nearest_node(&router.data, lon, lat) {
        Some(idx) => idx,
        None => return -1,
    };

    let max_cost_ms = (max_seconds * 1000.0) as u32;
    let num_nodes = router.data.node_positions.len();

    // Dijkstra with early termination
    let mut dist: Vec<u32> = vec![u32::MAX; num_nodes];
    let mut heap = BinaryHeap::new();

    dist[start_idx] = 0;
    heap.push(DijkstraState { cost: 0, node: start_idx });

    let mut result_count = 0i32;
    let max_results = max_results as usize;
    let out_results = unsafe { std::slice::from_raw_parts_mut(out_results, max_results) };

    while let Some(DijkstraState { cost, node }) = heap.pop() {
        // Skip if we've already found a better path
        if cost > dist[node] {
            continue;
        }

        // Stop if beyond time limit
        if cost > max_cost_ms {
            continue;
        }

        // Record this reachable node
        if (result_count as usize) < max_results {
            let (node_lon, node_lat) = router.data.node_positions[node];
            out_results[result_count as usize] = IsochroneResult {
                lat: node_lat,
                lon: node_lon,
                seconds: cost as f64 / 1000.0,
            };
            result_count += 1;
        }

        // Explore neighbors
        for &(next_node, edge_cost) in &router.data.adj_list[node] {
            let next_cost = cost.saturating_add(edge_cost);
            if next_cost <= max_cost_ms && next_cost < dist[next_node] {
                dist[next_node] = next_cost;
                heap.push(DijkstraState { cost: next_cost, node: next_node });
            }
        }
    }

    result_count
}

/// Calculate route with full geometry
/// Returns number of path points written, or -1 on error, -2 if not loaded
#[no_mangle]
pub extern "C" fn routing_route(
    lat1: f64,
    lon1: f64,
    lat2: f64,
    lon2: f64,
    mode: *const c_char,
    out_result: *mut RouteResult,
    out_points: *mut RoutePoint,
    max_points: i32,
) -> i32 {
    if out_result.is_null() || out_points.is_null() || max_points <= 0 {
        return -1;
    }

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let mut guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_mut() {
        Some(r) => r,
        None => return -2,
    };

    // Find nearest nodes
    let from_idx = match find_nearest_node(&router.data, lon1, lat1) {
        Some(idx) => idx,
        None => return -1,
    };

    let to_idx = match find_nearest_node(&router.data, lon2, lat2) {
        Some(idx) => idx,
        None => return -1,
    };

    // Calculate path
    let path = match router
        .calculator
        .calc_path(&router.data.fast_graph, from_idx, to_idx)
    {
        Some(p) => p,
        None => return -1,
    };

    let path_nodes = path.get_nodes();
    let duration_s = path.get_weight() as f64 / 1000.0;

    // Calculate actual road distance and collect points
    let mut total_distance_m = 0.0;
    let out_points = unsafe { std::slice::from_raw_parts_mut(out_points, max_points as usize) };
    let num_points = path_nodes.len().min(max_points as usize);

    for i in 0..num_points {
        let node_idx = path_nodes[i];
        let (lon, lat) = router.data.node_positions[node_idx];
        out_points[i] = RoutePoint { lat, lon };

        // Calculate distance between consecutive points
        if i > 0 {
            let prev_idx = path_nodes[i - 1];
            let (prev_lon, prev_lat) = router.data.node_positions[prev_idx];
            let p1 = Point::new(prev_lon, prev_lat);
            let p2 = Point::new(lon, lat);
            total_distance_m += p1.haversine_distance(&p2);
        }
    }

    // Write result
    unsafe {
        *out_result = RouteResult {
            distance_m: total_distance_m,
            duration_s,
            num_points: num_points as i32,
        };
    }

    num_points as i32
}

/// Calculate route with full geometry using WKT geometries as input
/// Uses centroid of each geometry as routing point
/// Returns number of path points written, or -1 on error, -2 if not loaded
#[no_mangle]
pub extern "C" fn routing_route_geom(
    from_wkt: *const c_char,
    to_wkt: *const c_char,
    mode: *const c_char,
    out_result: *mut RouteResult,
    out_points: *mut RoutePoint,
    max_points: i32,
) -> i32 {
    if out_result.is_null() || out_points.is_null() || max_points <= 0 {
        return -1;
    }

    let from_wkt = match unsafe { CStr::from_ptr(from_wkt) }.to_str() {
        Ok(s) if !from_wkt.is_null() => s,
        _ => return -1,
    };

    let to_wkt = match unsafe { CStr::from_ptr(to_wkt) }.to_str() {
        Ok(s) if !to_wkt.is_null() => s,
        _ => return -1,
    };

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    // Parse WKT and get centroids
    let (lon1, lat1) = match wkt_to_centroid(from_wkt) {
        Some(c) => c,
        None => return -1,
    };

    let (lon2, lat2) = match wkt_to_centroid(to_wkt) {
        Some(c) => c,
        None => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let mut guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_mut() {
        Some(r) => r,
        None => return -2,
    };

    // Find nearest nodes
    let from_idx = match find_nearest_node(&router.data, lon1, lat1) {
        Some(idx) => idx,
        None => return -1,
    };

    let to_idx = match find_nearest_node(&router.data, lon2, lat2) {
        Some(idx) => idx,
        None => return -1,
    };

    // Calculate path
    let path = match router
        .calculator
        .calc_path(&router.data.fast_graph, from_idx, to_idx)
    {
        Some(p) => p,
        None => return -1,
    };

    let path_nodes = path.get_nodes();
    let duration_s = path.get_weight() as f64 / 1000.0;

    // Calculate actual road distance and collect points
    let mut total_distance_m = 0.0;
    let out_points = unsafe { std::slice::from_raw_parts_mut(out_points, max_points as usize) };
    let num_points = path_nodes.len().min(max_points as usize);

    for i in 0..num_points {
        let node_idx = path_nodes[i];
        let (lon, lat) = router.data.node_positions[node_idx];
        out_points[i] = RoutePoint { lat, lon };

        if i > 0 {
            let prev_idx = path_nodes[i - 1];
            let (prev_lon, prev_lat) = router.data.node_positions[prev_idx];
            let p1 = Point::new(prev_lon, prev_lat);
            let p2 = Point::new(lon, lat);
            total_distance_m += p1.haversine_distance(&p2);
        }
    }

    unsafe {
        *out_result = RouteResult {
            distance_m: total_distance_m,
            duration_s,
            num_points: num_points as i32,
        };
    }

    num_points as i32
}

/// Calculate route with full geometry using WKB geometries as input
/// Uses centroid of each geometry as routing point
/// Returns number of path points written, or -1 on error, -2 if not loaded
#[no_mangle]
pub extern "C" fn routing_route_wkb(
    from_wkb: *const u8,
    from_wkb_len: i32,
    to_wkb: *const u8,
    to_wkb_len: i32,
    mode: *const c_char,
    out_result: *mut RouteResult,
    out_points: *mut RoutePoint,
    max_points: i32,
) -> i32 {
    if from_wkb.is_null() || to_wkb.is_null() || out_result.is_null() || out_points.is_null() || max_points <= 0 {
        return -1;
    }

    let from_bytes = unsafe { std::slice::from_raw_parts(from_wkb, from_wkb_len as usize) };
    let to_bytes = unsafe { std::slice::from_raw_parts(to_wkb, to_wkb_len as usize) };

    let mode = match unsafe { CStr::from_ptr(mode) }.to_str() {
        Ok(s) if !mode.is_null() => s,
        _ => return -1,
    };

    // Parse WKB and get centroids
    let (lon1, lat1) = match wkb_to_centroid(from_bytes) {
        Some(c) => c,
        None => return -1,
    };

    let (lon2, lat2) = match wkb_to_centroid(to_bytes) {
        Some(c) => c,
        None => return -1,
    };

    let mutex = get_router_for_mode(mode);
    let mut guard = match mutex.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };

    let router = match guard.as_mut() {
        Some(r) => r,
        None => return -2,
    };

    let from_idx = match find_nearest_node(&router.data, lon1, lat1) {
        Some(idx) => idx,
        None => return -1,
    };

    let to_idx = match find_nearest_node(&router.data, lon2, lat2) {
        Some(idx) => idx,
        None => return -1,
    };

    let path = match router
        .calculator
        .calc_path(&router.data.fast_graph, from_idx, to_idx)
    {
        Some(p) => p,
        None => return -1,
    };

    let path_nodes = path.get_nodes();
    let duration_s = path.get_weight() as f64 / 1000.0;

    let mut total_distance_m = 0.0;
    let out_points = unsafe { std::slice::from_raw_parts_mut(out_points, max_points as usize) };
    let num_points = path_nodes.len().min(max_points as usize);

    for i in 0..num_points {
        let node_idx = path_nodes[i];
        let (lon, lat) = router.data.node_positions[node_idx];
        out_points[i] = RoutePoint { lat, lon };

        if i > 0 {
            let prev_idx = path_nodes[i - 1];
            let (prev_lon, prev_lat) = router.data.node_positions[prev_idx];
            let p1 = Point::new(prev_lon, prev_lat);
            let p2 = Point::new(lon, lat);
            total_distance_m += p1.haversine_distance(&p2);
        }
    }

    unsafe {
        *out_result = RouteResult {
            distance_m: total_distance_m,
            duration_s,
            num_points: num_points as i32,
        };
    }

    num_points as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_speed_lookup() {
        assert_eq!(get_speed_kmh("motorway", "auto"), Some(120.0));
        assert_eq!(get_speed_kmh("cycleway", "bicycle"), Some(20.0));
        assert_eq!(get_speed_kmh("footway", "pedestrian"), Some(5.0));
        assert_eq!(get_speed_kmh("railway", "auto"), None);
    }

    #[test]
    fn test_is_main_road() {
        assert!(is_main_road("motorway"));
        assert!(is_main_road("residential"));
        assert!(!is_main_road("footway"));
        assert!(!is_main_road("cycleway"));
    }

    #[test]
    fn test_rtree_nearest() {
        let points = vec![
            IndexedPoint { lon: 0.0, lat: 0.0, idx: 0 },
            IndexedPoint { lon: 1.0, lat: 1.0, idx: 1 },
            IndexedPoint { lon: 2.0, lat: 2.0, idx: 2 },
        ];
        let tree = RTree::bulk_load(points);

        let nearest = tree.nearest_neighbor(&[0.1, 0.1]).unwrap();
        assert_eq!(nearest.idx, 0);

        let nearest = tree.nearest_neighbor(&[1.9, 1.9]).unwrap();
        assert_eq!(nearest.idx, 2);
    }

    #[test]
    fn test_cache_path() {
        assert_eq!(
            cache_path("/data/italy.osm.pbf", "auto"),
            "/data/italy.osm.pbf.auto.routing"
        );
    }
}
