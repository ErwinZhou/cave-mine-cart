#include "MineCartLogic.hpp"

#include <cassert>
#include <stdexcept>

namespace mine {

char const *level_name(Level level) {
	switch (level) {
		case Level::Easy: return "EASY";
		case Level::Medium: return "MEDIUM";
		case Level::Hard: return "HARD";
		case Level::Hell: return "HELL";
	}
	return "?";
}

Level level_for(int junction_index) {
	if (junction_index < 5) return Level::Easy;
	if (junction_index < 10) return Level::Medium;
	if (junction_index < 15) return Level::Hard;
	return Level::Hell;
}

int Junction::danger_count() const {
	int n = 0;
	for (int i = 0; i < LaneCount; ++i) if (!safe(i)) n += 1;
	return n;
}

bool Junction::has_safe_lane() const {
	for (int i = 0; i < LaneCount; ++i) if (safe(i)) return true;
	return false;
}

MineCartLogic::MineCartLogic(uint32_t seed_) : seed(seed_), rng(seed_) {
	roll_junction();
}

float MineCartLogic::default_speed() const {
	switch (level()) {
		case Level::Easy: return 1.0f;
		case Level::Medium: return 2.0f;
		case Level::Hard: return 3.0f;
		case Level::Hell: return MaxSpeed;
	}
	return 3.0f;
}

void MineCartLogic::roll_junction() {
	current = Junction();

	std::uniform_real_distribution< float > chance(0.0f, 1.0f);
	std::uniform_int_distribution< int > pick_lane(0, LaneCount - 1);
	std::uniform_int_distribution< int > pick_kind(0, 1);

	auto place = [&](int count) {
		while (current.danger_count() < count) {
			int lane = pick_lane(rng);
			if (!current.safe(lane)) continue;
			current.lane[lane] = (pick_kind(rng) == 0 ? Danger::Bat : Danger::Rock);
		}
	};

	switch (level()) {
		case Level::Easy:
			if (chance(rng) < 0.80f) place(1);
			break;
		case Level::Medium:
			place(chance(rng) < 0.60f ? 2 : 1);
			break;
		case Level::Hard:
		case Level::Hell:
			place(2);
			break;
	}

	//with three lanes and at most two dangers this holds structurally, but the whole game
	//rests on it, so a future tuning edit must not be able to break it quietly:
	assert(current.has_safe_lane());
}

bool MineCartLogic::commit(int lane) {
	if (lane < 0 || lane >= LaneCount) throw std::out_of_range("lane out of range");
	if (pending.active) return false;

	pending.lane = lane;
	pending.danger = current.lane[lane];
	pending.damage = (pending.danger == Danger::Bat ? BatDamage
	                : pending.danger == Danger::Rock ? RockDamage : 0);
	pending.active = true;
	return true;
}

bool MineCartLogic::settle() {
	if (!pending.active) return false;

	hp -= pending.damage;
	if (hp < 0) hp = 0;
	junctions_cleared += 1;
	pending = Pending();

	if (!dead()) roll_junction();
	return true;
}

void MineCartLogic::reset_run(uint32_t seed_) {
	hp = MaxHP;
	junctions_cleared = 0;
	seed = seed_;
	rng.seed(seed_);
	pending = Pending();
	roll_junction();
}

} //namespace mine
