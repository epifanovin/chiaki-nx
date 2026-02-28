#version 460

layout (location = 0) in vec2 vTextureCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D plane0;
layout (binding = 1) uniform sampler2D plane1;

layout (std140, binding = 0) uniform ColorParams
{
    float y_offset;
    float y_scale;
    float uv_offset;
    float r_cr;
    float g_cb;
    float g_cr;
    float b_cb;
    float sharpen;
};

void main()
{
    float y_raw = texture(plane0, vTextureCoord).r;
    float u_raw = texture(plane1, vTextureCoord).r;
    float v_raw = texture(plane1, vTextureCoord).g;

    float y = (y_raw - y_offset) * y_scale;
    float u = u_raw - uv_offset;
    float v = v_raw - uv_offset;

    vec3 rgb;
    rgb.r = y + r_cr * v;
    rgb.g = y + g_cb * u + g_cr * v;
    rgb.b = y + b_cb * u;

    if(sharpen > 0.0)
    {
        vec2 ts = 1.0 / textureSize(plane0, 0);
        float n = texture(plane0, vTextureCoord + vec2(0.0, -ts.y)).r;
        float s = texture(plane0, vTextureCoord + vec2(0.0,  ts.y)).r;
        float e = texture(plane0, vTextureCoord + vec2( ts.x, 0.0)).r;
        float w = texture(plane0, vTextureCoord + vec2(-ts.x, 0.0)).r;
        float edge = 4.0 * y_raw - (n + s + e + w);
        rgb += sharpen * edge;
    }

    outColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
