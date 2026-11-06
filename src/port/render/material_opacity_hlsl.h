#pragma once

// Shared by color, depth and motion shaders. Codes below16 retain the PC
// pipeline; native codes are16+ShadInstr+IgnoreTexAlpha*4+UseAlpha*8.
#define HOTD2_MATERIAL_OPACITY_HLSL R"(
float SourceTextureAlpha(float sampledAlpha, float code) {
    return code >= 16 && (((int)code - 16) & 4) != 0 ? 1.0 : sampledAlpha;
}
float SourceMaterialAlpha(float sampledAlpha, float faceAlpha, float code, bool textured) {
    if (code >= 16) {
        int state = (int)code - 16;
        float vertexAlpha = (state & 8) != 0 ? faceAlpha : 1.0;
        if (!textured) return vertexAlpha;
        float textureAlpha = SourceTextureAlpha(sampledAlpha, code);
        int instruction = state & 3;
        if (instruction < 2) return textureAlpha;
        return instruction == 2 ? vertexAlpha : vertexAlpha * textureAlpha;
    }
    return textured ? (code > 1.5 ? sampledAlpha : sampledAlpha * faceAlpha) : faceAlpha;
}
)"
