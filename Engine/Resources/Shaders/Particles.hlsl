cbuffer Camera : register(b0)
{
	float4x4 ViewProjection;
	float4x4 InvProjection;
	float4x4 View;
	float4x4 Proj;
};

struct VSIn {
	float4 origin : ORIGIN;
	float4 pos : POSITION;
	float4 vel : VELOCITY;
	float4 color : COLOR;
	float4 uv : TEXCOORD;
	float2 scale : SCALE;
	float rotation : ROTATION;
	float rotation_velocity : ROTATIONV;
	float age : AGE;

	float distort_intensity : DISTORT;

	int type : TYPE;
	int idx : IDX;
};

VSIn VS(VSIn input)
{
	return input;
}

struct GSOut
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float2 dist_uv : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float dist_strength : STR;
};

#define PTYPE_PLANAR 0
#define PTYPE_BILLBOARD 1
#define Epsilon 0.0001

static const float4 UV = float4(0, 0, 1, 1);

inline void PlanarParticle(VSIn input, inout TriangleStream<GSOut> outstream)
{
	GSOut output;
	output.color = input.color;
	output.dist_strength = input.distort_intensity;

	float w = input.scale.x;
	float h = input.scale.y;
	float rot = input.rotation;

	float4x4 rotate = {
		cos(rot),  0.0, sin(rot), 0.0,
		0.0,       1.0, 0.0,      0.0,
		-sin(rot), 0.0, cos(rot), 0.0,
		0.0,       0.0, 0.0,      1.0
	};

	float4 N = mul(rotate, float4(0, 0, w, 0));
	float4 S = mul(rotate, float4(0, 0, -w, 0));
	float4 E = mul(rotate, float4(-h, 0, 0, 0));
	float4 W = mul(rotate, float4(h, 0, 0, 0));


	output.pos = mul(ViewProjection, float4(input.pos.xyz + N + W, 1.0));
	output.uv = input.uv.xy;
	output.dist_uv = UV.xy;
	outstream.Append(output);

	output.pos = mul(ViewProjection, float4(input.pos.xyz + S + W, 1.0));
	output.uv = input.uv.xw;
	output.dist_uv = UV.xw;
	outstream.Append(output);

	output.pos = mul(ViewProjection, float4(input.pos.xyz + N + E, 1.0));
	output.uv = input.uv.zy;
	output.dist_uv = UV.zy;
	outstream.Append(output);

	output.pos = mul(ViewProjection, float4(input.pos.xyz + S + E, 1.0));
	output.uv = input.uv.zw;
	output.dist_uv = UV.zw;
	outstream.Append(output);
}

inline void BillboardParticle(VSIn input, inout TriangleStream<GSOut> outstream)
{
	GSOut output;
	output.color = input.color;
	output.dist_strength = input.distort_intensity;

	float w = input.scale.x;
	float h = input.scale.y;
	float rot = input.rotation;

	float4x4 rotate = {
		cos(rot), -sin(rot), 0.0, 0.0,
		sin(rot), cos(rot),  0.0, 0.0,
		0.0,        0.0,     1.0, 0.0,
		0.0,        0.0,     0.0, 1.0
	};

	float4 N = mul(rotate, float4(0, w, 0, 0));
	float4 S = mul(rotate, float4(0, -w, 0, 0));
	float4 E = mul(rotate, float4(-h, 0, 0, 0));
	float4 W = mul(rotate, float4(h, 0, 0, 0));

	float4 pos = mul(View, float4(input.pos.xyz, 1.0));

	output.pos = mul(Proj, pos + N + W);
	output.uv = input.uv.xy;
	output.dist_uv = UV.xy;
	outstream.Append(output);

	output.pos = mul(Proj, pos + S + W);
	output.uv = input.uv.xw;
	output.dist_uv = UV.xw;
	outstream.Append(output);

	output.pos = mul(Proj, pos + N + E);
	output.uv = input.uv.zy;
	output.dist_uv = UV.zy;
	outstream.Append(output);

	output.pos = mul(Proj, pos + S + E);
	output.uv = input.uv.zw;
	output.dist_uv = UV.zw;
	outstream.Append(output);
}

inline void VelocityParticle(VSIn input, inout TriangleStream<GSOut> outstream)
{
	GSOut output;
	output.color = input.color;
	output.dist_strength = input.distort_intensity;

	float w = input.scale.x;
	float h = input.scale.y;

	float3 u = mul(View, input.vel.xyz).xyz;

	float t = 0.0;
	float nz = abs(normalize(u).z);
	if (nz > 1.0 - Epsilon) {
		t = (nz - (1.0 - Epsilon)) / Epsilon;
	}
	else if (dot(u, u) < Epsilon) {
		t = (Epsilon - dot(u, u)) / Epsilon;
	}

	u.z = 0.0;
	u = normalize(u);

	u = normalize(lerp(u, float3(1, 0, 0), t));
	h = lerp(h, w, t);

	float3 v = float3(-u.y, u.x, 0);
	float3 a = mul(u, View).xyz;
	float3 b = mul(v, View).xyz;
	float3 c = cross(a, b);
	float3x3 basis = float3x3(a, b, c);

	float3 N = mul(float3(0, h, 0), basis);
	float3 S = mul(float3(0, -h, 0), basis);
	float3 E = mul(float3(0, 0, 0), basis);
	float3 W = mul(float3(w * 2, 0, 0), basis);

	output.pos = mul(Proj, mul(View, float4(input.origin.xyz + N + W, 1.0)));
	output.uv = input.uv.xy;
	output.dist_uv = UV.xy;
	outstream.Append(output);

	output.pos = mul(Proj, mul(View, float4(input.origin.xyz + S + W, 1.0)));
	output.uv = input.uv.xw;
	output.dist_uv = UV.xw;
	outstream.Append(output);

	output.pos = mul(Proj, mul(View, float4(input.origin.xyz + N + E, 1.0)));
	output.uv = input.uv.zy;
	output.dist_uv = UV.zy;
	outstream.Append(output);

	output.pos = mul(Proj, mul(View, float4(input.origin.xyz + S + E, 1.0)));
	output.uv = input.uv.zw;
	output.dist_uv = UV.zw;
	outstream.Append(output);
}

[maxvertexcount(4)]
void GS(point VSIn inp[1], inout TriangleStream<GSOut> outstream)
{
	VSIn input = inp[0];

	if (input.type == PTYPE_PLANAR) {
		PlanarParticle(input, outstream);
	}
	else if (input.type == PTYPE_BILLBOARD) {
		BillboardParticle(input, outstream);
	}
	else {
		VelocityParticle(input, outstream);
	}

}

SamplerState ParticleSampler : register(s0);
Texture2D ParticleTexture : register(t0);

struct PSOut {
	float4 color : SV_Target0;
};

PSOut PS(GSOut input)
{
	PSOut output;

	float4 col = ParticleTexture.Sample(ParticleSampler, input.uv);

	output.color = col * input.color;
	output.color.a = saturate(output.color.a);

	return output;
}