#ifndef QLCG_LOT_GRID_SHADER_H
#define QLCG_LOT_GRID_SHADER_H
#define QLCG_LOT_GRID_SHADER \
    "uniform vec4 uLotGrid;\n" \
    "float lotGridLine(vec2 local,vec2 pixels,float stepSize,float weight) {\n" \
    "    vec2 gap=abs(fract(local/stepSize+.5)-.5)*stepSize/pixels;\n" \
    "    return 1.0-smoothstep(weight*.35,weight+.35,min(gap.x,gap.y));\n" \
    "}\n" \
    "vec3 lotGroundGrid(vec3 col,vec2 pos) {\n" \
    "    if(uLotGrid.z<.5) return col;\n" \
    "    vec2 local=pos-uLotGrid.xy;\n" \
    "    vec2 pixels=max(fwidth(local),vec2(.002));\n" \
    "    if(any(lessThan(local,vec2(0.0))) || any(greaterThan(local,vec2(32.0)))) return col;\n" \
    "    bool core=all(greaterThanEqual(local,vec2(8.0))) && all(lessThanEqual(local,vec2(24.0)));\n" \
    "    if(core) col=mix(col,vec3(.80,.72,.40),.035);\n" \
    "    if(uLotGrid.w>.5) {\n" \
    "        float dashed=step(.38,fract((local.x+local.y)*3.0));\n" \
    "        col=mix(col,vec3(.80,.88,.78),.38*dashed*lotGridLine(local,pixels,.5,.48));\n" \
    "    }\n" \
    "    col=mix(col,vec3(.63,.84,.73),.68*lotGridLine(local,pixels,2.0,.63));\n" \
    "    return mix(col,vec3(.98,.76,.38),.88*lotGridLine(local,pixels,8.0,core?1.45:1.07));\n" \
    "}\n"
#endif
