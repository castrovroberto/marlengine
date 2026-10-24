#version 330 core
layout (location = 0) out vec4 FragColor;

in vec4 v_Color;
in vec2 v_TexCoord;
flat in float v_TexIndex;
flat in float v_Circle;   // 0 for an ordinary quad; for a circle, the ring width as a fraction of the radius (1 = filled)

uniform sampler2D u_Textures[16];

void main() {
    if (v_Circle > 0.0) {
        // A circle: the quad's texture coordinates run 0 to 1 across it, so the distance from its center, as a
        // fraction of the radius, is d. The edge fades over one pixel (fwidth) whatever the zoom.
        float d = length(v_TexCoord * 2.0 - 1.0);
        float aa = max(fwidth(d), 1e-4);
        float outer = 1.0 - smoothstep(1.0 - aa, 1.0, d);
        float inner = v_Circle >= 1.0 ? 1.0 : smoothstep(1.0 - v_Circle - aa, 1.0 - v_Circle, d);
        FragColor = vec4(v_Color.rgb, v_Color.a * outer * inner);
        return;
    }
    // GLSL 3.30 only allows constant sampler-array indices, hence the switch.
    vec4 texColor = vec4(1.0);
    switch (int(v_TexIndex + 0.5)) {
        case 0: texColor = texture(u_Textures[0], v_TexCoord); break;
        case 1: texColor = texture(u_Textures[1], v_TexCoord); break;
        case 2: texColor = texture(u_Textures[2], v_TexCoord); break;
        case 3: texColor = texture(u_Textures[3], v_TexCoord); break;
        case 4: texColor = texture(u_Textures[4], v_TexCoord); break;
        case 5: texColor = texture(u_Textures[5], v_TexCoord); break;
        case 6: texColor = texture(u_Textures[6], v_TexCoord); break;
        case 7: texColor = texture(u_Textures[7], v_TexCoord); break;
        case 8: texColor = texture(u_Textures[8], v_TexCoord); break;
        case 9: texColor = texture(u_Textures[9], v_TexCoord); break;
        case 10: texColor = texture(u_Textures[10], v_TexCoord); break;
        case 11: texColor = texture(u_Textures[11], v_TexCoord); break;
        case 12: texColor = texture(u_Textures[12], v_TexCoord); break;
        case 13: texColor = texture(u_Textures[13], v_TexCoord); break;
        case 14: texColor = texture(u_Textures[14], v_TexCoord); break;
        case 15: texColor = texture(u_Textures[15], v_TexCoord); break;
    }
    FragColor = texColor * v_Color;
}
