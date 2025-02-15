/*
 *  Author: fkaa
 *
 *  TL;DR: Compute shader used to sort lights into a grid of "bins" for later
 *         use in forward shading to reduce amount of lights per "bin" shaded.
 *
 *  Overview of the sorting/culling technique:
 *    1. TODO
 *
 *  Registers:
 *    Buffers:
 *      b0: camera
 *      b1: dispatch params
 *    Textures:
 *      t0: depth texture (Texture2D)
 *      t1: frustums      (StructuredBuffer)
 *      t2: lights        (StructuredBuffer)
 *      t3: debug heatmap (Texture2D)
 *    Samplers:
 *      s0: debug sampler (SamplerState, Linear Clamp)
 *    UAVs:
 *      u0: opaque light index counter      (RWStructuredBuffer<uint>)
 *      u1: transparent light index counter (RWStructuredBuffer<uint>)
 *      u2: opaque light index list         (RWStructuredBuffer<uint>)
 *      u3: transparent light index list    (RWStructuredBuffer<uint>)
 *      u4: opaque light grid               (RWTexture2D<uint2>)
 *      u5: transparent light grid          (RWTexture2D<uint2>)
 *      u6: debug texture                   (RWTexture2D<float4>)
 *
 *
 *  TODO:
 *    - move common forward+ structs and definitions into shared file
 *    - add option to disable debug texture parts to save memory in release
 */

#define BLOCK_SIZE 16
#define WIDTH 1280
#define HEIGHT 720

#define NUM_LIGHTS 8

cbuffer Camera : register(b0)
{
	float4x4 ViewProjection;
	float4x4 InvProjection;
	float4x4 View;
}

cbuffer DispatchParams : register(b1)
{
	uint4   numThreadGroups;
	uint4   numThreads;
}

// TODO: change
struct Light {
	float4 positionVS;
	float3 positionWS;
	float range;
	float3 color;
	float intensity;
};

// A plane defined by a normal and distance from origin
struct Plane {
	float3 N;
	float d;
};

// A frustrum defined by 4 planes
struct Frustum {
	Plane planes[4];
};

// A sphere defined as a center point and radius
struct Sphere {
	float3 center;
	float  radius;
};

struct CSInput {
	// 3D index of the thread group in the dispatch
	uint3 groupID           : SV_GroupID;
	// 3D index of local thread ID in a thread group
	uint3 groupThreadID     : SV_GroupThreadID;
	// 3D index of global thread ID in the dispatch
	uint3 dispatchThreadID  : SV_DispatchThreadID;
	// Flattened local index of the thread within a thread group
	uint  groupIndex        : SV_GroupIndex;
};

bool SphereInsidePlane(Sphere sphere, Plane plane) {
	return dot(plane.N, sphere.center) < -sphere.radius;
}

bool SphereInsideFrustum(Sphere sphere, Frustum frustum, float zNear, float zFar) {
	bool result = true;
	
	if (sphere.center.z - sphere.radius > zNear ||
		sphere.center.z + sphere.radius < zFar)
	{
		result = false;
	}

	//[unroll]
	for (int i = 0; i < 4 && result; i++) {
		if (SphereInsidePlane(sphere, frustum.planes[i])) {
			result = false;
		}
	}

	return result;
}

// Convert from clip-space to view-space
float4 ClipToView(float4 clip)
{
	// View space position
	float4 view = mul(InvProjection, clip);
	// Perspective projection
	view = view / view.w;

	return view;
}

// Convert from screen-space to view-space
float4 ScreenToView(float4 screen)
{
	// Convert to normalized texture coordinates
	float2 texCoord = screen.xy / float2(WIDTH, HEIGHT);

	// Convert to clip space
	float4 clip = float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, screen.z, screen.w);

	return ClipToView(clip);
}


// Depth texture from z-prepass
Texture2D DepthTexture : register(t0);

// All the frustums generated at the start
StructuredBuffer<Frustum> Frustums : register(t1);
// List of all lights to perform culling on
StructuredBuffer<Light>   Lights : register(t2);

globallycoherent RWStructuredBuffer<uint> OpaqueLightIndexCounter : register(u0);
RWStructuredBuffer<uint> OpaqueLightIndexList : register(u2);
RWTexture2D<uint2>       OpaqueLightGrid : register(u4);

globallycoherent RWStructuredBuffer<uint> TransparentLightIndexCounter : register(u1);
RWStructuredBuffer<uint> TransparentLightIndexList : register(u3);
RWTexture2D<uint2>       TransparentLightGrid : register(u5);

// Debug texture to visualize the frequency of lights per tile (basically a
// heatmap)
RWTexture2D<float4> DebugTexture : register(u6);

// Debug gradient whose color indicates frequency of tile lights
Texture2D DebugGradient : register(t3);

// Linear Clamp
SamplerState DebugSampler : register(s0);

groupshared uint MinDepth;
groupshared uint MaxDepth;

groupshared Frustum GroupFrustum;

groupshared uint OpaqueLightCount;
groupshared uint OpaqueLightIndexOffset;
groupshared uint OpaqueLightList[1024];

groupshared uint TransparentLightCount;
groupshared uint TransparentLightIndexOffset;
groupshared uint TransparentLightList[1024];

float4 GetDebugColor(CSInput input, uint count, uint max)
{
	int2 coord = input.dispatchThreadID.xy;

	if (input.groupThreadID.x == 0 || input.groupThreadID.y == 0) {
		return float4(0, 0, 0, 0.9f);
	}
	else if (input.groupThreadID.x == 1 || input.groupThreadID.y == 1) {
		return float4(1, 1, 1, 0.5f);
	}
	else if (count > 0) {
		float norm = (float)count / (float)max;
		float4 col = DebugGradient.SampleLevel(DebugSampler, float2(norm, 0), 0);

		return col;
	}
	else {
		return float4(0, 0, 0, 1);
	}
}

void OpaqueAppendLight(uint lightIndex) {
	uint index;
	InterlockedAdd(OpaqueLightCount, 1, index);
	if (index < 1024) {
		OpaqueLightList[index] = lightIndex;
	}
}

void TransparentAppendLight(uint lightIndex) {
	uint index;
	InterlockedAdd(TransparentLightCount, 1, index);
	if (index < 1024) {
		TransparentLightList[index] = lightIndex;
	}
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CS(CSInput input)
{
	int2 coord = input.dispatchThreadID.xy;
	float depth = DepthTexture.Load(int3(coord, 0)).r;

	uint atomicDepth = asuint(depth);

	// initialize groupshared values on thread #0 and sync
	if (input.groupIndex == 0) {
		MinDepth = 0xffffffff;
		MaxDepth = 0x0;

		OpaqueLightCount = 0;
		TransparentLightCount = 0;

		GroupFrustum = Frustums[input.groupID.x + (input.groupID.y * numThreadGroups.x)];
	}



	GroupMemoryBarrierWithGroupSync();
	
	if (depth != 1.f) {
		InterlockedMin(MinDepth, atomicDepth);
		InterlockedMax(MaxDepth, atomicDepth);
	}
	// need to wait for all threads to avoid data races, as next code section
	// requires the max/min value of the entire group
	GroupMemoryBarrierWithGroupSync();

	float minDepth = asfloat(MinDepth);
	float maxDepth = asfloat(MaxDepth);



	float minDepthVS = ScreenToView(float4(0, 0, minDepth, 1)).z;
	float maxDepthVS = ScreenToView(float4(0, 0, maxDepth, 1)).z;
	float nearClipVS = ScreenToView(float4(0, 0, 0, 1)).z;
	//DebugTexture[coord] = float4(minDepthVS/10.0, maxDepthVS / 10.0, 0., 1.0);

	// TODO: LH/RH?
	Plane minPlane = { float3(0, 0, -1), -minDepthVS };

	// loop through all lights and test if inside/intersecting with our group
	// frustum, and add to light list if so
	for (uint i = input.groupIndex; i < NUM_LIGHTS; i += BLOCK_SIZE * BLOCK_SIZE) {
		Light light = Lights[i];

		if (light.range == 0) continue;

		Sphere pointLight = { mul(View, float4(light.positionWS, 1.0)).xyz, light.range };
		if (SphereInsideFrustum(pointLight, GroupFrustum, nearClipVS, maxDepthVS)) {

			TransparentAppendLight(i);

			if (!SphereInsidePlane(pointLight, minPlane)) {
				OpaqueAppendLight(i);
			}
		}
	}

	// next code section transfers light list information to light grid, so we
	// need to wait again for work to complete in the entire group
	GroupMemoryBarrierWithGroupSync();


	// again, use only thread id #0 inside group to 
	if (input.groupIndex == 0) {
		InterlockedAdd(OpaqueLightIndexCounter[0], OpaqueLightCount, OpaqueLightIndexOffset);
		OpaqueLightGrid[input.groupID.xy] = uint2(
			OpaqueLightIndexOffset,
			OpaqueLightCount
			);

		InterlockedAdd(TransparentLightIndexCounter[0], TransparentLightCount, TransparentLightIndexOffset);
		TransparentLightGrid[input.groupID.xy] = uint2(
			TransparentLightIndexOffset,
			TransparentLightCount
			);
	}

	// next section transfers our groupshared data to the global UAV, so all our
	// work needs to finish before continuing
	GroupMemoryBarrierWithGroupSync();


	for (i = input.groupIndex; i < OpaqueLightCount; i += BLOCK_SIZE * BLOCK_SIZE) {
		OpaqueLightIndexList[OpaqueLightIndexOffset + i] = OpaqueLightList[i];
	}

	for (i = input.groupIndex; i < TransparentLightCount; i += BLOCK_SIZE * BLOCK_SIZE) {
		TransparentLightIndexList[TransparentLightIndexOffset + i] = TransparentLightList[i];
	}

	DebugTexture[coord] = GetDebugColor(input, OpaqueLightCount, NUM_LIGHTS);
}