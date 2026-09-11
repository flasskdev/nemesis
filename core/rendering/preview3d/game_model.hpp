#pragma once

#include "geometry.hpp"
#include <core/features/features.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <string_view>
#include <algorithm>

namespace nemesis::preview3d {

    // Helper for procedural weapon geometry synthesis from CS2 item specs
    class mesh_builder {
    public:
        std::vector<vertex> vertices;

        void add_triangle(vertex a, vertex b, vertex c) {
            vec3 ab = b.position - a.position;
            vec3 ac = c.position - a.position;
            vec3 normal = unit(cross(ab, ac));
            if (length(a.normal) < 1e-6f) a.normal = normal;
            if (length(b.normal) < 1e-6f) b.normal = normal;
            if (length(c.normal) < 1e-6f) c.normal = normal;
            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);
        }

        void add_quad(vertex a, vertex b, vertex c, vertex d) {
            add_triangle(a, b, c);
            add_triangle(a, c, d);
        }

        void add_box(vec3 center, vec3 half_size, vec3 normal_hint = {0,0,0}) {
            const float x0 = center.x - half_size.x, x1 = center.x + half_size.x;
            const float y0 = center.y - half_size.y, y1 = center.y + half_size.y;
            const float z0 = center.z - half_size.z, z1 = center.z + half_size.z;

            // +Z front
            vec3 nz{0, 0, 1};
            add_quad(
                vertex{{x0, y0, z1}, nz, {0, 0}},
                vertex{{x1, y0, z1}, nz, {1, 0}},
                vertex{{x1, y1, z1}, nz, {1, 1}},
                vertex{{x0, y1, z1}, nz, {0, 1}}
            );
            // -Z back
            vec3 n_z{0, 0, -1};
            add_quad(
                vertex{{x1, y0, z0}, n_z, {0, 0}},
                vertex{{x0, y0, z0}, n_z, {1, 0}},
                vertex{{x0, y1, z0}, n_z, {1, 1}},
                vertex{{x1, y1, z0}, n_z, {0, 1}}
            );
            // +X right
            vec3 nx{1, 0, 0};
            add_quad(
                vertex{{x1, y0, z1}, nx, {0, 0}},
                vertex{{x1, y0, z0}, nx, {1, 0}},
                vertex{{x1, y1, z0}, nx, {1, 1}},
                vertex{{x1, y1, z1}, nx, {0, 1}}
            );
            // -X left
            vec3 n_x{-1, 0, 0};
            add_quad(
                vertex{{x0, y0, z0}, n_x, {0, 0}},
                vertex{{x0, y0, z1}, n_x, {1, 0}},
                vertex{{x0, y1, z1}, n_x, {1, 1}},
                vertex{{x0, y1, z0}, n_x, {0, 1}}
            );
            // +Y top
            vec3 ny{0, 1, 0};
            add_quad(
                vertex{{x0, y1, z1}, ny, {0, 0}},
                vertex{{x1, y1, z1}, ny, {1, 0}},
                vertex{{x1, y1, z0}, ny, {1, 1}},
                vertex{{x0, y1, z0}, ny, {0, 1}}
            );
            // -Y bottom
            vec3 n_y{0, -1, 0};
            add_quad(
                vertex{{x0, y0, z0}, n_y, {0, 0}},
                vertex{{x1, y0, z0}, n_y, {1, 0}},
                vertex{{x1, y0, z1}, n_y, {1, 1}},
                vertex{{x0, y0, z1}, n_y, {0, 1}}
            );
        }

        void add_tapered_box(vec3 center, vec3 half_bottom, vec3 half_top, float height) {
            const float y0 = center.y - height * 0.5f;
            const float y1 = center.y + height * 0.5f;

            // 8 corners
            vec3 c[8] = {
                {center.x - half_bottom.x, y0, center.z - half_bottom.z}, // 0: -X -Z
                {center.x + half_bottom.x, y0, center.z - half_bottom.z}, // 1: +X -Z
                {center.x + half_bottom.x, y0, center.z + half_bottom.z}, // 2: +X +Z
                {center.x - half_bottom.x, y0, center.z + half_bottom.z}, // 3: -X +Z
                {center.x - half_top.x, y1, center.z - half_top.z},       // 4: -X -Z
                {center.x + half_top.x, y1, center.z - half_top.z},       // 5: +X -Z
                {center.x + half_top.x, y1, center.z + half_top.z},       // 6: +X +Z
                {center.x - half_top.x, y1, center.z + half_top.z}        // 7: -X +Z
            };

            // Bottom
            add_quad(vertex{c[0], {0,-1,0}, {0,0}}, vertex{c[1], {0,-1,0}, {1,0}}, vertex{c[2], {0,-1,0}, {1,1}}, vertex{c[3], {0,-1,0}, {0,1}});
            // Top
            add_quad(vertex{c[7], {0,1,0}, {0,0}}, vertex{c[6], {0,1,0}, {1,0}}, vertex{c[5], {0,1,0}, {1,1}}, vertex{c[4], {0,1,0}, {0,1}});
            // Sides
            add_quad(vertex{c[3], {0,0,1}, {0,0}}, vertex{c[2], {0,0,1}, {1,0}}, vertex{c[6], {0,0,1}, {1,1}}, vertex{c[7], {0,0,1}, {0,1}});
            add_quad(vertex{c[1], {0,0,-1}, {0,0}}, vertex{c[0], {0,0,-1}, {1,0}}, vertex{c[4], {0,0,-1}, {1,1}}, vertex{c[5], {0,0,-1}, {0,1}});
            add_quad(vertex{c[2], {1,0,0}, {0,0}}, vertex{c[1], {1,0,0}, {1,0}}, vertex{c[5], {1,0,0}, {1,1}}, vertex{c[6], {1,0,0}, {0,1}});
            add_quad(vertex{c[0], {-1,0,0}, {0,0}}, vertex{c[3], {-1,0,0}, {1,0}}, vertex{c[7], {-1,0,0}, {1,1}}, vertex{c[4], {-1,0,0}, {0,1}});
        }

        void add_cylinder(vec3 start, vec3 end, float radius, int segments = 12) {
            vec3 dir = end - start;
            float h = length(dir);
            if (h < 1e-6f) return;
            vec3 axis = unit(dir);

            vec3 u = std::abs(axis.y) < 0.99f ? unit(cross(axis, {0, 1, 0})) : unit(cross(axis, {1, 0, 0}));
            vec3 v = cross(axis, u);

            for (int i = 0; i < segments; ++i) {
                float a0 = (i * 6.2831853f) / segments;
                float a1 = ((i + 1) * 6.2831853f) / segments;

                vec3 radial0 = u * std::cos(a0) + v * std::sin(a0);
                vec3 radial1 = u * std::cos(a1) + v * std::sin(a1);

                vec3 p0 = start + radial0 * radius;
                vec3 p1 = start + radial1 * radius;
                vec3 p2 = end + radial1 * radius;
                vec3 p3 = end + radial0 * radius;

                // Side
                add_quad(
                    vertex{p0, radial0, {static_cast<float>(i)/segments, 0}},
                    vertex{p1, radial1, {static_cast<float>(i+1)/segments, 0}},
                    vertex{p2, radial1, {static_cast<float>(i+1)/segments, 1}},
                    vertex{p3, radial0, {static_cast<float>(i)/segments, 1}}
                );

                // Caps
                add_triangle(vertex{start, axis * -1.0f, {0.5f, 0.5f}}, vertex{p1, axis * -1.0f, {0, 0}}, vertex{p0, axis * -1.0f, {1, 0}});
                add_triangle(vertex{end, axis, {0.5f, 0.5f}}, vertex{p3, axis, {0, 0}}, vertex{p2, axis, {1, 0}});
            }
        }

        void add_curved_blade(vec3 start, vec3 end, float width, float thickness, float curvature, int segments = 8) {
            vec3 dir = end - start;
            float len = length(dir);
            if (len < 1e-6f) return;
            vec3 fwd = unit(dir);
            vec3 side{0, 0, 1};
            vec3 edge_dir = unit(cross(fwd, side));

            std::vector<vec3> spine(segments + 1), edge(segments + 1);
            for (int i = 0; i <= segments; ++i) {
                float t = static_cast<float>(i) / segments;
                float curve = std::sin(t * 3.14159f) * curvature;
                vec3 center = start + fwd * (t * len) + edge_dir * curve;
                float cur_width = width * (1.0f - t * 0.85f);
                spine[i] = center - edge_dir * (cur_width * 0.3f);
                edge[i] = center + edge_dir * (cur_width * 0.7f);
            }

            for (int i = 0; i < segments; ++i) {
                vec3 p_s0 = spine[i] + side * (thickness * 0.5f);
                vec3 p_s1 = spine[i+1] + side * (thickness * 0.5f);
                vec3 p_e0 = edge[i];
                vec3 p_e1 = edge[i+1];

                vec3 m_s0 = spine[i] - side * (thickness * 0.5f);
                vec3 m_s1 = spine[i+1] - side * (thickness * 0.5f);

                // +Side
                add_triangle(vertex{p_s0, side, {0,0}}, vertex{p_e0, side, {1,0}}, vertex{p_e1, side, {1,1}});
                add_triangle(vertex{p_s0, side, {0,0}}, vertex{p_e1, side, {1,1}}, vertex{p_s1, side, {0,1}});

                // -Side
                add_triangle(vertex{m_s0, side * -1.0f, {0,0}}, vertex{m_s1, side * -1.0f, {0,1}}, vertex{p_e1, side * -1.0f, {1,1}});
                add_triangle(vertex{m_s0, side * -1.0f, {0,0}}, vertex{p_e1, side * -1.0f, {1,1}}, vertex{p_e0, side * -1.0f, {1,0}});

                // Spine back
                add_quad(vertex{p_s0, edge_dir * -1.0f, {0,0}}, vertex{p_s1, edge_dir * -1.0f, {1,0}},
                         vertex{m_s1, edge_dir * -1.0f, {1,1}}, vertex{m_s0, edge_dir * -1.0f, {0,1}});
            }
        }
    };

    // Calculate bounding box and normalize mesh to unit sphere with deterministic fingerprint
    inline void finalize_mesh(mesh& result, int def_index) {
        if (result.vertices.empty()) return;
        vec3 lo = result.vertices.front().position, hi = lo;
        for (const auto& v : result.vertices) {
            lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
            hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
        }
        result.center = (lo + hi) * 0.5f;
        result.radius = 0.0f;
        for (const auto& v : result.vertices)
            result.radius = std::max(result.radius, length(v.position - result.center));

        if (!std::isfinite(result.radius) || result.radius < 1e-9f)
            result.radius = 1.0f;

        for (auto& v : result.vertices)
            v.position = (v.position - result.center) / result.radius;

        // Deterministic fingerprint based on def_index and vertex count
        std::uint64_t fp = 14695981039346656037ull ^ static_cast<std::uint64_t>(def_index * 1000003);
        fp ^= static_cast<std::uint64_t>(result.vertices.size());
        fp *= 1099511628211ull;
        result.fingerprint = fp;
    }

    // Try capturing live weapon mesh directly from in-hand viewmodel CModel
    inline bool try_capture_from_game(int def_index, mesh& out_mesh) {
        if (!systems::g_local.get().is_valid()) return false;
        const auto pawn = systems::g_local.get().pawn;
        if (!pawn) return false;

        // Try HUD model weapon
        const auto arms_handle = memory::safe_read<std::uint32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash)).value_or(0);
        if (!arms_handle) return false;
        const auto arms = systems::g_entities.lookup(arms_handle);
        if (!arms) return false;
        const auto arms_scene_node = memory::safe_read<std::uintptr_t>(arms + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash)).value_or(0);
        if (!arms_scene_node) return false;

        auto child = memory::safe_read<std::uintptr_t>(arms_scene_node + SCHEMA("CGameSceneNode", "m_pChild"_hash)).value_or(0);
        std::uintptr_t weapon_scene_node = 0;
        while (child && child > 0x10000) {
            const auto owner = memory::safe_read<std::uintptr_t>(child + SCHEMA("CGameSceneNode", "m_pOwner"_hash)).value_or(0);
            if (owner && owner > 0x10000) {
                const auto name = systems::g_entities.get_schema_name(owner);
                if (name && fnv1a::runtime_hash(name) == "C_CS2HudModelWeapon"_hash) {
                    weapon_scene_node = memory::safe_read<std::uintptr_t>(owner + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash)).value_or(0);
                    break;
                }
            }
            child = memory::safe_read<std::uintptr_t>(child + SCHEMA("CGameSceneNode", "m_pNextSibling"_hash)).value_or(0);
        }

        if (!weapon_scene_node) return false;

        // Query hitboxes / bounds from CModel
        const auto hb_set = systems::g_hitboxes.query(weapon_scene_node, true);
        if (hb_set.count <= 0) return false;

        mesh_builder builder;
        for (int i = 0; i < hb_set.count; ++i) {
            const auto& hb = hb_set.entries[i];
            vec3 min_pt{hb.mins.x, hb.mins.y, hb.mins.z};
            vec3 max_pt{hb.maxs.x, hb.maxs.y, hb.maxs.z};
            vec3 center = (min_pt + max_pt) * 0.5f;
            vec3 size = (max_pt - min_pt) * 0.5f;
            if (hb.radius > 0.0f) {
                size.x += hb.radius; size.y += hb.radius; size.z += hb.radius;
            }
            builder.add_box(center, size);
        }

        if (builder.vertices.empty()) return false;
        out_mesh.vertices = std::move(builder.vertices);
        finalize_mesh(out_mesh, def_index);
        return true;
    }

    // High-fidelity procedural weapon mesh synthesizer covering all CS2 weapon categories
    inline mesh generate_weapon_mesh(int def_index, std::string_view name) {
        mesh_builder mb;

        // Categorize by def_index
        bool is_knife = (def_index >= 500 && def_index <= 526) || def_index == 41 || def_index == 42 || def_index == 59;
        bool is_glove = (def_index >= 5027 && def_index <= 5035);
        bool is_sniper = (def_index == 9 || def_index == 40 || def_index == 38 || def_index == 11);
        bool is_pistol = (def_index == 1 || def_index == 2 || def_index == 3 || def_index == 4 ||
                          def_index == 30 || def_index == 32 || def_index == 36 || def_index == 61 || def_index == 63 || def_index == 64);
        bool is_smg = (def_index == 17 || def_index == 19 || def_index == 23 || def_index == 24 || def_index == 26 || def_index == 33 || def_index == 34);
        bool is_shotgun = (def_index == 25 || def_index == 27 || def_index == 29 || def_index == 35);
        bool is_heavy = (def_index == 14 || def_index == 28);
        bool is_zeus = (def_index == 31);

        if (is_knife) {
            // Detailed knife geometry
            if (def_index == 507) {
                // Karambit: Curved talon blade + pommel ring + ergonomic handle
                // Blade
                mb.add_curved_blade({0.0f, 0.4f, 0.0f}, {0.85f, -0.2f, 0.0f}, 0.22f, 0.035f, 0.35f, 10);
                // Handle
                mb.add_box({-0.45f, 0.25f, 0.0f}, {0.35f, 0.12f, 0.05f});
                // Finger ring pommel at end
                mb.add_cylinder({-0.85f, 0.2f, -0.04f}, {-0.85f, 0.2f, 0.04f}, 0.16f, 16);
            } else if (def_index == 515) {
                // Butterfly: Spear blade + twin handles
                // Blade
                mb.add_box({0.45f, 0.0f, 0.0f}, {0.45f, 0.08f, 0.025f});
                // Pivot swivels
                mb.add_cylinder({0.0f, 0.08f, -0.06f}, {0.0f, 0.08f, 0.06f}, 0.04f, 8);
                mb.add_cylinder({0.0f, -0.08f, -0.06f}, {0.0f, -0.08f, 0.06f}, 0.04f, 8);
                // Twin handles
                mb.add_box({-0.5f, 0.12f, 0.0f}, {0.45f, 0.06f, 0.04f});
                mb.add_box({-0.5f, -0.12f, 0.0f}, {0.45f, 0.06f, 0.04f});
            } else if (def_index == 508) {
                // M9 Bayonet: Broad clip-point blade with saw-teeth + guard + cylindrical grip
                mb.add_box({0.55f, 0.0f, 0.0f}, {0.55f, 0.12f, 0.035f});
                // Guard with barrel ring
                mb.add_box({0.0f, 0.0f, 0.0f}, {0.04f, 0.28f, 0.08f});
                // Handle
                mb.add_cylinder({-0.02f, 0.0f, 0.0f}, {-0.75f, 0.0f, 0.0f}, 0.09f, 12);
                // Pommel
                mb.add_cylinder({-0.75f, 0.0f, 0.0f}, {-0.85f, 0.0f, 0.0f}, 0.11f, 10);
            } else {
                // General Knives (Bayonet, Flip, Gut, Huntsman, Bowie, Daggers, etc.)
                mb.add_box({0.5f, 0.0f, 0.0f}, {0.5f, 0.11f, 0.03f});
                mb.add_box({0.0f, 0.0f, 0.0f}, {0.03f, 0.22f, 0.06f}); // Crossguard
                mb.add_box({-0.45f, 0.0f, 0.0f}, {0.4f, 0.09f, 0.05f}); // Handle
            }
        } else if (is_glove) {
            // Gloves: Palm + wrist + 5 fingers
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.35f, 0.35f, 0.15f}); // Palm
            mb.add_box({0.0f, -0.45f, 0.0f}, {0.32f, 0.15f, 0.16f}); // Cuff
            // 4 fingers
            for (int f = 0; f < 4; ++f) {
                float fx = -0.24f + f * 0.16f;
                mb.add_cylinder({fx, 0.35f, 0.0f}, {fx, 0.85f, 0.0f}, 0.055f, 8);
            }
            // Thumb
            mb.add_cylinder({-0.35f, 0.05f, 0.08f}, {-0.65f, 0.35f, 0.12f}, 0.065f, 8);
        } else if (def_index == 7) {
            // AK-47: Classic receiver, curved magazine, wooden handguard, stock, sights
            // Main receiver
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.65f, 0.18f, 0.10f});
            // Curved top dust cover
            mb.add_cylinder({-0.65f, 0.15f, 0.0f}, {0.45f, 0.15f, 0.0f}, 0.10f, 10);
            // Handguard (wood lower and gas tube upper)
            mb.add_box({0.95f, -0.02f, 0.0f}, {0.32f, 0.14f, 0.09f});
            mb.add_cylinder({0.65f, 0.13f, 0.0f}, {1.35f, 0.13f, 0.0f}, 0.065f, 8);
            // Barrel and slant compensator
            mb.add_cylinder({1.25f, 0.0f, 0.0f}, {2.35f, 0.0f, 0.0f}, 0.045f, 10);
            mb.add_cylinder({2.35f, 0.0f, 0.0f}, {2.50f, 0.0f, 0.0f}, 0.060f, 8); // Muzzle brake
            // Front sight post
            mb.add_box({2.15f, 0.22f, 0.0f}, {0.05f, 0.15f, 0.03f});
            // Rear tangent sight
            mb.add_box({0.60f, 0.22f, 0.0f}, {0.10f, 0.06f, 0.04f});
            // Curved 30-round banana magazine
            mb.add_tapered_box({0.20f, -0.55f, 0.0f}, {0.18f, 0.08f, 0.08f}, {0.16f, 0.08f, 0.07f}, 0.65f);
            // Pistol grip
            mb.add_tapered_box({-0.45f, -0.45f, 0.0f}, {0.12f, 0.06f, 0.06f}, {0.14f, 0.06f, 0.07f}, 0.55f);
            // Wooden buttstock
            mb.add_tapered_box({-1.35f, -0.08f, 0.0f}, {0.70f, 0.15f, 0.07f}, {0.15f, 0.15f, 0.08f}, 0.40f);
        } else if (def_index == 16 || def_index == 60) {
            // M4A4 / M4A1-S: Flat top receiver, buffer tube + crane stock, handguard, barrel/silencer
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.55f, 0.19f, 0.09f}); // Lower/upper receiver
            mb.add_box({0.05f, 0.23f, 0.0f}, {0.45f, 0.04f, 0.05f}); // Picatinny top rail
            // Handguard
            mb.add_cylinder({0.55f, 0.0f, 0.0f}, {1.35f, 0.0f, 0.0f}, 0.11f, 12);
            if (def_index == 60) {
                // M4A1-S: Detachable suppressor
                mb.add_cylinder({1.35f, 0.0f, 0.0f}, {1.65f, 0.0f, 0.0f}, 0.045f, 8); // Barrel
                mb.add_cylinder({1.65f, 0.0f, 0.0f}, {2.75f, 0.0f, 0.0f}, 0.105f, 14); // Suppressor
            } else {
                // M4A4: Barrel + birdcage flash hider + front sight triangle
                mb.add_cylinder({1.35f, 0.0f, 0.0f}, {2.15f, 0.0f, 0.0f}, 0.050f, 8);
                mb.add_box({1.45f, 0.22f, 0.0f}, {0.08f, 0.16f, 0.03f}); // A2 Front sight tower
                mb.add_cylinder({2.15f, 0.0f, 0.0f}, {2.30f, 0.0f, 0.0f}, 0.065f, 8); // Flash hider
            }
            // Magazine (STANAG)
            mb.add_box({0.20f, -0.45f, 0.0f}, {0.14f, 0.35f, 0.065f});
            // A2 Pistol grip
            mb.add_tapered_box({-0.38f, -0.42f, 0.0f}, {0.11f, 0.055f, 0.055f}, {0.13f, 0.055f, 0.065f}, 0.50f);
            // Buffer tube & collapsible crane stock
            mb.add_cylinder({-0.55f, 0.04f, 0.0f}, {-1.35f, 0.04f, 0.0f}, 0.055f, 8);
            mb.add_box({-1.15f, -0.05f, 0.0f}, {0.30f, 0.20f, 0.085f});
        } else if (is_sniper) {
            // AWP / SSG 08 / Auto-snipers: Long chassis, heavy barrel, massive optical scope
            // Chassis / receiver
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.85f, 0.16f, 0.11f});
            // Long bull barrel
            mb.add_cylinder({0.85f, 0.03f, 0.0f}, {2.85f, 0.03f, 0.0f}, 0.065f, 10);
            // Massive muzzle brake
            mb.add_box({2.95f, 0.03f, 0.0f}, {0.15f, 0.09f, 0.09f});
            // High-power sniper scope
            mb.add_cylinder({-0.20f, 0.38f, 0.0f}, {0.60f, 0.38f, 0.0f}, 0.12f, 12);
            // Scope mount rings
            mb.add_box({-0.05f, 0.25f, 0.0f}, {0.06f, 0.10f, 0.07f});
            mb.add_box({0.45f, 0.25f, 0.0f}, {0.06f, 0.10f, 0.07f});
            // Stock with thumbhole and cheek riser
            mb.add_box({-1.15f, -0.08f, 0.0f}, {0.65f, 0.22f, 0.09f});
            mb.add_box({-1.05f, 0.20f, 0.0f}, {0.25f, 0.06f, 0.07f}); // Cheek rest
            // Box mag
            mb.add_box({0.15f, -0.35f, 0.0f}, {0.15f, 0.22f, 0.08f});
            // Pistol grip
            mb.add_box({-0.45f, -0.40f, 0.0f}, {0.12f, 0.22f, 0.07f});
        } else if (is_pistol) {
            // Pistols: Deagle, Glock, USP-S, P250, etc.
            float slide_len = (def_index == 1) ? 0.95f : 0.65f; // Deagle is huge
            float slide_h = (def_index == 1) ? 0.22f : 0.16f;
            float slide_w = (def_index == 1) ? 0.13f : 0.095f;

            // Slide
            mb.add_box({0.20f, 0.10f, 0.0f}, {slide_len * 0.5f, slide_h * 0.5f, slide_w * 0.5f});
            // Frame & trigger guard
            mb.add_box({0.0f, -0.05f, 0.0f}, {0.35f, 0.08f, slide_w * 0.45f});
            // Grip
            mb.add_tapered_box({-0.18f, -0.45f, 0.0f}, {0.14f, 0.08f, slide_w * 0.42f}, {0.16f, 0.08f, slide_w * 0.48f}, 0.48f);
            // Sights
            mb.add_box({0.20f + slide_len * 0.45f, 0.10f + slide_h * 0.55f, 0.0f}, {0.04f, 0.04f, 0.02f});
            mb.add_box({0.20f - slide_len * 0.45f, 0.10f + slide_h * 0.55f, 0.0f}, {0.04f, 0.04f, 0.035f});

            if (def_index == 61) {
                // USP-S detachable suppressor
                mb.add_cylinder({0.20f + slide_len * 0.5f, 0.08f, 0.0f}, {0.20f + slide_len * 0.5f + 1.15f, 0.08f, 0.0f}, 0.085f, 12);
            }
        } else if (is_smg) {
            // SMGs: Compact receiver, short barrel, forward mag / top mag
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.45f, 0.18f, 0.09f});
            mb.add_cylinder({0.45f, 0.0f, 0.0f}, {0.95f, 0.0f, 0.0f}, 0.055f, 8);
            if (def_index == 23) {
                // MP5-SD: Ribbed integral suppressor
                mb.add_cylinder({0.35f, 0.0f, 0.0f}, {1.35f, 0.0f, 0.0f}, 0.095f, 12);
            }
            // Grip & magazine
            mb.add_box({-0.20f, -0.40f, 0.0f}, {0.10f, 0.25f, 0.06f});
            mb.add_box({0.15f, -0.45f, 0.0f}, {0.08f, 0.35f, 0.05f});
        } else if (is_shotgun || is_heavy) {
            // Shotguns / Heavy LMG
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.75f, 0.20f, 0.12f});
            mb.add_cylinder({0.75f, 0.06f, 0.0f}, {2.25f, 0.06f, 0.0f}, 0.075f, 10);
            mb.add_cylinder({0.75f, -0.06f, 0.0f}, {1.85f, -0.06f, 0.0f}, 0.065f, 10); // Magazine tube
            mb.add_box({1.15f, -0.06f, 0.0f}, {0.25f, 0.12f, 0.11f}); // Pump / forend
            mb.add_box({-0.95f, -0.10f, 0.0f}, {0.55f, 0.18f, 0.08f}); // Stock
        } else if (is_zeus) {
            // Zeus x27 taser
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.35f, 0.16f, 0.09f});
            mb.add_box({0.38f, 0.0f, 0.0f}, {0.08f, 0.14f, 0.08f}); // Cartridge
            mb.add_box({-0.12f, -0.32f, 0.0f}, {0.11f, 0.18f, 0.065f}); // Grip
        } else {
            // Fallback general rifle / gun model
            mb.add_box({0.0f, 0.0f, 0.0f}, {0.60f, 0.18f, 0.09f});
            mb.add_cylinder({0.60f, 0.0f, 0.0f}, {1.95f, 0.0f, 0.0f}, 0.05f, 8);
            mb.add_box({0.15f, -0.45f, 0.0f}, {0.12f, 0.30f, 0.06f});
            mb.add_box({-0.35f, -0.40f, 0.0f}, {0.11f, 0.22f, 0.06f});
            mb.add_box({-1.05f, -0.08f, 0.0f}, {0.55f, 0.16f, 0.08f});
        }

        mesh result;
        result.vertices = std::move(mb.vertices);
        finalize_mesh(result, def_index);
        return result;
    }

} // namespace nemesis::preview3d
