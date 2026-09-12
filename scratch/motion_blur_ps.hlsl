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

float get_exclusion_mask(float2 uv)
{
    float mask = 0.0f;

    // 1. HUD Protection
    if (g_ignore_hud > 0.5f)
    {
        // Radar (top left): x in [0.0, 0.22], y in [0.0, 0.36]
        if (uv.x <= 0.22f && uv.y <= 0.36f)
        {
            float edge_x = smoothstep(0.22f, 0.18f, uv.x);
            float edge_y = smoothstep(0.36f, 0.30f, uv.y);
            mask = max(mask, min(edge_x, edge_y));
        }

        // Top bar (scores, timer, player avatars): x in [0.28, 0.72], y in [0.0, 0.09]
        if (uv.x >= 0.28f && uv.x <= 0.72f && uv.y <= 0.09f)
        {
            float edge_x1 = smoothstep(0.28f, 0.32f, uv.x);
            float edge_x2 = smoothstep(0.72f, 0.68f, uv.x);
            float edge_y  = smoothstep(0.09f, 0.06f, uv.y);
            mask = max(mask, min(min(edge_x1, edge_x2), edge_y));
        }

        // Killfeed (top right): x in [0.70, 1.0], y in [0.0, 0.28]
        if (uv.x >= 0.70f && uv.y <= 0.28f)
        {
            float edge_x = smoothstep(0.70f, 0.74f, uv.x);
            float edge_y = smoothstep(0.28f, 0.24f, uv.y);
            mask = max(mask, min(edge_x, edge_y));
        }

        // Health & Armor (bottom left): x in [0.0, 0.30], y in [0.85, 1.0]
        if (uv.x <= 0.30f && uv.y >= 0.85f)
        {
            float edge_x = smoothstep(0.30f, 0.26f, uv.x);
            float edge_y = smoothstep(0.85f, 0.89f, uv.y);
            mask = max(mask, min(edge_x, edge_y));
        }

        // Weapon & Ammo (bottom right): x in [0.72, 1.0], y in [0.85, 1.0]
        if (uv.x >= 0.72f && uv.y >= 0.85f)
        {
            float edge_x = smoothstep(0.72f, 0.76f, uv.x);
            float edge_y = smoothstep(0.85f, 0.89f, uv.y);
            mask = max(mask, min(edge_x, edge_y));
        }

        // Equipment / Grenades (bottom center): x in [0.34, 0.66], y in [0.90, 1.0]
        if (uv.x >= 0.34f && uv.x <= 0.66f && uv.y >= 0.90f)
        {
            float edge_x1 = smoothstep(0.34f, 0.38f, uv.x);
            float edge_x2 = smoothstep(0.66f, 0.62f, uv.x);
            float edge_y  = smoothstep(0.90f, 0.93f, uv.y);
            mask = max(mask, min(min(edge_x1, edge_x2), edge_y));
        }

        // Chat box (mid left): x in [0.0, 0.26], y in [0.38, 0.72]
        if (uv.x <= 0.26f && uv.y >= 0.38f && uv.y <= 0.72f)
        {
            float edge_x  = smoothstep(0.26f, 0.22f, uv.x);
            float edge_y1 = smoothstep(0.38f, 0.42f, uv.y);
            float edge_y2 = smoothstep(0.72f, 0.68f, uv.y);
            mask = max(mask, min(min(edge_x, edge_y1), edge_y2));
        }
    }

    // 2. Viewmodel (Character Hands & Weapon) Protection
    if (g_ignore_viewmodel > 0.5f && abs(g_viewmodel_handedness) > 0.1f)
    {
        float vm_x = (g_viewmodel_handedness > 0.0f) ? uv.x : (1.0f - uv.x);
        if (vm_x >= 0.38f && uv.y >= 0.36f)
        {
            float diag = (vm_x - 0.38f) * 1.3f + (uv.y - 0.36f) * 1.1f;
            float vm_mask = smoothstep(0.35f, 0.60f, diag);
            mask = max(mask, vm_mask);
        }
    }

    return saturate(mask);
}

float4 main(PS_INPUT input) : SV_Target
{
    float2 uv = input.uv;
    
    float center_mask = 0.0f;
    if (g_center_protection > 0.0f)
    {
        float2 center_dist = uv - float2(0.5f, 0.5f);
        center_dist.x *= g_aspect_ratio;
        float d = length(center_dist);
        float factor = saturate(d * 2.5f);
        center_mask = 1.0f - lerp(1.0f - g_center_protection, 1.0f, factor);
    }
    
    float exclusion = max(get_exclusion_mask(uv), center_mask);
    
    if (exclusion >= 0.98f)
    {
        return g_scene_texture.SampleLevel(g_scene_sampler, uv, 0.0f);
    }
    
    float2 motion = g_motion_vector * g_strength * (1.0f - exclusion);
    if (length(motion) < 0.0001f)
    {
        return g_scene_texture.SampleLevel(g_scene_sampler, uv, 0.0f);
    }
    
    int samples = clamp(g_sample_count, 4, 24);
    float inv_samples_minus_one = 1.0f / float(samples - 1);
    
    float4 acc = float4(0, 0, 0, 0);
    float total_weight = 0.0f;
    
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) * inv_samples_minus_one - 0.5f;
        float weight = max(1.0f - abs(t) * 1.2f, 0.1f);
        float2 sample_uv = clamp(uv + motion * t, float2(0.0005f, 0.0005f), float2(0.9995f, 0.9995f));
        
        float sample_ex = get_exclusion_mask(sample_uv);
        weight *= (1.0f - sample_ex * 0.85f);
        
        acc += g_scene_texture.SampleLevel(g_scene_sampler, sample_uv, 0.0f) * weight;
        total_weight += weight;
    }
    
    if (total_weight <= 0.0001f)
    {
        return g_scene_texture.SampleLevel(g_scene_sampler, uv, 0.0f);
    }
    
    return acc / total_weight;
}
