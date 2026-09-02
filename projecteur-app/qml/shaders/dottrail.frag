#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 outputSize;
    float dotSize;
    vec4 dotColor;
    vec2 point0;
    vec2 point1;
    vec2 point2;
    vec2 point3;
    vec2 point4;
    vec2 point5;
    vec2 point6;
};

float segmentDistance(vec2 pixel, vec2 start, vec2 end)
{
    vec2 segment = end - start;
    float lengthSquared = dot(segment, segment);
    float amount = lengthSquared > 0.001
        ? clamp(dot(pixel - start, segment) / lengthSquared, 0.0, 1.0)
        : 0.0;
    return length(pixel - (start + amount * segment));
}

float trailSegment(vec2 pixel, vec2 start, vec2 end, float opacity)
{
    float radius = max(1.0, dotSize * 0.42);
    float coverage = 1.0 - smoothstep(radius * 0.25, radius, segmentDistance(pixel, start, end));
    return coverage * opacity;
}

void main()
{
    vec2 pixel = qt_TexCoord0 * outputSize;
    float alpha = 0.0;
    alpha = max(alpha, trailSegment(pixel, point0, point1, 0.58));
    alpha = max(alpha, trailSegment(pixel, point1, point2, 0.46));
    alpha = max(alpha, trailSegment(pixel, point2, point3, 0.34));
    alpha = max(alpha, trailSegment(pixel, point3, point4, 0.24));
    alpha = max(alpha, trailSegment(pixel, point4, point5, 0.15));
    alpha = max(alpha, trailSegment(pixel, point5, point6, 0.08));
    alpha *= dotColor.a;

    fragColor = vec4(dotColor.rgb * alpha, alpha) * qt_Opacity;
}
