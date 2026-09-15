#pragma once

#include <cstdint>
#include <random>

namespace mine {

enum class Danger : uint8_t { None, Bat, Rock };
enum class Level : uint8_t { Easy, Medium, Hard, Hell };

constexpr int MaxHP = 10;
constexpr int BatDamage = 2;
constexpr int RockDamage = 5;

//the cart is always rolling; the player may slow down but never stop or reverse:
constexpr float MinSpeed = 1.0f;
constexpr float MaxSpeed = 5.0f;
constexpr float TunnelSpeed = 4.0f;

constexpr int LaneCount = 3;

char const *level_name(Level level);

//half-open ranges, so junction 5 is Medium and only Medium:
Level level_for(int junction_index);

struct Junction {
	Danger lane[LaneCount] = { Danger::None, Danger::None, Danger::None };

	bool safe(int i) const { return lane[i] == Danger::None; }
	int danger_count() const;
	bool has_safe_lane() const;
};

//decided the instant the cart crosses the split, applied when it leaves the tunnel:
struct Pending {
	int lane = 1;
	Danger danger = Danger::None;
	int damage = 0;
	bool active = false;
};

struct MineCartLogic {
	int hp = MaxHP;
	int junctions_cleared = 0; //also the score, and what picks the level
	uint32_t seed;
	std::mt19937 rng;
	Junction current;
	Pending pending;

	explicit MineCartLogic(uint32_t seed_ = std::random_device{}());

	Level level() const { return level_for(junctions_cleared); }

	//three speeds: what the level rolls at, and what W and S reach for
	float default_speed() const;
	float min_speed() const { return MinSpeed; }
	float max_speed() const { return MaxSpeed; }

	//Hell gives one pass of the warnings instead of two:
	int warning_passes() const { return level() == Level::Hell ? 1 : 2; }

	void roll_junction();
	bool commit(int lane);  //false if a decision is already waiting
	bool settle();          //false if there is nothing to apply
	bool dead() const { return hp <= 0; }
	void reset_run(uint32_t seed_);
};

} //namespace mine
