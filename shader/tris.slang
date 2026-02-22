struct VSInput {
    float3 pos;
    float3 color;
};


struct VSOutput {
    float4 pos : SV_POSITION;
    float3 color;
};

[shader("vertex")]
VSOutput main(VSInput input) {
    VSOutput res;
    res.pos = float4(input.pos.xyz, 1.0f);
    res.color = input.color;
    return res;
}

[shader("fragment")]
float4 main(VSOutput input) {

    float4 fragColor = float4(1.0f);
    fragColor = float4(input.color, 1.0f);

    return fragColor;
}