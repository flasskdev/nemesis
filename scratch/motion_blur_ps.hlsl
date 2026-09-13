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

// Returns whether the UV is within a HUD/UI interactive region
float get_hud_region(float2 uv)
{
    // Radar & Money (top left)
    if (uv.x <= 0.32f && uv.y <= 0.45f)
        return 1.0f;

    // Top bar (scores, timer, player avatars, win streaks)
    if (uv.x >= 0.18f && uv.x <= 0.82f && uv.y <= 0.14f)
        return 1.0f;

    // Killfeed & Telemetry (top right)
    if (uv.x >= 0.60f && uv.y <= 0.40f)
        return 1.0f;

    // Health, Armor, & Teammate list (bottom left)
    if (uv.x <= 0.38f && uv.y >= 0.75f)
        return 1.0f;

    // Weapon, Ammo, & Fire mode (bottom right)
    if (uv.x >= 0.60f && uv.y >= 0.75f)
        return 1.0f;

    // Equipment, Grenades, C4 / Defuse kit (bottom center)
    if (uv.x >= 0.25f && uv.x <= 0.75f && uv.y >= 0.82f)
        return 1.0f;

    // Chat box & Radio commands (mid left)
    if (uv.x <= 0.35f && uv.y >= 0.30f && uv.y <= 0.75f)
        return 1.0f;

    // Crosshair & Aim reticle (center)
    if (abs(uv.x - 0.5f) <= 0.035f && abs(uv.y - 0.5f) <= 0.035f)
        return 1.0f;

    // Bomb planting / defuse progress bar & spectator overlay (mid lower center)
    if (abs(uv.x - 0.5f) <= 0.18f && uv.y >= 0.62f && uv.y <= 0.78f)
        return 1.0f;

    return 0.0f;
}

float get_viewmodel_region(float2 uv)
{
    if (abs(g_viewmodel_handedness) > 0.1f)
    {
        float vm_x = (g_viewmodel_handedness > 0.0f) ? uv.x : (1.0f - uv.x);
        if (vm_x >= 0.42f && uv.y >= 0.42f)
        {
            float diag = (vm_x - 0.42f) * 1.3f + (uv.y - 0.42f) * 1.1f;
            return smoothstep(0.25f, 0.55f, diag);
        }
    }
    return 0.0f;
}

float4 main(PS_INPUT input) : SV_Target
{
    float2 uv = input.uv;
    float4 sharp = g_scene_texture.SampleLevel(g_scene_sampler, uv, 0.0f);
    
    // Smooth center protection around aiming reticle
    float center_mask = 0.0f;
    if (g_center_protection > 0.0f)
    {
        float2 center_dist = uv - float2(0.5f, 0.5f);
        center_dist.x *= g_aspect_ratio;
        float d = length(center_dist);
        float factor = saturate(d * 2.5f);
        center_mask = 1.0f - lerp(1.0f - g_center_protection, 1.0f, factor);
    }
    
    float2 motion = g_motion_vector * g_strength * (1.0f - center_mask);
    if (length(motion) < 0.0001f)
    {
        return sharp;
    }
    
    int samples = clamp(g_sample_count, 4, 24);
    float inv_samples_minus_one = 1.0f / float(samples - 1);
    
    float4 acc = float4(0, 0, 0, 0);
    float total_weight = 0.0f;
    
    // Background motion blur: smoothly samples the entire 3D scene across the whole screen (including under HUD)
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) * inv_samples_minus_one - 0.5f;
        float weight = max(1.0f - abs(t) * 1.2f, 0.1f);
        float2 sample_uv = clamp(uv + motion * t, float2(0.0005f, 0.0005f), float2(0.9995f, 0.9995f));
        
        acc += g_scene_texture.SampleLevel(g_scene_sampler, sample_uv, 0.0f) * weight;
        total_weight += weight;
    }
    
    float4 blurred = (total_weight > 0.0001f) ? (acc / total_weight) : sharp;
    
    // HUD and Viewmodel preservation:
    // Blur works everywhere in the background under the HUD.
    // Sharp HUD elements (text, numbers, icons, crosshair lines) and foreground viewmodel are preserved on top.
    float in_hud = get_hud_region(uv);
    float in_vm  = get_viewmodel_region(uv);
    
    if (in_hud > 0.5f)
    {
        // Inside HUD areas: detect sharp HUD text/icon strokes via contrast against the blurred background
        float3 diff = abs(sharp.rgb - blurred.rgb);
        float max_diff = max(diff.r, max(diff.g, diff.b));
        
        // High-contrast stroke/icon/crosshair: preserve sharp HUD element;
        // Background under HUD: keep smooth motion blur
        float hud_blend = smoothstep(0.07f, 0.22f, max_diff);
        return lerp(blurred, sharp, hud_blend);
    }
    
    if (in_vm > 0.01f)
    {
        // Viewmodel foreground preservation
        float3 diff = abs(sharp.rgb - blurred.rgb);
        float max_diff = max(diff.r, max(diff.g, diff.b));
        float vm_blend = in_vm * smoothstep(0.08f, 0.24f, max_diff);
        return lerp(blurred, sharp, vm_blend);
    }
    
    return blurred;
}
