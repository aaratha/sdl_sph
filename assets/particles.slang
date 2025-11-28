// =========================================
// Shared structures
// =========================================
[[vk::binding(0, 0)]] RWStructuredBuffer<float> gXCurr;
[[vk::binding(1, 0)]] RWStructuredBuffer<float> gYCurr;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> gXPrev;
[[vk::binding(3, 0)]] RWStructuredBuffer<float> gYPrev;
[[vk::binding(4, 0)]] RWStructuredBuffer<float> gMass;
[[vk::binding(5, 0)]] RWStructuredBuffer<float> gDensity;

// =========================================
// Compute Shader: simple motion
// =========================================
[shader("compute")]
[numthreads(64, 1, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    const uint count = 1024; // keep in sync with CPU allocation
    if (i >= count) return;

    // Basic Verlet step with no external forces. Velocity is encoded as
    // (x_curr - x_prev).
    float x_curr = gXCurr[i];
    float y_curr = gYCurr[i];
    float x_prev = gXPrev[i];
    float y_prev = gYPrev[i];

    float vel_x = x_curr - x_prev;
    float vel_y = y_curr - y_prev;

    float x_next = x_curr + vel_x;
    float y_next = y_curr + vel_y;

    // Simple bounce with a slight damping factor to avoid runaway energy.
    const float bounce = 0.98f;

    if (x_next > 1.0) {
        x_next = 1.0;
        vel_x = -vel_x * bounce;
    } else if (x_next < -1.0) {
        x_next = -1.0;
        vel_x = -vel_x * bounce;
    }

    if (y_next > 1.0) {
        y_next = 1.0;
        vel_y = -vel_y * bounce;
    } else if (y_next < -1.0) {
        y_next = -1.0;
        vel_y = -vel_y * bounce;
    }

    gXPrev[i] = x_next - vel_x;
    gYPrev[i] = y_next - vel_y;
    gXCurr[i] = x_next;
    gYCurr[i] = y_next;

    // mass/density buffers are present and updated elsewhere.
}

// =========================================
// Vertex Shader: read particle positions
// =========================================
struct VSOutput {
    float4 pos : SV_Position;
    float4 col : COLOR0;
    float pointSize : SV_PointSize;
};

struct PSInput {
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

[shader("vertex")]
VSOutput mainVS(uint id : SV_VertexID)
{
    float x = gXCurr[id];
    float y = gYCurr[id];

    VSOutput o;
    o.pos = float4(x, y, 0, 1);
    o.col = float4(1, 1, 1, 1);
    o.pointSize = 4.0;
    return o;
}

// =========================================
// Pixel Shader
// =========================================
[shader("pixel")]
float4 mainPS(PSInput i) : SV_Target
{
    return i.col;
}
