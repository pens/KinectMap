cbuffer cb : register(b0) {
    //float hfov, vfov;
    int mode;
    float3 lightDir;
}
Texture2D<float> Depth : register(t0);
Texture2D<float2> DepthToColor : register(t1);
Texture2D Color : register(t2);
Texture2D<float> Infrared : register(t3);
RWTexture2D<float4> RT : register(u0);

float3 getPos(uint2 id);
float colorDepth(uint2 id);
float3 irNormal(uint2 id);
float3 depthNormal(uint2 id);
float3 colorNormal(uint2 id);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 inID = id.xy;
    if (id.x < 448 || id.x >= 1472 || id.y < 28 || id.y >= 1052) {
        RT[id.xy] = Color[id.xy];
        return;
    }
    switch (mode) {
    case 0: //Color
        float3 color = Color[inID];
        float grey = (color.x + color.y + color.z) / 3;
        float max = .95;
        if (grey > max)
            color *= max / grey;
        float min = .05;
        if (grey < min)
            color *= min / grey;
        RT[id.xy] = float4(color, 1);
        break;
    case 1: //Color-Space Depth
        RT[id.xy] = Depth[DepthToColor[inID]] * 10;
        break;
    case 2: //Color-Space Infrared
        RT[id.xy] = Infrared[DepthToColor[inID]] * 10;
        break;
    case 3: //Position
        RT[id.xy] = float4(getPos(DepthToColor[inID]), 1) * 10;
        break;
    case 4: //Normal (calculated in depth-space)
        float3 s = depthNormal(inID);
        RT[id.xy] = (float4(s, 1) + 1) / 2;
        break;
    case 5: {
        float3 s = colorNormal(inID);
        RT[id.xy] = (float4(s, 1) + 1) / 2;
        break;
    }
    case 6: {
        float3 s = irNormal(inID);
        RT[id.xy] = (float4(s, 1) + 1) / 2;
        break;

    }
    case 7: {
        float3 s = colorNormal(inID);
        s = normalize(s);
        /*float3 s2 = float3(0, 0, 0);
        for (int i = -5; i <= 5; ++i) {
        for (int j = -5; j <= 5; ++j) {
        if (i == 0 || j == 0) continue;
        s2 += depthNormal(inID + uint2(i, j));
        }
        }
        s2 = normalize(s2);

        float3 diff = s2 - s;*/

        float3 s2 = depthNormal(inID);

        RT[id.xy] = float4((normalize(s + s2) + 1) / 2, 1);
        break;
    }
    case 8: {
        float3 s = normalize(depthNormal(inID));

        float3 lightColor = float3(1, 1, 1);
        float lambert = dot(s, -normalize(lightDir));
        if (lambert < 0) {
            RT[id.xy] = 0;
            break;
        }
        float3 lint = .05 + lambert / 3.14159;

        float3 V = float3(0, 0, 1);
        float3 L = -normalize(lightDir);
        float3 H = normalize(L + V);
        float3 N = s;

        float roughness = 1;
        float a2 = pow(roughness, 4);
        float3 F0 = .04 * Color[inID];
        float k = pow(roughness, 2) / 2;

        float NdH = saturate(dot(N, H));
        float VdH = saturate(dot(V, H));
        float NdV = saturate(dot(N, V));
        float NdL = saturate(dot(N, L));

        float D = 1 / pow(NdH * NdH * (a2 - 1) + 1, 2);
        float3 F = F0 + (1 - F0) * exp2((-5.55473 * VdH - 6.98316) * VdH);
        float Gv = a2 / (4 * (NdV * (1 - k) + k));
        float G = Gv / (NdL * (1 - k) + k);
        
        lint += D * F * G;

        RT[id.xy] = lambert > 0 ? float4(Color[inID].xyz * lint * lightColor * 5, 1) : 0;
        break;
    }
    default: {
        RT[id.xy] = 1;
        float cutoff = .05;
        float3 diff = Color[inID + uint2(1, 0)].xyz - Color[inID].xyz;
        if ((diff.x + diff.y + diff.z) / 3 > cutoff)
            RT[id.xy] = 0;
        diff = Color[inID + uint2(0, 1)].xyz - Color[inID].xyz;
        if ((diff.x + diff.y + diff.z) / 3 > cutoff)
            RT[id.xy] = 0; 
        diff = Color[inID + uint2(-1, 0)].xyz - Color[inID].xyz;
        if ((diff.x + diff.y + diff.z) / 3 > cutoff)
            RT[id.xy] = 0; 
        diff = Color[inID + uint2(0, -1)].xyz - Color[inID].xyz;
        if ((diff.x + diff.y + diff.z) / 3 > cutoff)
            RT[id.xy] = 0;
        break;
    }
    }
}

float3 getPos(uint2 id) {
    float3 depth = float3(id.x, id.y, Depth[id]);
    float3 pos;
    pos.z = depth.z;
    pos.x = -(depth.x - 256) * pos.z / (256 / tan(84.1 / 2));
    pos.y = -(depth.y - 212) * pos.z / (212 / tan(53.8 / 2));
    return pos;
}

float colorDepth(uint2 id) {
    return (Color[id].x + Color[id].y + Color[id].z) / 3;
}

float3 irNormal(uint2 id) {
    float scale = 40;
    float3 u = float3(1, 0, scale * (Infrared[(uint2)DepthToColor[id] + uint2(1, 0)] - Infrared[(uint2)DepthToColor[id]]));
    float3 v = float3(0, 1, scale * (Infrared[(uint2)DepthToColor[id] + uint2(0, 1)] - Infrared[(uint2)DepthToColor[id]]));
    float3 s = cross(u, v);
    if (s.z < 0)
        s.z *= -1;
    return normalize(s);
}

float3 depthNormal(uint2 id) {
    float3 u = getPos((uint2)DepthToColor[id] + uint2(0, 1)) - getPos((uint2)DepthToColor[id]);
    float3 v = getPos((uint2)DepthToColor[id] + uint2(1, 0)) - getPos((uint2)DepthToColor[id]);
    float3 s = cross(u, v);
    if (getPos((uint2)DepthToColor[id]).z == 0 ||
        getPos((uint2)DepthToColor[id] + uint2(0, 1)).z == 0 ||
        getPos((uint2)DepthToColor[id] + uint2(1, 0)).z == 0) {
        return 0;
    } else {
        if (s.z < 0)
            s.z *= -1;
        return normalize(s);
    }
}

float3 colorNormal(uint2 id) {
    float scale = 5;
    float3 u = float3(1, 0, scale * (colorDepth(id + uint2(1, 0)) - colorDepth(id)));
    float3 v = float3(0, 1, scale * (colorDepth(id + uint2(0, 1)) - colorDepth(id)));
    float3 s = cross(u, v);
    return normalize(s);
}