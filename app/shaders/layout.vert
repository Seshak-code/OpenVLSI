#version 440
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 v_color;
layout(std140, binding = 0) uniform buf { mat4 mvp; float opacity; };

void main() {
    v_color = vec4(color.rgb, color.a * opacity);
    gl_Position = mvp * vec4(position, 0.0, 1.0);
}
