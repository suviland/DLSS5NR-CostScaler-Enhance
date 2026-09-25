// ================================================================================================
// DLSS-NR Proxy Shaders: Area-Weighted Downsample + High-Frequency Matched Residual Resolve
// ================================================================================================
// 本文件包含两个 compute shader（由 build.bat 用 fxc 编译成 *_Shader.h 内嵌进 DLL）：
//
//   CS_Downsample : 原生帧 → work 分辨率低分辨率输入（TMU 双线性，一步到位）。
//   CS_Resolve    : 合成输出。Mode 0 = 双线性直接放大；Mode 1 = 「编辑量」模式：
//                   edit = outputSmall - colorSmall（NR 网络学到的去噪/增强差值），
//                   经锐化/强度缩放后叠加回原生帧。isSkipFrame（VRNR 隔帧）时按
//                   亮度差把陈旧编辑量淡出，运动处自然回落到干净原生像素。
//
// 常量块与 proxy_main.cpp 的 ResolveConstants/DownsampleConstants 严格对应，
// 改任何一边必须同步另一边（字段顺序 = cbuffer 布局）。
// ⚠️ 教训：曾在此加过 CS_Motion / 运动矢量混合（VRNR 2.0），因显存与稳定性问题
//    已回退；重做类似功能前先读 AI_ASSISTANT_GUIDE.md 铁律 2。
// ================================================================================================

// --- DOWNSAMPLE SHADER ---
cbuffer DownConstants : register(b0)
{
    uint gSrcWidth;
    uint gSrcHeight;
    uint gDstWidth;
    uint gDstHeight;
};

Texture2D<float4>   gDownSource  : register(t0);
RWTexture2D<float4> gDownTarget  : register(u0);
SamplerState        gLinearClamp : register(s0);

[numthreads(8, 8, 1)]
void CS_Downsample(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gDstWidth || id.y >= gDstHeight)
        return;

    // Hardware TMU bilinear downsampling (ultra-fast, zero-ALU overhead)
    float2 uv = (float2(id.xy) + 0.5f) / float2(gDstWidth, gDstHeight);
    gDownTarget[id.xy] = gDownSource.SampleLevel(gLinearClamp, uv, 0);
}


// --- RESOLVE & RESIDUAL COMPOSITE SHADER ---
cbuffer ResolveConstants : register(b0)
{
    uint  gNativeWidth;
    uint  gNativeHeight;
    uint  gWorkWidth;
    uint  gWorkHeight;
    float gTransferStrength;
    float gSharpness;
    uint  gEnlargementMode;
    float gColorStrength;
    uint  gIsSkipFrame;
    uint  gHasDepth;
    float gVrnrRamp;   // 0.6.3 防闪烁 P0：连续 skip 帧的 edit 强度坡道（1.0 = 满强度；0.7.1 起连续 ≥2 才衰减）
    uint  gVrnrAf;     // 0.6.3 防闪烁总开关（0 = 与 0.6.2 行为完全一致）
    // 0.7.1 VRNR 防闪烁 v2（实验）——与 proxy_main.cpp ResolveConstants 尾部严格对应
    float gVrnrFloor;      // skip 帧 edit 权重下限（0 = 关闭下限）
    uint  gVrnrReproject;  // 1 = 用 MV 把陈旧神经输入对齐到本帧再算亮度差
    uint  gHasMVec;        // 本帧游戏运动矢量纹理是否可用
    float gMvScaleX;       // 原始 MV → native 像素
    float gMvScaleY;
    // 0.7.3 帧时自适应强度（实验）——与 proxy_main.cpp ResolveConstants 尾部严格对应。
    // CPU 侧由 EWMA 帧率算出（0=不衰减，1=全减光），skip 帧恒为 0。
    float gAdaptDim;
};

Texture2D<float4>   gSmallInput    : register(t0); // Downsampled model input (g_colorSmall)
Texture2D<float4>   gSmallOutput   : register(t1); // Model output (g_outputSmall)
Texture2D<float4>   gNativeColor   : register(t2); // Pristine native frame (origColor)
Texture2D<float>    gDepth         : register(t3); // Native depth buffer (if available)
Texture2D           gMVec          : register(t4); // Game motion vectors, RG float（0.7.1）
RWTexture2D<float4> gResolveTarget : register(u0); // Destination (origOutput)

SamplerState gLinear : register(s0);

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);
groupshared float3 s_nativeTile[10][10];

// 0.7.1 VRNR 重投影采样：用运动矢量把 2 帧前的神经输入对齐到本帧像素位置。
// DLSS MV 符号约定存在两种实现方向，这里取「对齐后亮度差更小」的一支，
// 因此无需关心 MV 正负号——对齐错了自然退回原始采样（错位更大 diff 更大）。
float3 VrnrAlignedInput(float2 uv, float3 inRaw, float lumaNative)
{
    float2 mv = gMVec.SampleLevel(gLinear, uv, 0).xy;
    float2 prevUV = uv + (mv * float2(gMvScaleX, gMvScaleY)) / float2(gNativeWidth, gNativeHeight);
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
        return inRaw;
    float3 inPrev = gSmallInput.SampleLevel(gLinear, prevUV, 0).rgb;
    float dRaw  = abs(dot(max(inRaw,  0.0), kLuma) - lumaNative);
    float dPrev = abs(dot(max(inPrev, 0.0), kLuma) - lumaNative);
    return (dPrev < dRaw) ? inPrev : inRaw;
}

[numthreads(8, 8, 1)]
void CS_Resolve(uint3 id : SV_DispatchThreadID, uint3 tid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    // Cooperative loading of 10x10 apron into LDS (eliminates redundant VRAM reads during RCAS)
    int2 baseCoord = int2(gid.xy * 8) - 1;
    int2 maxCoord = int2((int)gNativeWidth - 1, (int)gNativeHeight - 1);
    uint linearThreadId = tid.y * 8 + tid.x;

    int2 c0 = clamp(baseCoord + int2((int)(linearThreadId % 10), (int)(linearThreadId / 10)), int2(0, 0), maxCoord);
    s_nativeTile[linearThreadId / 10][linearThreadId % 10] = gNativeColor.Load(int3(c0, 0)).rgb;

    if (linearThreadId < 36)
    {
        uint idx1 = linearThreadId + 64;
        int2 c1 = clamp(baseCoord + int2((int)(idx1 % 10), (int)(idx1 / 10)), int2(0, 0), maxCoord);
        s_nativeTile[idx1 / 10][idx1 % 10] = gNativeColor.Load(int3(c1, 0)).rgb;
    }

    GroupMemoryBarrierWithGroupSync();

    if (id.x >= gNativeWidth || id.y >= gNativeHeight)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(gNativeWidth, gNativeHeight);

    // Mode 0: Direct Neural Reconstruction with RCAS (Recommended for DLSS-NR / Ray Reconstruction)
    if (gEnlargementMode == 0)
    {
        float4 outSample = gSmallOutput.SampleLevel(gLinear, uv, 0);
        float3 result = outSample.rgb;
        float3 original = s_nativeTile[tid.y + 1][tid.x + 1];

        // Blend with native if TransferStrength < 1.0 or on skip frames
        if (gIsSkipFrame != 0)
        {
            float3 inSample = gSmallInput.SampleLevel(gLinear, uv, 0).rgb;
            float lumaIn = dot(max(inSample, 0.0), kLuma);
            float lumaNative = dot(max(original, 0.0), kLuma);
            float diff = abs(lumaIn - lumaNative);
            float weight = saturate(1.0 - (diff * 2.0) / (lumaIn + lumaNative + 0.05));

            // 0.6.3 防闪烁（P1 空间平滑）：亮度差改用 3x3 邻域平均值，
            // 原生噪点不再逐帧驱动权重抖动（呼吸感根源）。
            if (gVrnrAf != 0)
            {
                bool useReproject = (gVrnrReproject != 0) && (gHasMVec != 0);
                if (useReproject)
                {
                    // 0.7.1（实验）MV 重投影：对齐采样消除「错位画面误判运动」，
                    // 与原始采样取权重较大者（= 亮度差较小者），符号约定无关。
                    float3 inPrev = VrnrAlignedInput(uv, inSample, lumaNative);
                    float lumaPrev = dot(max(inPrev, 0.0), kLuma);
                    float wPrev = saturate(1.0 - (abs(lumaPrev - lumaNative) * 2.0) / (lumaPrev + lumaNative + 0.05));
                    weight = max(weight, wPrev);
                }
                else
                {
                    float2 pxAf = 1.0 / float2(gNativeWidth, gNativeHeight);
                    float3 iE = gSmallInput.SampleLevel(gLinear, uv + float2( pxAf.x, 0), 0).rgb;
                    float3 iW = gSmallInput.SampleLevel(gLinear, uv + float2(-pxAf.x, 0), 0).rgb;
                    float3 iS = gSmallInput.SampleLevel(gLinear, uv + float2(0,  pxAf.y), 0).rgb;
                    float3 iN = gSmallInput.SampleLevel(gLinear, uv + float2(0, -pxAf.y), 0).rgb;
                    float3 inAvg  = (inSample + iE + iW + iS + iN) * 0.2;
                    float3 natAvg = (original
                        + s_nativeTile[tid.y + 1][tid.x + 2] + s_nativeTile[tid.y + 1][tid.x + 0]
                        + s_nativeTile[tid.y + 2][tid.x + 1] + s_nativeTile[tid.y + 0][tid.x + 1]) * 0.2;
                    float lumaInAvg  = dot(max(inAvg, 0.0), kLuma);
                    float lumaNatAvg = dot(max(natAvg, 0.0), kLuma);
                    float diffAvg = abs(lumaInAvg - lumaNatAvg);
                    weight = saturate(1.0 - (diffAvg * 2.0) / (lumaInAvg + lumaNatAvg + 0.05));
                }
                // P0 时间坡道（0.7.1：连续 skip ≥2 帧才衰减，交替模式不再打 0.9 折）
                weight *= gVrnrRamp;
                // 0.7.1（实验）权重下限：运动区 edit 不整段归零，恢复帧不突变
                weight = max(weight, gVrnrFloor);
            }

            result = lerp(original, result, weight * saturate(gTransferStrength));
        }
        // 0.7.3 帧时自适应：低帧率时推理帧 edit 减光，缩小与 skip 帧的视觉差。
        // gAdaptDim=0 时 lerp 系数 = saturate(gTransferStrength)，行为与 0.7.1 一致。
        else if (gTransferStrength < 0.999 || gAdaptDim > 0.001)
        {
            result = lerp(original, result, saturate(gTransferStrength) * (1.0 - gAdaptDim));
        }

        // Contrast-adaptive edge sharpening (RCAS) on denoised features
        if (gSharpness > 0.001)
        {
            float2 px = float2(1.0 / (float)gNativeWidth, 1.0 / (float)gNativeHeight);
            float3 cE = gSmallOutput.SampleLevel(gLinear, uv + float2( px.x, 0), 0).rgb;
            float3 cW = gSmallOutput.SampleLevel(gLinear, uv + float2(-px.x, 0), 0).rgb;
            float3 cS = gSmallOutput.SampleLevel(gLinear, uv + float2(0,  px.y), 0).rgb;
            float3 cN = gSmallOutput.SampleLevel(gLinear, uv + float2(0, -px.y), 0).rgb;

            float lE = dot(cE, kLuma);
            float lW = dot(cW, kLuma);
            float lS = dot(cS, kLuma);
            float lN = dot(cN, kLuma);
            float lM = dot(result, kLuma);

            float minL = min(lM, min(min(lE, lW), min(lS, lN)));
            float maxL = max(lM, max(max(lE, lW), max(lS, lN)));

            float range = maxL - minL;
            if (range > 1e-5)
            {
                float3 crossAvg = (cE + cW + cS + cN) * 0.25;
                float3 highFreq = result - crossAvg;
                float adaptiveScale = saturate(1.0 - range / (maxL + 1e-4));
                float rcasWeight = saturate(gSharpness) * (0.2 + 0.8 * adaptiveScale);
                result = max(result + highFreq * rcasWeight, 0.0);
            }
        }

        float nativeAlpha = gNativeColor.Load(int3(id.xy, 0)).a;
        gResolveTarget[id.xy] = float4(max(result, 0.0), nativeAlpha);
        return;
    }

    // Mode 1: Matched Residual (1:1 Native Resolution Anchor + Scaled Neural Delta)
    // 1. Pristine 1:1 Native Game Pixel loaded directly from on-chip LDS tile
    float3 original = s_nativeTile[tid.y + 1][tid.x + 1];

    // 2. Sample neural input and output at standard screen UV
    float3 smallInput = gSmallInput.SampleLevel(gLinear, uv, 0).rgb;
    float3 smallOutput = gSmallOutput.SampleLevel(gLinear, uv, 0).rgb;

    // 3. Compute neural delta / edit
    float3 edit = smallOutput - smallInput;

    // Chroma vs Luma control for ColorStrength (上游 15f09dd 色相保持重构):
    // At ColorStrength = 1.0: Full neural color delta (indirect bounce lighting & material colors).
    // At ColorStrength = 0.0: Proportional luminance scaling that strictly preserves the native pixel's
    // original hue and saturation, eliminating chalky/pastel desaturation in vibrant lighting and skin.
    float editLuma = dot(edit, kLuma);
    float origLuma = dot(max(original, 0.0), kLuma);

    // Normalized color direction (chromaticity vector) with smooth fade for sub-blacks
    float3 lumaDir = lerp(float3(1.0, 1.0, 1.0), original / (origLuma + 1e-5), saturate(origLuma * 50.0));
    float3 lumaEdit = lumaDir * editLuma;

    // Safety guard: prevent shadow inversions and clamp extreme specular spikes
    lumaEdit = clamp(lumaEdit, -original * 0.95, max(original * 2.5, 2.0));

    float3 controlledEdit = lerp(lumaEdit, edit, saturate(gColorStrength));

    // Depth-Aware Bilateral Silhouette Preservation:
    // If native depth is available, detect geometric silhouette discontinuities and prevent
    // low-res neural radiance deltas from bleeding across thin foreground edges.
    if (gHasDepth != 0)
    {
        float nativeDepth = gDepth.Load(int3(id.xy, 0)).r;
        float dE = gDepth.Load(int3(min(id.x + 1, (uint)maxCoord.x), id.y, 0)).r;
        float dW = gDepth.Load(int3(max((int)id.x - 1, 0), id.y, 0)).r;
        float dS = gDepth.Load(int3(id.x, min(id.y + 1, (uint)maxCoord.y), 0)).r;
        float dN = gDepth.Load(int3(id.x, max((int)id.y - 1, 0), 0)).r;

        float minD = min(nativeDepth, min(min(dE, dW), min(dS, dN)));
        float maxD = max(nativeDepth, max(max(dE, dW), max(dS, dN)));
        float depthRange = (maxD - minD) / (maxD + 1e-4);

        if (depthRange > 0.02)
        {
            float edgeWeight = saturate(1.0 - (depthRange - 0.02) * 20.0);
            controlledEdit *= lerp(0.25, 1.0, edgeWeight);
        }
    }

    // Apply TransferStrength
    float3 scaledEdit = controlledEdit * gTransferStrength;

    // 0.7.3 帧时自适应（实验）：低帧率时推理帧 edit 减光，缩小与 skip 帧的
    // 视觉差（脉动根源 = 推理帧满强度 vs skip 帧陈旧 edit×低权重）。
    // gAdaptDim 由 CPU 按帧率算出，skip 帧 CPU 已置 0，此处再按 gIsSkipFrame 兜底。
    if (gIsSkipFrame == 0 && gAdaptDim > 0.001)
        scaledEdit *= (1.0 - gAdaptDim);

    // On skip frames, detect motion/edge transitions by comparing cached neural input with fresh native color.
    // When scene content moves, smoothly fade the stale delta so the pixel displays the clean 1:1 native game pixel!
    if (gIsSkipFrame != 0)
    {
        float origLuma = dot(max(original, 0.0), kLuma);
        float inLuma   = dot(max(smallInput, 0.0), kLuma);
        float diff     = abs(inLuma - origLuma);
        float weight   = saturate(1.0 - (diff * 2.5) / (origLuma + inLuma + 0.05));

        // 0.6.3 防闪烁（P1 空间平滑）：与 Mode 0 相同——邻域平均消噪抖。
        // native 邻居直接复用 LDS tile（RCAS 也要用，零额外显存读取）。
        if (gVrnrAf != 0)
        {
            bool useReproject = (gVrnrReproject != 0) && (gHasMVec != 0);
            if (useReproject)
            {
                // 0.7.1（实验）MV 重投影：对齐采样，与原始采样取权重较大者
                float3 inPrev = VrnrAlignedInput(uv, smallInput, origLuma);
                float lumaPrev = dot(max(inPrev, 0.0), kLuma);
                float wPrev = saturate(1.0 - (abs(lumaPrev - origLuma) * 2.5) / (origLuma + lumaPrev + 0.05));
                weight = max(weight, wPrev);
            }
            else
            {
                float nE = dot(max(s_nativeTile[tid.y + 1][tid.x + 2], 0.0), kLuma);
                float nW = dot(max(s_nativeTile[tid.y + 1][tid.x + 0], 0.0), kLuma);
                float nS = dot(max(s_nativeTile[tid.y + 2][tid.x + 1], 0.0), kLuma);
                float nN = dot(max(s_nativeTile[tid.y + 0][tid.x + 1], 0.0), kLuma);
                float2 pxAf = 1.0 / float2(gNativeWidth, gNativeHeight);
                float iE = dot(max(gSmallInput.SampleLevel(gLinear, uv + float2( pxAf.x, 0), 0).rgb, 0.0), kLuma);
                float iW = dot(max(gSmallInput.SampleLevel(gLinear, uv + float2(-pxAf.x, 0), 0).rgb, 0.0), kLuma);
                float iS = dot(max(gSmallInput.SampleLevel(gLinear, uv + float2(0,  pxAf.y), 0).rgb, 0.0), kLuma);
                float iN = dot(max(gSmallInput.SampleLevel(gLinear, uv + float2(0, -pxAf.y), 0).rgb, 0.0), kLuma);
                float oAvg = (origLuma + nE + nW + nS + nN) * 0.2;
                float iAvg = (inLuma  + iE + iW + iS + iN) * 0.2;
                float diffAvg = abs(iAvg - oAvg);
                weight = saturate(1.0 - (diffAvg * 2.5) / (oAvg + iAvg + 0.05));
            }
            // P0 时间坡道（0.7.1：连续 skip ≥2 帧才衰减，交替模式不再打 0.9 折）
            weight *= gVrnrRamp;
            // 0.7.1（实验）权重下限：运动区 edit 不整段归零，恢复帧不突变
            weight = max(weight, gVrnrFloor);

            // 0.7.1（实验）剪影保护：深度不连续处（人物/物体边缘）权重压到最低，
            // 陈旧 edit 不会越过轮廓产生鬼影——边缘干净回落到原生像素。
            if (gHasDepth != 0)
            {
                float sdC = gDepth.Load(int3(id.xy, 0)).r;
                float sdE = gDepth.Load(int3(min(id.x + 1, (uint)maxCoord.x), id.y, 0)).r;
                float sdW = gDepth.Load(int3(max((int)id.x - 1, 0), id.y, 0)).r;
                float sdS = gDepth.Load(int3(id.x, min(id.y + 1, (uint)maxCoord.y), 0)).r;
                float sdN = gDepth.Load(int3(id.x, max((int)id.y - 1, 0), 0)).r;
                float sdMin = min(sdC, min(min(sdE, sdW), min(sdS, sdN)));
                float sdMax = max(sdC, max(max(sdE, sdW), max(sdS, sdN)));
                float sdRange = (sdMax - sdMin) / (sdMax + 1e-4);
                if (sdRange > 0.02)
                    weight = min(weight, 0.10);
            }
        }

        scaledEdit *= weight;
    }

    // Base native frame + scaled neural delta
    float3 result = max(original + scaledEdit, 0.0);

    // 4. HDR highlight & shadow guard using luminance ratio
    // 0.6.3 防闪烁（P2 守恒对称化）：skip 帧同样执行——比例由上一 NR 帧的
    // 输入/输出算出，与本帧一致，保证高光区两条链路亮度一致，消除钳制闪烁。
    if (gIsSkipFrame == 0 || gVrnrAf != 0)
    {
        float origLuma = dot(max(original, 0.0), kLuma);
        float inLuma   = dot(max(smallInput, 0.0), kLuma);
        float outLuma  = dot(max(smallOutput, 0.0), kLuma);

        const float kFloor = 1.0 / 512.0;
        float lumaRatio = (outLuma + kFloor) / (inLuma + kFloor);

        float resLuma = dot(result, kLuma);
        if (resLuma > 1e-5 && inLuma > 1e-5)
        {
            float targetLuma = origLuma * lumaRatio;
            float maxAllowedLuma = max(origLuma * 2.5, targetLuma * 1.5 + 0.1);
            if (resLuma > maxAllowedLuma)
            {
                result *= (maxAllowedLuma / resLuma);
            }
        }
    }

    // 5. RCAS (Robust Contrast-Adaptive Sharpening) using on-chip LDS tile (zero global VRAM reads!)
    if (gSharpness > 0.001)
    {
        float3 cE = s_nativeTile[tid.y + 1][tid.x + 2];
        float3 cW = s_nativeTile[tid.y + 1][tid.x + 0];
        float3 cS = s_nativeTile[tid.y + 2][tid.x + 1];
        float3 cN = s_nativeTile[tid.y + 0][tid.x + 1];

        float lE = dot(cE, kLuma);
        float lW = dot(cW, kLuma);
        float lS = dot(cS, kLuma);
        float lN = dot(cN, kLuma);
        float lM = dot(max(original, 0.0), kLuma);

        float minL = min(lM, min(min(lE, lW), min(lS, lN)));
        float maxL = max(lM, max(max(lE, lW), max(lS, lN)));

        float range = maxL - minL;
        if (range > 1e-5)
        {
            float3 crossAvg = (cE + cW + cS + cN) * 0.25;
            float3 highFreq = original - crossAvg;
            float adaptiveScale = saturate(1.0 - range / (maxL + 1e-4));
            float rcasWeight = saturate(gSharpness) * (0.2 + 0.8 * adaptiveScale);
            result = max(result + highFreq * rcasWeight, 0.0);
        }
    }

    float nativeAlpha = gNativeColor.Load(int3(id.xy, 0)).a;
    gResolveTarget[id.xy] = float4(result, nativeAlpha);
}
