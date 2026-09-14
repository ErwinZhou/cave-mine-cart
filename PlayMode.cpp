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

static std::function< Sound::Sample const *() > sample_loader(std::string const &file) {
	return [file]() -> Sound::Sample const * { return new Sound::Sample(data_path(file)); };
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

	begin_phase(Phase::Approach);

	cart_loop = Sound::loop_3D(*cart_slow_sample, 0.18f, glm::vec3(0.0f, 0.0f, 0.0f), 8.0f);
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

//anything not in play is parked below the floor rather than juggling drawable lists:
static constexpr glm::vec3 Offscreen = glm::vec3(0.0f, 0.0f, -100.0f);

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
		if (cart_s >= TunnelLength) {
			logic.settle();
			begin_phase(Phase::Approach);
		}
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

	cart_loop->set_position(glm::vec3(cart_x, cart_s, 0.2f));

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

	// {
	// 	glDisable(GL_DEPTH_TEST);
	// 	float aspect = float(drawable_size.x) / float(drawable_size.y);
	// 	DrawLines lines(glm::mat4(
	// 		1.0f / aspect, 0.0f, 0.0f, 0.0f,
	// 		0.0f, 1.0f, 0.0f, 0.0f,
	// 		0.0f, 0.0f, 1.0f, 0.0f,
	// 		0.0f, 0.0f, 0.0f, 1.0f
	// 	));

	// 	char const *phase_name = (phase == Phase::Approach ? "APPROACH"
	// 	                        : phase == Phase::Branch   ? "BRANCH" : "TUNNEL");
	// 	std::string hud = std::string(phase_name)
	// 		+ "  lane " + std::to_string(target_lane)
	// 		+ "  y " + std::to_string(int(cart_s))
	// 		+ "  cleared " + std::to_string(junctions_cleared);

	// 	constexpr float H = 0.09f;
	// 	lines.draw_text(hud,
	// 		glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
	// 		glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 		glm::u8vec4(0x00, 0x00, 0x00, 0x00));
	// 	float ofs = 2.0f / drawable_size.y;
	// 	lines.draw_text(hud,
	// 		glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
	// 		glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 		glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	// }
	GL_ERRORS();
}
