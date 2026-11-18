// CRT-style filter (see crt.h). Hand-written shaders for the backends we build
// with: GLSL 410 (Linux, Windows), GLSL ES 300 (a future web build) and MSL
// (macOS). They compute the same thing; keep them in step.
#include "video/crt.h"

#include <string.h>

// One source line becomes a light beam with a gaussian profile; bright pixels
// make a wider beam (as on a real tube), so dark areas show more scanline gaps.
// The effect fades in with the scale: below ~2x there's no room for scanlines.
#define CRT_SHARED_GLSL \
    "uniform vec4 fs_params[3];\n" \
    "uniform sampler2D tex;\n" \
    "in vec2 uv;\n" \
    "out vec4 frag_color;\n" \
    "vec3 fetch(vec2 t) {\n" \
    "    t = clamp(t, vec2(0.0), fs_params[0].xy - 1.0);\n" \
    "    vec3 c = texture(tex, (t + 0.5) * fs_params[0].zw).rgb;\n" \
    "    return c * c;\n" /* roughly linear light */ \
    "}\n" \
    "vec3 beam(float d, vec3 c) {\n" \
    "    vec3 w = mix(vec3(0.28), vec3(0.48), c);\n" \
    "    return exp(-0.5 * d * d / (w * w));\n" \
    "}\n" \
    "void main() {\n" \
    "    vec2 src = uv / fs_params[0].zw - 0.5;\n" \
    "    vec2 i = floor(src);\n" \
    "    vec2 f = src - i;\n" \
    "    float sx = smoothstep(0.25, 0.75, f.x);\n" \
    "    vec3 l0 = mix(fetch(i), fetch(i + vec2(1.0, 0.0)), sx);\n" \
    "    vec3 l1 = mix(fetch(i + vec2(0.0, 1.0)), fetch(i + vec2(1.0, 1.0)), sx);\n" \
    "    float s = fs_params[1].z;\n" \
    "    vec3 flat_c = mix(l0, l1, f.y);\n" \
    "    vec3 crt = l0 * beam(f.y, l0) + l1 * beam(1.0 - f.y, l1);\n" \
    "    vec3 c = mix(flat_c, crt * 1.45, s);\n" \
    "    float m = fs_params[2].x * s;\n" \
    "    int px = int(mod(gl_FragCoord.x, 3.0));\n" \
    "    vec3 mask = vec3(1.0 - m);\n" \
    "    if (px == 0) mask.r = 1.0 + m; else if (px == 1) mask.g = 1.0 + m; else mask.b = 1.0 + m;\n" \
    "    c *= mask;\n" \
    "    frag_color = vec4(sqrt(clamp(c, 0.0, 1.0)) * fs_params[1].w, 1.0);\n" \
    "}\n"

#define CRT_VS_GLSL_BODY \
    "uniform vec4 vs_params[2];\n" \
    "layout(location = 0) in vec2 corner;\n" \
    "out vec2 uv;\n" \
    "void main() {\n" \
    "    gl_Position = vec4(mix(vs_params[0].xy, vs_params[0].zw, corner), 0.0, 1.0);\n" \
    "    uv = corner * vs_params[1].xy;\n" \
    "}\n"

#if defined(SOKOL_GLCORE)
static const char* vs_src = "#version 410\n" CRT_VS_GLSL_BODY;
static const char* fs_src = "#version 410\n" CRT_SHARED_GLSL;
#elif defined(SOKOL_GLES3)
static const char* vs_src = "#version 300 es\n" CRT_VS_GLSL_BODY;
static const char* fs_src = "#version 300 es\nprecision mediump float;\n" CRT_SHARED_GLSL;
#elif defined(SOKOL_METAL)
static const char* vs_src =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct vs_params_t { float4 p[2]; };\n"
    "struct vs_in { float2 corner [[attribute(0)]]; };\n"
    "struct vs_out { float4 pos [[position]]; float2 uv; };\n"
    "vertex vs_out vs_main(vs_in in [[stage_in]], constant vs_params_t& P [[buffer(0)]]) {\n"
    "    vs_out o;\n"
    "    o.pos = float4(mix(P.p[0].xy, P.p[0].zw, in.corner), 0.0, 1.0);\n"
    "    o.uv = in.corner * P.p[1].xy;\n"
    "    return o;\n"
    "}\n";
static const char* fs_src =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct fs_params_t { float4 p[3]; };\n"
    "struct vs_out { float4 pos [[position]]; float2 uv; };\n"
    "static float3 fetch(texture2d<float> tex, sampler smp, constant fs_params_t& P, float2 t) {\n"
    "    t = clamp(t, float2(0.0), P.p[0].xy - 1.0);\n"
    "    float3 c = tex.sample(smp, (t + 0.5) * P.p[0].zw).rgb;\n"
    "    return c * c;\n"
    "}\n"
    "static float3 beam(float d, float3 c) {\n"
    "    float3 w = mix(float3(0.28), float3(0.48), c);\n"
    "    return exp(-0.5 * d * d / (w * w));\n"
    "}\n"
    "fragment float4 fs_main(vs_out in [[stage_in]], constant fs_params_t& P [[buffer(0)]],\n"
    "                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "    float2 src = in.uv / P.p[0].zw - 0.5;\n"
    "    float2 i = floor(src);\n"
    "    float2 f = src - i;\n"
    "    float sx = smoothstep(0.25, 0.75, f.x);\n"
    "    float3 l0 = mix(fetch(tex, smp, P, i), fetch(tex, smp, P, i + float2(1.0, 0.0)), sx);\n"
    "    float3 l1 = mix(fetch(tex, smp, P, i + float2(0.0, 1.0)), fetch(tex, smp, P, i + float2(1.0, 1.0)), sx);\n"
    "    float s = P.p[1].z;\n"
    "    float3 flat_c = mix(l0, l1, f.y);\n"
    "    float3 crt = l0 * beam(f.y, l0) + l1 * beam(1.0 - f.y, l1);\n"
    "    float3 c = mix(flat_c, crt * 1.45, s);\n"
    "    float m = P.p[2].x * s;\n"
    "    int px = int(fmod(in.pos.x, 3.0));\n"
    "    float3 mask = float3(1.0 - m);\n"
    "    if (px == 0) mask.r = 1.0 + m; else if (px == 1) mask.g = 1.0 + m; else mask.b = 1.0 + m;\n"
    "    c *= mask;\n"
    "    return float4(sqrt(clamp(c, 0.0, 1.0)) * P.p[1].w, 1.0);\n"
    "}\n";
#else
#error "crt.c: no shader for this graphics backend"
#endif

static struct {
    sg_shader shd;
    sg_pipeline pip;
    sg_buffer vbuf;
    sg_sampler smp;
    bool ok;
} crt;

void crt_init(void) {
    memset(&crt, 0, sizeof(crt));
    sg_shader_desc d = {
        .vertex_func = { .source = vs_src },
        .fragment_func = { .source = fs_src },
        .attrs[0] = { .glsl_name = "corner", .base_type = SG_SHADERATTRBASETYPE_FLOAT },
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_VERTEX, .size = 2 * 16, .msl_buffer_n = 0,
            .glsl_uniforms[0] = { .type = SG_UNIFORMTYPE_FLOAT4, .array_count = 2, .glsl_name = "vs_params" },
        },
        .uniform_blocks[1] = {
            .stage = SG_SHADERSTAGE_FRAGMENT, .size = 3 * 16, .msl_buffer_n = 0,
            .glsl_uniforms[0] = { .type = SG_UNIFORMTYPE_FLOAT4, .array_count = 3, .glsl_name = "fs_params" },
        },
        .views[0].texture = { .stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D,
                              .sample_type = SG_IMAGESAMPLETYPE_FLOAT, .msl_texture_n = 0 },
        .samplers[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_FILTERING, .msl_sampler_n = 0 },
        .texture_sampler_pairs[0] = { .stage = SG_SHADERSTAGE_FRAGMENT, .view_slot = 0, .sampler_slot = 0, .glsl_name = "tex" },
        .label = "crt-shader",
    };
#if defined(SOKOL_METAL)
    d.vertex_func.entry = "vs_main";
    d.fragment_func.entry = "fs_main";
#endif
    crt.shd = sg_make_shader(&d);
    crt.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = crt.shd,
        .layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .label = "crt-pipeline",
    });
    static const float corners[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    crt.vbuf = sg_make_buffer(&(sg_buffer_desc){ .data = SG_RANGE(corners), .label = "crt-quad" });
    // The shader filters by itself; nearest keeps texel fetches exact.
    crt.smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    crt.ok = sg_query_pipeline_state(crt.pip) == SG_RESOURCESTATE_VALID;
}

void crt_shutdown(void) {
    // sg_shutdown() releases everything
    memset(&crt, 0, sizeof(crt));
}

void crt_draw(const crt_frame_t* f) {
    if (!crt.ok || f->w <= 0.0f || f->h <= 0.0f || f->src_h <= 0.0f) return;
    // clip space: x right, y up
    const float x0 = f->x / f->win_w * 2.0f - 1.0f, x1 = (f->x + f->w) / f->win_w * 2.0f - 1.0f;
    const float y0 = 1.0f - f->y / f->win_h * 2.0f, y1 = 1.0f - (f->y + f->h) / f->win_h * 2.0f;
    const float vs_params[8] = { x0, y0, x1, y1, f->src_w / f->tex_w, f->src_h / f->tex_h, 0.0f, 0.0f };
    const float scale = f->h / f->src_h;
    float strength = (scale - 2.0f) / 1.5f;        // none at 2x, full from 3.5x
    strength = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    const float fs_params[12] = {
        f->src_w, f->src_h, 1.0f / f->tex_w, 1.0f / f->tex_h,
        f->w, f->h, strength, f->dim,
        0.12f, 0.0f, 0.0f, 0.0f,                    // RGB mask strength
    };
    sg_apply_pipeline(crt.pip);
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers[0] = crt.vbuf,
        .views[0] = f->texture,
        .samplers[0] = crt.smp,
    });
    sg_apply_uniforms(0, &SG_RANGE(vs_params));
    sg_apply_uniforms(1, &SG_RANGE(fs_params));
    sg_draw(0, 4, 1);
}
