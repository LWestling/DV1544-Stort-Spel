#pragma once

#include <vector>
#include <unordered_map>

#include <DirectXMath.h>
#include <CommonStates.h>

#include "Camera.h"
#include "Resources\Shader.h"

using namespace DirectX;

namespace Graphics {

#define MAX_PARTICLE_FX 8

enum class ParticleEmitter {
	Static = 0,
	Cube,
	Sphere
};

enum class ParticleEase {
	Linear = 0,
	EaseIn,
	EaseOut,
	None
};

enum class ParticleOrientation {
	Planar = 0,
	Clip,
	Velocity,
	VelocityAnchored
};

typedef float(*EaseFunc)(float, float, float);
typedef XMVECTOR(*EaseFuncV)(XMVECTOR, XMVECTOR, float);

extern EaseFunc ease_funcs[4];
extern EaseFuncV ease_funcs_xmv[4];

inline EaseFunc GetEaseFunc(ParticleEase ease)
{
	return ease_funcs[(int)ease];
}

inline EaseFuncV GetEaseFuncV(ParticleEase ease)
{
	return ease_funcs_xmv[(int)ease];
}

struct ParticleDefinition {
	char name[32] = { "Untitled\0" };
	ParticleOrientation orientation;

	float gravity;
	float lifetime = 1.f;

	ParticleEase scale_fn;
	float scale_start;
	float scale_end = 1.0;

	ParticleEase distort_fn = ParticleEase::None;
	float distort_start = 1.0;
	float distort_end;

	ParticleEase color_fn;
	XMFLOAT4 start_color = { 1.f, 1.f, 1.f, 1.f };
	XMFLOAT4 end_color = { 1.f, 1.f, 1.f, 0.f };

	int u, v, u2 = 256, v2 = 256;
};


struct ParticleEffectEntry {
	int idx = -1;
	float start, end = 1.f;
	bool loop;

	ParticleEmitter emitter_type;
	float emitter_xmin, emitter_xmax;
	float emitter_ymin, emitter_ymax;
	float emitter_zmin, emitter_zmax;

	float vel_xmin, vel_xmax;
	float vel_ymin, vel_ymax;
	float vel_zmin, vel_zmax;

	float rot_min, rot_max;
	float rot_vmin, rot_vmax;

	ParticleEase spawn_fn;
	int spawn_start = 0, spawn_end;
	float spawned_particles = 1.f;
};

struct ParticleEffect {
	char name[32] = { "Untitled\0" };
	int fx_count = 0;
	ParticleEffectEntry fx[MAX_PARTICLE_FX];
	float children_time = 0.f;
	float time = 1.f;
	float age;

	bool loop;
	bool clamp_children = false;
};

struct Particle {
	XMVECTOR origin;
	XMVECTOR pos;
	XMVECTOR velocity;
	XMVECTOR color;
	XMFLOAT4 uv;
	XMFLOAT2 scale;

	float rotation;
	float rotation_velocity;
	float age;
	float distort;
	int type;
	int idx;
};


struct ParticleInstance {
	XMMATRIX position;
	ParticleEffect effect;
};

class ParticleSystem {
public:
	ParticleSystem(ID3D11Device *device, const wchar_t *file, int capacity, int width, int height);
	~ParticleSystem();

	bool ProcessFX(ParticleEffect &fx, XMMATRIX model, float dt);
	void ProcessFX(ParticleEffect &fx, XMMATRIX model, XMVECTOR velocity, float dt);
	void AddFX(std::string name, XMMATRIX model);
	ParticleEffect GetFX(std::string name);



	void update(ID3D11DeviceContext *cxt, Camera *cam, float dt);
	void render(ID3D11DeviceContext *cxt, DirectX::CommonStates *states, Camera *cam);

	//private:

	//ParticleEffect *getEffect(std::string name);

	ID3D11Buffer *m_ParticleBuffer;

	Shader m_ParticleShader;
	ID3D11ShaderResourceView *m_ParticleAtlas;


	int capacity;

	std::vector<ParticleEffect> effect_definitions;
	std::vector<ParticleDefinition> particle_definitions;

	std::vector<ParticleInstance> effects;
	std::vector<Particle> particles;
};

extern ParticleSystem *FXSystem;

inline bool SerializeParticles(const wchar_t *file, std::vector<ParticleEffect> effects, std::vector<ParticleDefinition> definitions)
{
	FILE *f;

	if (_wfopen_s(&f, file, L"wb+") != 0) {
		return false;
	}

	for (auto &fx : effects) {
		fx.age = 0;
		for (int i = 0; i < fx.fx_count; ++i) {
			if (fx.fx[i].emitter_type == ParticleEmitter::Static)
				fx.fx[i].spawned_particles = 1.f;
		}
	}

	auto size = definitions.size();
	fwrite(&size, sizeof(size_t), 1, f);
	fwrite(definitions.data(), sizeof(ParticleDefinition), definitions.size(), f);

	size = effects.size();
	fwrite(&size, sizeof(size_t), 1, f);
	fwrite(effects.data(), sizeof(ParticleEffect), effects.size(), f);

	fclose(f);

	return true;
}

inline bool DeserializeParticles(const wchar_t *file, std::vector<ParticleEffect> &effects, std::vector<ParticleDefinition> &definitions)
{
	FILE *f;

	if (_wfopen_s(&f, file, L"rb") != 0) {
		return false;
	}

	size_t size;
	fread(&size, sizeof(size_t), 1, f);

	for (int i = 0; i < size; ++i) {
		ParticleDefinition e;
		fread(&e, sizeof(ParticleDefinition), 1, f);

		definitions.push_back(e);
	}


	fread(&size, sizeof(size_t), 1, f);

	for (int i = 0; i < size; ++i) {
		ParticleEffect e;
		fread(&e, sizeof(ParticleEffect), 1, f);

		effects.push_back(e);
	}

	fclose(f);

	return true;
}



}

