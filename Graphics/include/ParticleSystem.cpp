#include "ParticleSystem.h"

#include "Engine\Constants.h"
#include "ThrowIfFailed.h"

#include <DDSTextureLoader.h>

#include <algorithm>

namespace Graphics {


#define EASE_FUNC(type, name, factor) inline type name(type start, type end, float t) \
{ \
	return Lerp(start, end, factor(t)); \
}


inline float Lerp(float start, float end, float t)
{
	return start + t * (end - start);
}

inline XMVECTOR Lerp(XMVECTOR start, XMVECTOR end, float t)
{
	return XMVectorLerp(start, end, t);
}

inline float EaseInFactor(float t)
{
	return powf(t, 5);
}

inline float EaseOutFactor(float t)
{
	return 1 - powf(1 - t, 5);
}

EASE_FUNC(float, EaseIn, EaseInFactor)
EASE_FUNC(XMVECTOR, EaseIn, EaseInFactor)

EASE_FUNC(float, EaseOut, EaseOutFactor)
EASE_FUNC(XMVECTOR, EaseOut, EaseOutFactor)


EaseFunc ease_funcs[] = {
	Lerp,
	EaseIn,
	EaseOut,
	nullptr
};

EaseFuncV ease_funcs_xmv[] = {
	Lerp,
	EaseIn,
	EaseOut,
	nullptr
};

inline float RandomFloat(float lo, float hi)
{
	return ((hi - lo) * ((float)rand() / RAND_MAX)) + lo;
}


ParticleSystem::ParticleSystem(ID3D11Device *device, const wchar_t * file, int capacity, int width, int height)
	: m_ParticleShader(device, SHADER_PATH("Particles.hlsl"), {
		{ "ORIGIN",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "VELOCITY",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "SCALE",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "ROTATION",  0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "ROTATIONV", 0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "AGE",       0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "DISTORT",   0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TYPE",      0, DXGI_FORMAT_R32_SINT,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "IDX",       0, DXGI_FORMAT_R32_SINT,           0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	})
{
	if (file)
		DeserializeParticles(file, effect_definitions, particle_definitions);

	particles.reserve(capacity);

	ThrowIfFailed(CreateDDSTextureFromFile(device, TEXTURE_PATH("Particle.dds"), nullptr, &m_ParticleAtlas));
}

ParticleSystem::~ParticleSystem()
{
}

bool ParticleSystem::ProcessFX(ParticleEffect & fx, XMMATRIX model, float dt)
{
	auto time = fx.clamp_children ? fx.children_time : fx.time;

	fx.age += dt;

	if (fx.age >= time) {
		if (fx.loop) {
			fx.age = 0;
		}
	}
	else {
		return true;
	}

	for (int i = 0; i < fx.fx_count; ++i) {
		auto &entry = fx.fx[i];
		auto idx = entry.idx & ~(1 << 31);

		auto def = particle_definitions[idx];

		if (fx.loop || (fx.age >  entry.start && fx.age < entry.start + entry.end)) {
			auto factor = (fx.age - entry.start) / (entry.end);

			auto spawn_ease = GetEaseFunc(entry.spawn_fn);
			float spawn = entry.loop ? entry.spawn_start : spawn_ease((float)entry.spawn_start, (float)entry.spawn_end, factor);

			entry.spawned_particles += spawn * dt;

			for (; entry.spawned_particles >= 1.f; entry.spawned_particles -= 1.f) {
				XMVECTOR pos = XMVectorAdd(XMVector3Transform({ 0, 0, 0 }, model), {
					RandomFloat(entry.emitter_xmin, entry.emitter_xmax),
					RandomFloat(entry.emitter_ymin, entry.emitter_ymax),
					RandomFloat(entry.emitter_zmin, entry.emitter_zmax),
				});

				XMVECTOR vel = {
					RandomFloat(entry.vel_xmin, entry.vel_xmax),
					RandomFloat(entry.vel_ymin, entry.vel_ymax),
					RandomFloat(entry.vel_zmin, entry.vel_zmax),
				};

				float rot = RandomFloat(entry.rot_min, entry.rot_max);
				float rotvel = RandomFloat(entry.rot_vmin, entry.rot_vmax);

				Particle p = {};
				p.origin = pos;
				p.pos = pos;
				p.velocity = vel;
				p.idx = entry.idx;
				p.rotation = rot;
				p.rotation_velocity = rotvel;
				p.type = (int)def.orientation;
				p.scale = { 1.f, 1.f };
				particles.push_back(p);
			}
		}
	}
	
	return false;
}

void ParticleSystem::ProcessFX(ParticleEffect & fx, XMMATRIX model, XMVECTOR velocity, float dt)
{
}

void ParticleSystem::AddFX(std::string name, XMMATRIX model)
{
	ParticleInstance effect = {
		model,
		GetFX(name)
	};

	effects.push_back(effect);
}

ParticleEffect ParticleSystem::GetFX(std::string name)
{
	auto result = std::find_if(effect_definitions.begin(), effect_definitions.end(), [name](ParticleEffect &a) {
		return std::strcmp(name.c_str(), a.name) == 0;
	});

	return *result;
}

void ParticleSystem::update(ID3D11DeviceContext *cxt, Camera * cam, float dt)
{
	for (auto &effect : effects) {
		auto &fx = effect.effect;
		auto origin = effect.position;
		auto time = fx.clamp_children ? fx.children_time : fx.time;

		if (ProcessFX(fx, effect.position, dt)) {
			// TODO: remove emitter
		}
	}

	// TODO: remove unused parameters?
	auto it = particles.begin();
	while (it != particles.end())
	{
		auto p = it;

		// äckligt
		auto idx = p->idx & ~(1 << 31);

		ParticleDefinition *def = &particle_definitions[idx];

		auto age = p->age / def->lifetime;
		p->pos += p->velocity * dt;
		p->velocity -= { 0.f, def->gravity * dt, 0.f, 0.f };
		p->rotation += p->rotation_velocity * dt;
		p->age += dt;

		// TEMP: for bounce
		if (XMVectorGetY(p->pos) < 0) {
			p->pos *= {1.f, 0.f, 1.f};
			p->velocity *= {0.8f, -0.3f, 0.8f};
			p->rotation_velocity *= 0.6;
		}
		
		if (def->orientation == ParticleOrientation::Velocity) {
			p->origin = p->pos;
		}

		auto scale_fn = GetEaseFunc(def->scale_fn);
		p->scale = {
			scale_fn(def->scale_start, def->scale_end, age) * (def->u2 / 2048.f),
			scale_fn(def->scale_start, def->scale_end, age) * (def->v2 / 2048.f)
		};

		bool glow = (int)def->distort_fn & (1 << 31);
		auto distort_fn = GetEaseFunc((ParticleEase)((int)def->distort_fn & ~(1 << 31)));

		p->uv = { def->u / 2048.f, def->v / 2048.f, (def->u + def->u2) / 2048.f, (def->v + def->v2) / 2048.f };

		auto color_fn = GetEaseFuncV(def->color_fn);
		if (color_fn) {
			p->color = color_fn(
				XMLoadFloat4(&def->start_color),
				XMLoadFloat4(&def->end_color),
				age
			);
		}

		if (glow) {
			if (distort_fn) {
				float scalar = distort_fn(def->distort_start, def->distort_end, age);
				p->color *= { scalar, scalar, scalar, 1 };
			}
		}
		else {
			if (distort_fn)
				p->distort = distort_fn(def->distort_start, def->distort_end, age);
		}

		// TODO: better solution than to erase during iteration?
		//       * temp scratch array?
		if (p->age > def->lifetime) {
			it = particles.erase(it);
		}
		else {
			it++;
		}
	}

	// sort back to front (for alpha blending)
	std::sort(particles.begin(), particles.end(), [cam](Particle &a, Particle &b) {
		return XMVectorGetZ(XMVector3Length((XMVECTOR)cam->getPos() - a.pos)) > XMVectorGetZ(XMVector3Length((XMVECTOR)cam->getPos() - b.pos));
	});

	if (!particles.empty()) {
		D3D11_MAPPED_SUBRESOURCE data;
		cxt->Map(m_ParticleBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &data);
		{
			CopyMemory(data.pData, particles.data(), sizeof(ParticleInstance) * particles.size());
		}
		cxt->Unmap(m_ParticleBuffer, 0);
	}
}

void ParticleSystem::render(ID3D11DeviceContext *cxt, DirectX::CommonStates *states, Camera * cam)
{
	auto stride = (UINT)sizeof(ParticleInstance);
	auto offset = 0u;

	cxt->IASetInputLayout(m_ParticleShader);
	cxt->IASetVertexBuffers(0, 1, &m_ParticleBuffer, &stride, &offset);
	cxt->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	cxt->VSSetShader(m_ParticleShader, nullptr, 0);
	cxt->GSSetShader(m_ParticleShader, nullptr, 0);
	auto buf = cam->getBuffer();
	cxt->GSSetConstantBuffers(0, 1, &buf);
	cxt->PSSetShader(m_ParticleShader, nullptr, 0);

	cxt->PSSetShaderResources(0, 1, &m_ParticleAtlas);
	auto sampler = states->LinearClamp();
	cxt->PSSetSamplers(0, 1, &sampler);

	cxt->Draw(particles.size(), 0);
}

}
