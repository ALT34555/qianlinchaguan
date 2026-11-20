#ifndef QLCG_EQUIPMENT_SHADERS_H
#define QLCG_EQUIPMENT_SHADERS_H

#define QLCG_EQUIPMENT_VISION \
    "uniform vec4 uLotLights[4];\n" \
    "uniform int uLotLightCount;\n" \
    "uniform vec4 uEquipmentBeam;\n" \
    "uniform vec3 uEquipmentOriginDelta;\n" \
    "uniform float uEquipmentNightVision, uEquipmentAquaticVision;\n" \
    "float qlcgWaterFog(float dist) {\n" \
    "    return clamp((dist - 1.0) / mix(35.0, 150.0, uEquipmentAquaticVision), 0.0, 1.0);\n" \
    "}\n" \
    "vec3 qlcgNightVision(vec3 col) {\n" \
    "    if (uEquipmentNightVision <= 0.0) return col;\n" \
    "    vec3 lifted = sqrt(max(col, vec3(0.0))) * vec3(0.76, 1.14, 0.85) + vec3(0.015, 0.036, 0.018);\n" \
    "    return mix(col, lifted, 0.84 * uEquipmentNightVision);\n" \
    "}\n" \
    "vec3 qlcgEquipmentLight(vec3 col, vec3 ray, float dist) {\n" \
    "    for(int i=0;i<4;i++) {\n" \
    "        if(i>=uLotLightCount) break;\n" \
    "        float glow=1.0-smoothstep(0.3,6.0,length(ray-uLotLights[i].xyz));\n" \
    "        col+=vec3(0.56,0.24,0.075)*glow*glow*uLotLights[i].w;\n" \
    "    }\n" \
    "    ray -= uEquipmentOriginDelta;dist = length(ray);\n" \
    "    if (uEquipmentBeam.w > 0.0 && dist < 3.0) {\n" \
    "        float aim = dot(ray, uEquipmentBeam.xyz) / max(dist, 0.001);\n" \
    "        float beam = uEquipmentBeam.w * smoothstep(0.77, 0.94, aim) * (1.0 - smoothstep(1.0, 3.0, dist));\n" \
    "        vec3 warm = sqrt(max(col, vec3(0.0))) * vec3(1.0, 0.90, 0.74) + vec3(0.11, 0.085, 0.055);\n" \
    "        col = mix(col, max(col, warm), beam);\n" \
    "    }\n" \
    "    return qlcgNightVision(col);\n" \
    "}\n"

#endif
