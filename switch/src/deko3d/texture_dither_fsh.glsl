#version 460

layout (location = 0) in vec2 vTextureCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D plane0;
layout (binding = 1) uniform sampler2D plane1;

layout (std140, binding = 0) uniform DitherParams {
    float frame_counter;
    float deband_range;
};

vec4 hash43n(vec3 p)
{
    p = fract(p * vec3(5.3987, 5.4421, 6.9371));
    p += dot(p.yzx, p.xyz + vec3(21.5351, 14.3137, 15.3247));
    return fract(vec4(p.x * p.y * 95.4307, p.x * p.y * 97.5901,
                      p.x * p.z * 93.8369, p.y * p.z * 91.6931));
}

void main()
{
    float y_center = texture(plane0, vTextureCoord).r;
    float u_chroma = texture(plane1, vTextureCoord).r - (128.0 / 255.0);
    float v_chroma = texture(plane1, vTextureCoord).g - (128.0 / 255.0);

    vec4 rnd = hash43n(vec3(gl_FragCoord.xy, mod(frame_counter, 1024.0)));

    // Random direction for sampling (no trig)
    vec2 dir = normalize(rnd.xy * 2.0 - 1.0);
    vec2 texel = 1.0 / vec2(textureSize(plane0, 0));
    vec2 offset = dir * deband_range * texel;

    // Sample luma in both directions along the random line
    float y_pos = texture(plane0, vTextureCoord + offset).r;
    float y_neg = texture(plane0, vTextureCoord - offset).r;
    float y_avg = (y_pos + y_neg) * 0.5;

    // Blend toward average only if close to center (preserves edges)
    const float threshold = 0.02;
    float diff = abs(y_avg - y_center);
    float blend = smoothstep(threshold, 0.0, diff);
    float y = mix(y_center, y_avg, blend) - (16.0 / 255.0);

    vec3 rgb;
    rgb.r = 1.1644 * y + 1.7927 * v_chroma;
    rgb.g = 1.1644 * y - 0.2132 * u_chroma - 0.5329 * v_chroma;
    rgb.b = 1.1644 * y + 2.1124 * u_chroma;

    rgb = clamp(rgb, 0.0, 1.0);

    // Tiny TPDF grain (1 LSB) to prevent re-banding after smoothing
    vec3 grain = (rnd.xyz + rnd.yzw - 1.0) / 255.0;
    rgb += grain;

    outColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
