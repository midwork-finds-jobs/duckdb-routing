-- Macro: travel_time_route
-- Automatically converts WKB BLOB geometry to GEOMETRY type
--
-- This is the recommended routing function. It wraps travel_time_route_wkb()
-- and returns the geometry field as GEOMETRY type instead of BLOB.
--
-- For direct WKB output (no spatial extension needed), use travel_time_route_wkb()
--
-- Prerequisites:
--   INSTALL spatial;
--   LOAD spatial;
--
-- Usage:
--   .read examples/geometry_macro.sql
--
--   WITH route AS (
--       SELECT travel_time_route(
--           ST_Point(12.4964, 41.9028),  -- Rome
--           ST_Point(11.2558, 43.7696),  -- Florence
--           'auto'
--       ) as r
--   )
--   SELECT
--       r.distance_km,
--       r.duration_minutes,
--       r.geometry,  -- GEOMETRY type, ready for spatial operations!
--       ST_NPoints(r.geometry) as num_points
--   FROM route;

CREATE OR REPLACE MACRO travel_time_route(from_geom, to_geom, costing) AS (
    SELECT struct_pack(
        distance_km := r.distance_km,
        duration_minutes := r.duration_minutes,
        geometry := ST_GeomFromWKB(r.geometry)
    ) FROM (SELECT travel_time_route_wkb(from_geom, to_geom, costing) as r)
);
