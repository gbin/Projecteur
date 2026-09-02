#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 outputSize;
    float dotSize;
    float time;
    vec4 dotColor;
};

float hash(vec2 position)
{
    vec3 p = fract(vec3(position.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void main()
{
    vec2 pixel = qt_TexCoord0 * outputSize;
    vec2 center = outputSize * 0.5;
    float radius = max(1.5, dotSize * 0.5);
    float distanceFromCenter = length(pixel - center);

    float frame = floor(time * 32.0);
    float fineNoise = hash(floor(pixel * 0.8) + frame * vec2(7.0, 13.0));
    float slowNoise = hash(floor(pixel * 0.32) + floor(time * 15.0));
    float corePulse = 0.88 + 0.12 * sin(time * 37.0 + fineNoise * 6.28318);

    float core = (1.0 - smoothstep(radius * 0.62, radius, distanceFromCenter)) * corePulse;
    float haze = exp(-0.5 * pow(distanceFromCenter / (radius * 1.45), 2.0)) * 0.42;
    float speckle = pow(fineNoise, 9.0) * slowNoise
                  * exp(-0.5 * pow(distanceFromCenter / (radius * 1.9), 2.0)) * 0.8;
    float alpha = clamp(max(core, haze) + speckle, 0.0, 1.0) * dotColor.a;

    fragColor = vec4(dotColor.rgb * alpha, alpha) * qt_Opacity;
}
