#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <random>
#include <cmath>
#include <algorithm>

struct MeshSet {
	MeshBuffer const *buffer = nullptr;
	GLuint vao = 0;
};

static MeshSet cave_set, tunnel_set, cart_set;

static std::function< Scene const *() > scene_loader(MeshSet *set, std::string const &pnct, std::string const &scene) {
	return [set, pnct, scene]() -> Scene const * {
		set->buffer = new MeshBuffer(data_path(pnct));
		set->vao = set->buffer->make_vao_for_program(lit_color_texture_program->program);
		return new Scene(data_path(scene), [set](Scene &s, Scene::Transform *transform, std::string const &mesh_name){
			Mesh const &mesh = set->buffer->lookup(mesh_name);

			s.drawables.emplace_back(transform);
			Scene::Drawable &drawable = s.drawables.back();

			drawable.pipeline = lit_color_texture_program_pipeline;

			drawable.pipeline.vao = set->vao;
			drawable.pipeline.type = mesh.type;
			drawable.pipeline.start = mesh.start;
			drawable.pipeline.count = mesh.count;
		});
	};
}

Load< Scene > cave_scene   (LoadTagDefault, scene_loader(&cave_set,   "cave-cart.pnct",   "cave-cart.scene"));
Load< Scene > tunnel_scene (LoadTagDefault, scene_loader(&tunnel_set, "tunnel-cart.pnct", "tunnel-cart.scene"));
Load< Scene > cart_scene   (LoadTagDefault, scene_loader(&cart_set,   "cart-front.pnct",  "cart-front.scene"));

static MeshSet bat_set, rock_set;
Load< Scene > bat_scene    (LoadTagDefault, scene_loader(&bat_set,    "bat.pnct",         "bat.scene"));
Load< Scene > rock_scene   (LoadTagDefault, scene_loader(&rock_set,   "rock.pnct",        "rock.scene"));

//the recordings carry up to a second of silence before anything happens, which is longer than a
//warning is allowed to play; drop the quiet head and tail so every sample starts on its onset
static std::function< Sound::Sample const *() > sample_loader(std::string const &file) {
	return [file]() -> Sound::Sample const * {
		Sound::Sample raw(data_path(file));
		std::vector< float > const &d = raw.data;
		if (d.empty()) return new Sound::Sample(d);

		float peak = 0.0f;
		for (float x : d) peak = std::max(peak, std::abs(x));
		float const gate = std::max(1e-4f, 0.02f * peak);

		size_t first = 0;
		while (first < d.size() && std::abs(d[first]) < gate) first += 1;
		size_t last = d.size();
		while (last > first && std::abs(d[last - 1]) < gate) last -= 1;
		if (first >= last) return new Sound::Sample(d);

		//a little room before the onset, and short ramps so the cut edges do not click
		size_t const lead = 48000 / 50;
		size_t const ramp = 48000 / 200;
		first = (first > lead ? first - lead : 0);
		last = std::min(d.size(), last + lead);

		std::vector< float > out(d.begin() + std::ptrdiff_t(first), d.begin() + std::ptrdiff_t(last));
		for (size_t i = 0; i < ramp && i < out.size(); ++i) {
			float g = float(i) / float(ramp);
			out[i] *= g;
			out[out.size() - 1 - i] *= g;
		}
		return new Sound::Sample(out);
	};
}

Load< Sound::Sample > cart_slow_sample (LoadTagDefault, sample_loader("sounds/cart-moving-slowly.wav"));
Load< Sound::Sample > cart_fast_sample (LoadTagDefault, sample_loader("sounds/cart-moving-quickly.wav"));
Load< Sound::Sample > bats_sample      (LoadTagDefault, sample_loader("sounds/bats.wav"));
Load< Sound::Sample > rocks_sample     (LoadTagDefault, sample_loader("sounds/rocks-loose.wav"));
Load< Sound::Sample > hit_bats_sample  (LoadTagDefault, sample_loader("sounds/hit-by-bats.wav"));
Load< Sound::Sample > hit_rocks_sample (LoadTagDefault, sample_loader("sounds/hit-by-rocks.wav"));
Load< Sound::Sample > avoid_sample     (LoadTagDefault, sample_loader("sounds/avoid-sound.wav"));
Load< Sound::Sample > game_end_sample  (LoadTagDefault, sample_loader("sounds/game-end.wav"));

//matches "cart_root" and "cart_root.002" alike:
static bool name_is(std::string const &name, std::string const &want) {
	if (name.compare(0, want.size(), want) != 0) return false;
	if (name.size() == want.size()) return true;
	//accept exactly ".NNN"
	return name.size() == want.size() + 4 && name[want.size()] == '.';
}


PlayMode::PlayMode() : junction(*cave_scene), tunnel(*tunnel_scene), cart(*cart_scene),
                       rock(*rock_scene), visual_rng(std::random_device{}()) {
	//tunnel-cart has no camera of its own and borrows this one:
	if (junction.cameras.size() != 1) {
		throw std::runtime_error("Expecting cave-cart to have exactly one camera, but it has " + std::to_string(junction.cameras.size()));
	}
	camera = &junction.cameras.front();
	camera_base_rotation = camera->transform->rotation;

	//cave-cart has a cart modelled into it; without this it draws on top of cart-front's:
	for (auto d = junction.drawables.begin(); d != junction.drawables.end(); /* below */) {
		if (d->transform->name.compare(0, 5, "Cart_") == 0) d = junction.drawables.erase(d);
		else ++d;
	}

	for (auto &transform : cart.transforms) {
		if (name_is(transform.name, "cart_root")) cart_root = &transform;
	}
	if (cart_root == nullptr) throw std::runtime_error("cart-front.scene has no 'cart_root' transform.");

	for (int i = 0; i < BatCount; ++i) {
		bats[size_t(i)] = *bat_scene;
		for (auto &t : bats[size_t(i)].transforms) {
			if (name_is(t.name, "bat_root")) bat_root[size_t(i)] = &t;
			else if (name_is(t.name, "bat_wing_left")) bat_wing_l[size_t(i)] = &t;
			else if (name_is(t.name, "bat_wing_right")) bat_wing_r[size_t(i)] = &t;
		}
		if (!bat_root[size_t(i)]) throw std::runtime_error("bat.scene has no 'bat_root' transform.");
	}
	for (auto &t : rock.transforms) {
		if (name_is(t.name, "rock_root")) rock_root = &t;
	}
	if (rock_root == nullptr) throw std::runtime_error("rock.scene has no 'rock_root' transform.");

	speed = logic.default_speed();
	arm_warnings(0.6f, (SplitY - CartStartY) / std::max(speed, mine::MinSpeed) - 0.2f);

	begin_phase(Phase::Approach);

	//both rumbles run the whole time and are crossfaded by speed:
	cart_slow_loop = Sound::loop(*cart_slow_sample, RumbleVolume);
	cart_fast_loop = Sound::loop(*cart_fast_sample, 0.0f);
}

PlayMode::~PlayMode() {
}

void PlayMode::begin_phase(Phase next) {
	phase = next;
	if (next == Phase::Approach) {
		cart_s = CartStartY;
		target_lane = 1;
		locked_lane = 1;
	} else if (next == Phase::Branch) {
		cart_s = SplitY;
		locked_lane = target_lane;
	} else { //Tunnel
		cart_s = 0.0f;
		settled_this_tunnel = false;
	}
	place_cart_and_camera();
	spawn_danger();
}

void PlayMode::place_cart_and_camera() {
	//the panels sit CartRootOffset south of cart_root, so bias the root to land the body at cart_s:
	cart_root->position = glm::vec3(cart_x, cart_s + CartRootOffset, 0.0f);
	cart_root->rotation = glm::angleAxis(cart_yaw, glm::vec3(0.0f, 0.0f, 1.0f));

	//behind the cart along its heading, not straight south, or a turn swings the cart out of frame:
	glm::vec3 forward = glm::vec3(-std::sin(cam_yaw), std::cos(cam_yaw), 0.0f);
	camera->transform->position = glm::vec3(cart_x, cart_s, 1.65f) - CameraBack * forward;
	camera->transform->rotation = glm::angleAxis(cam_yaw, glm::vec3(0.0f, 0.0f, 1.0f)) * camera_base_rotation;
}

//one heart per 2 HP: outline always, chords across the inside for the part that is still full
static void draw_heart(DrawLines &lines, glm::vec2 c, float r, float fill, glm::u8vec4 col) {
	auto at = [&](float t) {
		float st = std::sin(t), ct = std::cos(t);
		return glm::vec3(c.x + r * (st * st * st),
		                 c.y + r * (0.8125f * ct - 0.3125f * std::cos(2.0f * t)
		                          - 0.125f * std::cos(3.0f * t) - 0.0625f * std::cos(4.0f * t)),
		                 0.0f);
	};
	int const steps = 24;
	for (int i = 0; i < steps; ++i) {
		lines.draw(at(float(i) / steps * 6.2832f), at(float(i + 1) / steps * 6.2832f), col);
	}
	if (fill <= 0.0f) return;
	for (int i = 1; i < 7; ++i) {
		float y = c.y + r * (0.9f - 0.28f * float(i));
		float d = (y - c.y) / (r * 1.1f);
		float half = r * 0.95f * std::sqrt(std::max(0.0f, 1.0f - d * d));
		lines.draw(glm::vec3(c.x - half, y, 0.0f),
		           glm::vec3(c.x - half + 2.0f * half * fill, y, 0.0f), col);
	}
}

//anything not in play is parked below the floor rather than juggling drawable lists:
static constexpr glm::vec3 Offscreen = glm::vec3(0.0f, 0.0f, -100.0f);

void PlayMode::restart() {
	Sound::stop_all_samples();
	warn_playing.clear();

	logic.reset_run(std::random_device{}());
	speed = logic.default_speed();
	slowing = false;
	cart_x = cart_yaw = cam_yaw = 0.0f;
	target_lane = locked_lane = 1;

	cart_slow_loop = Sound::loop(*cart_slow_sample, RumbleVolume);
	cart_fast_loop = Sound::loop(*cart_fast_sample, 0.0f);

	begin_phase(Phase::Approach);
	arm_warnings(0.6f, (SplitY - CartStartY) / std::max(speed, mine::MinSpeed) - 0.2f);
}

void PlayMode::arm_warnings(float start_delay, float budget) {
	warn_queue.clear();
	warn_next = 0;
	warn_t = 0.0f;

	int dangers = logic.current.danger_count();
	if (dangers == 0) return;

	//drop the repeat pass, then tighten the spacing, rather than let a warning arrive after the
	//split, where it can no longer change anything
	int passes = logic.warning_passes();
	float room = budget - start_delay - WarnLength;
	float gap = WarnGap;
	while (passes > 1 && float(passes * dangers - 1) * WarnLength > room) passes -= 1;
	int slots = passes * dangers;
	if (slots > 1) gap = glm::clamp(room / float(slots - 1), WarnLength, WarnGap);

	float at = start_delay;
	for (int pass = 0; pass < passes; ++pass) {
		for (int lane = 0; lane < mine::LaneCount; ++lane) {
			if (logic.current.safe(lane)) continue;
			warn_queue.push_back(Warning{ at, lane, logic.current.lane[lane] });
			at += gap;
		}
	}
}

void PlayMode::update_warnings(float elapsed) {
	warn_t += elapsed;

	while (warn_next < warn_queue.size() && warn_queue[warn_next].at <= warn_t) {
		Warning const &w = warn_queue[warn_next];
		//2D panning, not play_3D: pan -1 leaves the right channel at exactly zero, which is the
		//whole point; distance attenuation would bleed the warning into both ears
		auto handle = Sound::play(
			*(w.danger == mine::Danger::Bat ? bats_sample : rocks_sample),
			1.0f, LanePan[w.lane]);
		warn_playing.push_back(Playing{ handle, warn_t + WarnLength });
		warn_next += 1;
	}

	for (auto p = warn_playing.begin(); p != warn_playing.end(); /* below */) {
		if (warn_t >= p->stop_at) {
			if (p->handle) p->handle->stop(0.05f);
			p = warn_playing.erase(p);
		} else {
			++p;
		}
	}
}

void PlayMode::update_rumble(float) {
	float f = (speed - mine::MinSpeed) / (mine::MaxSpeed - mine::MinSpeed);
	f = glm::clamp(f, 0.0f, 1.0f);
	cart_slow_loop->set_volume(RumbleVolume * (1.0f - f));
	cart_fast_loop->set_volume(RumbleVolume * f);
}

void PlayMode::spawn_danger() {
	tunnel_t = 0.0f;
	for (int i = 0; i < BatCount; ++i) bat_root[size_t(i)]->position = Offscreen;
	rock_root->position = Offscreen;

	if (phase != Phase::Tunnel || !logic.pending.active) return;

	//tunnel_wall's bore is x -1.74..1.81 and z up to 3.45, so keep well inside it:
	std::uniform_real_distribution< float > across(-1.1f, 1.1f);
	std::uniform_real_distribution< float > height(0.7f, 2.6f);
	std::uniform_real_distribution< float > size(0.18f, 0.34f);
	std::uniform_real_distribution< float > turn(0.0f, 6.283f);

	if (logic.pending.danger == mine::Danger::Bat) {
		for (int i = 0; i < BatCount; ++i) {
			BatState &b = bat_state[size_t(i)];
			b.x = across(visual_rng);
			b.z = height(visual_rng);
			b.scale = size(visual_rng);
			b.delay = float(i) * 0.22f;
			b.flap_phase = turn(visual_rng);
		}
	} else if (logic.pending.danger == mine::Danger::Rock) {
		rock_x = across(visual_rng) * 0.5f;
	}
}

void PlayMode::animate_danger(float elapsed) {
	if (phase != Phase::Tunnel) return;
	tunnel_t += elapsed;

	if (logic.pending.danger == mine::Danger::Bat) {
		for (int i = 0; i < BatCount; ++i) {
			BatState const &b = bat_state[size_t(i)];
			float t = tunnel_t - b.delay;
			//start at the far end and close on the cart faster than the cart is travelling:
			float y = TunnelLength - t * BatSpeed;
			if (t < 0.0f || y < cart_s - 3.0f) {
				bat_root[size_t(i)]->position = Offscreen;
				continue;
			}
			bat_root[size_t(i)]->position = glm::vec3(
				b.x + 0.35f * std::sin(t * 2.7f + b.flap_phase), y,
				b.z + 0.20f * std::sin(t * 1.9f + b.flap_phase));
			bat_root[size_t(i)]->scale = glm::vec3(b.scale);

			float flap = 0.9f * std::sin(tunnel_t * 18.0f + b.flap_phase);
			glm::vec3 const fwd = glm::vec3(0.0f, 1.0f, 0.0f);
			if (bat_wing_l[size_t(i)]) bat_wing_l[size_t(i)]->rotation = glm::angleAxis(-flap, fwd);
			if (bat_wing_r[size_t(i)]) bat_wing_r[size_t(i)]->rotation = glm::angleAxis(flap, fwd);
		}
	} else if (logic.pending.danger == mine::Danger::Rock) {
		//let go once the cart is close enough that the fall reads as a near miss:
		float release = (RockDropY - 3.0f) / std::max(TunnelSpeed, speed);
		float t = tunnel_t - release;
		if (t < 0.0f) {
			rock_root->position = Offscreen;
			return;
		}
		float z = RockDropZ - 0.5f * 9.8f * t * t;
		rock_root->position = glm::vec3(rock_x, RockDropY, std::max(0.0f, z));
		rock_root->scale = glm::vec3(0.45f);
		rock_root->rotation = glm::angleAxis(t * 3.0f, glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)));
	}
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	(void)window_size;

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.repeat) return false;
		if (evt.key.key == SDLK_ESCAPE) {
			Mode::set_current(nullptr);
			return true;
		} else if (evt.key.key == SDLK_R) {
			restart();
			return true;
		} else if (phase == Phase::GameOver) {
			return false;
		} else if (evt.key.key == SDLK_A) {
			left.downs += 1;
			if (phase == Phase::Approach && target_lane > 0) target_lane -= 1;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.downs += 1;
			if (phase == Phase::Approach && target_lane < 2) target_lane += 1;
			return true;
		} else if (evt.key.key == SDLK_S) {
			slowing = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			slowing = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_S) {
			slowing = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	if (phase == Phase::GameOver) return;
	{ //S eases down toward the floor speed, W back up to whatever the level rolls at:
		float want = slowing ? logic.min_speed() : logic.default_speed();
		speed += (1.0f - std::exp(-elapsed / 0.25f)) * (want - speed);
	}
	cart_s += (phase == Phase::Tunnel ? std::max(TunnelSpeed, speed) : speed) * elapsed;

	if (phase == Phase::Approach) {
		if (cart_s >= SplitY) {
			//the lane is decided here and nowhere else; the tunnel only plays it back
			locked_lane = target_lane;
			logic.commit(locked_lane);
			begin_phase(Phase::Branch);
		}
	} else if (phase == Phase::Branch) {
		if (cart_s >= BranchEndY) begin_phase(Phase::Tunnel);
	} else { //Tunnel
		//the danger meets the cart part way through; settle there so the HP drop lines up with
		//the animation, and so the next junction exists in time to be warned about
		float impact = TunnelLength * 0.5f;
		if (logic.pending.danger == mine::Danger::Rock) impact = RockDropY;
		else if (logic.pending.danger == mine::Danger::Bat) impact = TunnelLength * 0.34f;

		if (!settled_this_tunnel && cart_s >= impact) {
			settled_this_tunnel = true;
			//everything heard inside the tunnel is centred: the lane is already decided, so there
			//is nothing left for panning to tell the player
			if (logic.pending.danger == mine::Danger::Bat) {
				Sound::play(*bats_sample, 0.9f);
				Sound::play(*hit_bats_sample, 1.0f);
			} else if (logic.pending.danger == mine::Danger::Rock) {
				Sound::play(*rocks_sample, 0.9f);
				Sound::play(*hit_rocks_sample, 1.0f);
			} else {
				Sound::play(*avoid_sample, 0.9f);
			}
			logic.settle();
			if (logic.dead()) {
				Sound::stop_all_samples();
				warn_playing.clear();
				Sound::play(*game_end_sample, 1.0f);
				phase = Phase::GameOver;
				return;
			}
			//hold the warnings back until the cart is nearly out of the tunnel, so they land in
			//the approach where the choice is actually made rather than ending before it starts
			float remaining = (TunnelLength - cart_s) / std::max(TunnelSpeed, speed);
			float approach = (SplitY - CartStartY) / std::max(speed, mine::MinSpeed);
			arm_warnings(std::max(0.0f, remaining - 0.5f), remaining + approach - 0.2f);
		}
		if (cart_s >= TunnelLength) begin_phase(Phase::Approach);
	}

	{ //ease toward the lane so a switch reads as following a rail rather than sliding sideways:
		float want_x = 0.0f;
		float want_yaw = 0.0f;
		float want_cam_yaw = 0.0f;
		if (phase == Phase::Approach) {
			//the three rails are still one track here, so only the camera turns:
			want_cam_yaw = LookLean * float(1 - target_lane);
		} else if (phase == Phase::Branch) {
			float t = (cart_s - SplitY) / (BranchEndY - SplitY);
			want_x = LaneX[locked_lane] * t;
			want_yaw = LaneYaw[locked_lane];
			want_cam_yaw = want_yaw;
		}

		//covers about 90% of the gap in 0.2 seconds
		float k = 1.0f - std::exp(-elapsed / 0.09f);
		cart_x += k * (want_x - cart_x);
		cart_yaw += k * (want_yaw - cart_yaw);
		cam_yaw += k * (want_cam_yaw - cam_yaw);
	}

	place_cart_and_camera();
	animate_danger(elapsed);
	update_warnings(elapsed);
	update_rumble(elapsed);

	{ //listener rides the camera:
		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	left.downs = 0;
	right.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	//a lone overhead hemi gives a face's top 1.0 and its side 0.5, so the same brown reads as two
	//browns depending on viewing angle; most of the light is ambient to keep that ratio near 1
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(0.40f, 0.40f, 0.38f)));
	glUniform3fv(lit_color_texture_program->LIGHT_AMBIENT_vec3, 1, glm::value_ptr(glm::vec3(0.62f, 0.60f, 0.56f)));
	glUseProgram(0);

	//the tunnel bores are open pipes, so this is what shows through them: keep it the rock's
	//hue or the far end reads as a hole punched in the world rather than depth
	glClearColor(0.030f, 0.027f, 0.023f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	if (phase == Phase::Tunnel) tunnel.draw(*camera);
	else junction.draw(*camera);
	cart.draw(*camera);
	if (phase == Phase::Tunnel) {
		for (auto &b : bats) b.draw(*camera);
		rock.draw(*camera);
	}

	{
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		//drawn twice, one pixel apart, so it stays legible over both rock and void:
		auto text = [&](std::string const &str, float x, float y, float h) {
			lines.draw_text(str, glm::vec3(x, y, 0.0f),
				glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = 2.0f / float(drawable_size.y);
			lines.draw_text(str, glm::vec3(x + ofs, y + ofs, 0.0f),
				glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		};

		for (int i = 0; i < 5; ++i) {
			int left = logic.hp - i * 2;
			float fill = (left >= 2 ? 1.0f : left == 1 ? 0.5f : 0.0f);
			draw_heart(lines, glm::vec2(-aspect + 0.10f + float(i) * 0.115f, -0.84f), 0.045f, fill,
				glm::u8vec4(0xe0, 0x30, 0x40, 0xff));
		}
		text(mine::level_name(logic.level()), -aspect + 0.055f, -0.98f, 0.07f);
		text("CLEARED " + std::to_string(logic.junctions_cleared), -aspect + 0.055f, 0.90f, 0.07f);

		if (phase == Phase::GameOver) {
			text("RUN OVER", -0.42f, 0.10f, 0.20f);
			text("cleared " + std::to_string(logic.junctions_cleared) + " junctions", -0.52f, -0.06f, 0.08f);
			text("press R to ride again", -0.48f, -0.20f, 0.08f);
		} else {
			text("A / D  lane     S  slow     W  fast     R  restart",
				-aspect + 0.055f, 0.80f, 0.05f);
		}
	}
	GL_ERRORS();
}
