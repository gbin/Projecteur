#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 outputSize;
};

layout(binding = 1) uniform sampler2D source;

vec3 srgbToLinear(vec3 color)
{
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}

vec3 linearToSrgb(vec3 color)
{
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

vec4 unpremultipliedSample(vec2 position)
{
    vec4 sampleColor = texture(source, position);
    if (sampleColor.a > 0.0) {
        sampleColor.rgb /= sampleColor.a;
    }
    return sampleColor;
}

void main()
{
    vec2 pixelStep = vec2(1.0) / outputSize;

    vec4 centerSample = unpremultipliedSample(qt_TexCoord0);
    vec4 northSample = unpremultipliedSample(qt_TexCoord0 - vec2(0.0, pixelStep.y));
    vec4 southSample = unpremultipliedSample(qt_TexCoord0 + vec2(0.0, pixelStep.y));
    vec4 westSample = unpremultipliedSample(qt_TexCoord0 - vec2(pixelStep.x, 0.0));
    vec4 eastSample = unpremultipliedSample(qt_TexCoord0 + vec2(pixelStep.x, 0.0));

    vec3 center = srgbToLinear(centerSample.rgb);
    vec3 north = srgbToLinear(northSample.rgb);
    vec3 south = srgbToLinear(southSample.rgb);
    vec3 west = srgbToLinear(westSample.rgb);
    vec3 east = srgbToLinear(eastSample.rgb);

    vec3 localMinimum = min(center, min(min(north, south), min(west, east)));
    vec3 localMaximum = max(center, max(max(north, south), max(west, east)));
    vec3 localBlur = (4.0 * center + north + south + west + east) / 8.0;

    const vec3 luminanceWeights = vec3(0.2126, 0.7152, 0.0722);
    float minimumLuminance = dot(localMinimum, luminanceWeights);
    float maximumLuminance = dot(localMaximum, luminanceWeights);
    float localContrast = maximumLuminance - minimumLuminance;

    // Keep low-contrast and photographic regions close to the smooth source.
    // Text and UI edges get a bounded high-frequency boost with no overshoot.
    float edgeConfidence = smoothstep(0.015, 0.18, localContrast);
    vec3 enhanced = center + (center - localBlur) * (1.25 * edgeConfidence);
    enhanced = clamp(enhanced, localMinimum, localMaximum);

    vec3 outputColor = linearToSrgb(clamp(enhanced, 0.0, 1.0));
    float outputAlpha = centerSample.a;
    fragColor = vec4(outputColor * outputAlpha, outputAlpha) * qt_Opacity;
}
