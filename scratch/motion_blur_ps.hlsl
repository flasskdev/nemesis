cbuffer MotionBlurConstants : register(b0)
{
    float2 g_motion_vector;
    float  g_center_protection;
    float  g_strength;
    int    g_sample_count;
    float  g_aspect_ratio;
    float2 g_screen_res;
    float  g_ignore_hud;
    float  g_ignore_viewmodel;
    float  g_viewmodel_handedness;
    float  pad;
};

SamplerState g_scene_sampler : register(s0);
Texture2D    g_scene_texture : register(t0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// 1. Radar circle mask (top left)
float get_radar_mask(float2 uv, float aspect)
{
    float2 center = float2(0.092f, 0.135f);
    float2 delta = uv - center;
    delta.x *= aspect;
    float dist = length(delta);
    return 1.0f - smoothstep(0.070f, 0.075f, dist);
}

// 2. Top bar (match score, timer, avatars)
float get_topbar_mask(float2 uv)
{
    if (uv.x >= 0.36f && uv.x <= 0.64f && uv.y <= 0.068f)
    {
        float edge_x = min(smoothstep(0.36f, 0.38f, uv.x), smoothstep(0.64f, 0.62f, uv.x));
        float edge_y = smoothstep(0.068f, 0.058f, uv.y);
        return edge_x * edge_y;
    }
    return 0.0f;
}

// 3. Identification of 2D HUD text/icon overlay zones:
// - Health & Armor (bottom-left)
// - Ammo & Weapon info (bottom-right)
// - Killfeed (top-right)
// - Chat (mid-left)
// - Center reticle / crosshair (center)
bool is_in_text_hud_zone(float2 uv)
{
    // Health & Armor (bottom left)
    if (uv.x >= 0.015f && uv.x <= 0.22f && uv.y >= 0.90f && uv.y <= 0.99f)
        return true;

    // Ammo & Weapon info (bottom right)
    if (uv.x >= 0.78f && uv.x <= 0.985f && uv.y >= 0.90f && uv.y <= 0.99f)
        return true;

    // Killfeed & spectator info (top right)
    if (uv.x >= 0.74f && uv.x <= 0.99f && uv.y >= 0.015f && uv.y <= 0.22f)
        return true;

    // Chat / radio commands (mid left)
    if (uv.x >= 0.015f && uv.x <= 0.28f && uv.y >= 0.45f && uv.y <= 0.75f)
        return true;

    // Center reticle / crosshair
    if (abs(uv.x - 0.5f) <= 0.018f && abs(uv.y - 0.5f) <= 0.018f)
        return true;

    return false;
}

// Determines if a pixel in a text HUD zone is an actual sharp UI glyph
// (letters, numbers, icons, crosshair) vs the 3D game background behind it.
float get_glyph_weight(float4 col, float4 bg)
{
    float luma = dot(col.rgb, float3(0.299f, 0.587f, 0.114f));
    float bg_luma = dot(bg.rgb, float3(0.299f, 0.587f, 0.114f));

    // High-brightness text/glyph (white font, bright icons):
    float is_bright = smoothstep(0.72f, 0.88f, luma);

    // Saturated UI color (health red, money green, armor cyan):
    float max_c = max(col.r, max(col.g, col.b));
    float min_c = min(col.r, min(col.g, col.b));
    float sat = (max_c > 0.01f) ? (max_c - min_c) / max_c : 0.0f;
    float is_vivid_ui = smoothstep(0.48f, 0.72f, sat) * smoothstep(0.50f, 0.75f, max_c);

    // Strong contrast above the local background:
    float is_contrast = smoothstep(0.14f, 0.30f, luma - bg_luma);

    return saturate(max(is_bright, max(is_vivid_ui, is_contrast)));
}

// 4. Viewmodel (character hands and weapon):
float get_viewmodel_mask(float2 uv)
{
    if (abs(g_viewmodel_handedness) > 0.1f)
    {
        float vm_x = (g_viewmodel_handedness > 0.0f) ? uv.x : (1.0f - uv.x);
        if (vm_x >= 0.48f && uv.y >= 0.48f)
        {
            float diag = (vm_x - 0.48f) * 1.25f + (uv.y - 0.48f) * 1.05f;
            return smoothstep(0.32f, 0.65f, diag);
        }
    }
    return 0.0f;
}

float4 main(PS_INPUT input) : SV_Target
{
    float2 uv = input.uv;
    float4 sharp = g_scene_texture.SampleLevel(g_scene_sampler, uv, 0.0f);

    // 1. Center reticle protection for smooth aiming
    float center_mask = 0.0f;
    if (g_center_protection > 0.0f)
    {
        float2 center_dist = uv - float2(0.5f, 0.5f);
        center_dist.x *= g_aspect_ratio;
        float d = length(center_dist);
        float factor = saturate(d * 2.5f);
        center_mask = 1.0f - lerp(1.0f - g_center_protection, 1.0f, factor);
    }

    // Velocity deadzone
    float2 motion = g_motion_vector * g_strength * (1.0f - center_mask);
    if (length(motion) < 0.0001f)
    {
        return sharp;
    }

    int samples = clamp(g_sample_count, 4, 24);
    float inv_samples_minus_one = 1.0f / float(samples - 1);

    float4 acc = float4(0, 0, 0, 0);
    float total_weight = 0.0f;

    // Background motion blur:
    // Samples the entire 3D scene smoothly across the whole screen.
    // Suppresses bright UI glyphs from bleeding ghost trails into the background.
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) * inv_samples_minus_one - 0.5f;
        float weight = max(1.0f - abs(t) * 1.2f, 0.1f);
        float2 sample_uv = clamp(uv + motion * t, float2(0.0005f, 0.0005f), float2(0.9995f, 0.9995f));

        float4 s_col = g_scene_texture.SampleLevel(g_scene_sampler, sample_uv, 0.0f);

        // If sample lands in a text HUD zone, attenuate bright text so it doesn't leave ghost trails on the ground
        if (is_in_text_hud_zone(sample_uv))
        {
            float s_luma = dot(s_col.rgb, float3(0.299f, 0.587f, 0.114f));
            float s_glyph = smoothstep(0.72f, 0.88f, s_luma);
            weight *= (1.0f - s_glyph * 0.80f);
        }

        acc += s_col * weight;
        total_weight += weight;
    }

    float4 blurred = (total_weight > 0.0001f) ? (acc / total_weight) : sharp;

    // 2. Solid HUD elements: Radar and Top Bar
    float radar_mask = get_radar_mask(uv, g_aspect_ratio);
    if (radar_mask > 0.001f)
    {
        return lerp(blurred, sharp, radar_mask);
    }

    float topbar_mask = get_topbar_mask(uv);
    if (topbar_mask > 0.001f)
    {
        return lerp(blurred, sharp, topbar_mask);
    }

    // 3. Text HUD Elements (Health, Armor, Ammo, Killfeed, Chat, Crosshair):
    // The background under the HUD remains smoothly motion-blurred.
    // Sharp HUD glyphs (text, icons, reticle) are composited cleanly on top.
    if (is_in_text_hud_zone(uv))
    {
        float glyph_alpha = get_glyph_weight(sharp, blurred);
        return lerp(blurred, sharp, glyph_alpha);
    }

    // 4. Viewmodel (character hands & weapon):
    // Keeps the weapon foreground crisp while blurring the 3D world behind it.
    float vm_mask = get_viewmodel_mask(uv);
    if (vm_mask > 0.001f)
    {
        return lerp(blurred, sharp, vm_mask);
    }

    // Everywhere else: 100% smooth, rich cinematic motion blur
    return blurred;
}
