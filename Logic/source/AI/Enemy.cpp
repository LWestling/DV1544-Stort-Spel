#include <AI\Enemy.h>
#include <AI\Behavior\TestBehavior.h>
#include <AI\Behavior\RangedBehavior.h>
using namespace Logic;

Enemy::Enemy(Graphics::ModelID modelID, btRigidBody* body, btVector3 halfExtent, float health, float baseDamage, float moveSpeed, int enemyType, int animationId)
: Entity(body, halfExtent, modelID)
{
	m_behavior = nullptr;

	m_health = health;
	m_baseDamage = baseDamage;
	m_moveSpeed = moveSpeed;
	m_enemyType = enemyType;

	//animation todo
}

void Enemy::setBehavior(BEHAVIOR_ID id)
{
	if (m_behavior)
		delete m_behavior;
	m_behavior = nullptr;

	switch (id) 
	{
		case TEST:
			m_behavior = new TestBehavior();
			break;
		case RANGED:
			m_behavior = new RangedBehavior();
			break;
	}
}

Enemy::~Enemy() {
	if (m_behavior)
		delete m_behavior;
}

void Enemy::setProjectileManager(ProjectileManager * projectileManager)
{
	m_projectiles = projectileManager;
}

void Enemy::update(Player const &player, float deltaTime, bool updatePath) {
	Entity::update(deltaTime);
	updateSpecific(player, deltaTime);

	if (m_behavior) // remove later for better perf
	{
		if (updatePath)
			m_behavior->updatePath(*this, player);
		m_behavior->update(*this, player, deltaTime); // BEHAVIOR IS NOT DONE, FIX LATER K
	}

	m_moveSpeedMod = 0.f; // Reset effect variables, should be in function if more variables are added.
}

void Enemy::debugRendering(Graphics::Renderer & renderer)
{
	if (m_behavior)
	{
		m_behavior->debugRendering(renderer);
	}
}

void Enemy::damage(float damage)
{
	m_health -= damage;
}

void Enemy::affect(int stacks, Effect const &effect, float dt) 
{
	int flags = effect.getStandards()->flags;

	if (flags & Effect::EFFECT_KILL)
		damage(m_health);
	if (flags & Effect::EFFECT_ON_FIRE)
		damage(effect.getModifiers()->modifyDmgTaken * dt);
	if (flags & Effect::EFFECT_MODIFY_MOVEMENTSPEED)
		m_moveSpeedMod += effect.getModifiers()->modifyMovementSpeed;
}

float Enemy::getHealth() const
{
	return m_health;
}

float Enemy::getMaxHealth() const
{
	return m_maxHealth;
}

float Enemy::getBaseDamage() const
{
	return m_baseDamage;
}

float Enemy::getMoveSpeed() const
{
	return m_moveSpeed + m_moveSpeedMod;
}

int Enemy::getEnemyType() const
{
	return m_enemyType;
}

// projectiles
void Enemy::spawnProjectile(btVector3 dir, Graphics::ModelID id, float speed)
{
	ProjectileData data;

	data.damage = getBaseDamage();
	data.meshID = id;
	data.speed = speed;
	data.scale = 1.f;
	data.enemyBullet = true;
	
	m_projectiles->addProjectile(data, getPositionBT(), dir, *this);
}

ProjectileManager * Enemy::getProjectileManager() const
{
	return m_projectiles;
}
