#version 460 core

// Procedural sky: fullscreen triangle (no vertex buffer), gradient + sun glow.
out vec2 vNdc;

void main()
{
    // Fullscreen triangle from gl_VertexID (0, 1, 2).
    vNdc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0); // z = w -> depth 1.0 (far plane)
}
