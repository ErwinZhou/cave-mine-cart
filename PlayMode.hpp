#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"
#include "MineCartLogic.hpp"

#include <glm/glm.hpp>

#include <array>
#include <random>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//some geometry
	static constexpr float CartRootOffset = 3.75f; //cart_root to cart body, in cart-front.blend
	static constexpr float CameraBack = 1.35f;     //cart body to camera
	static constexpr float CartStartY = -5.65f;    //any earlier and the camera clips the cave wall
	static constexpr float SplitY = 2.1f;          //Junction_Shared_Tie_11, where the rails separate
	static constexpr float BranchEndY = 7.7f;      //Junction_*_Tie_18, the tunnel mouths
	static constexpr float TunnelLength = 16.0f;
	static constexpr float LaneX[3] = { -4.0f, 0.0f, 4.0f };
	static constexpr float LaneYaw[3] = { 0.6196f, 0.0f, -0.6196f }; //atan(4.0/5.6)
	static constexpr float LookLean = 0.22f; //how far the camera turns toward the chosen mouth

	//nothing to decide in the tunnel, so it never crawls no matter what the level rolls at:
	static constexpr float TunnelSpeed = 3.0f;
	static_assert(TunnelSpeed >= mine::MinSpeed && TunnelSpeed <= mine::MaxSpeed);

	enum class Phase {
		Approach, //shared straight, lane still changeable
		Branch,   //past the split, committed
		Tunnel,
	};
	Phase phase = Phase::Approach;

	float cart_s = CartStartY;

	int target_lane = 1;
	int locked_lane = 1;

	mine::MineCartLogic logic;
	//eased so W/S does not snap the cart between speeds:
	float speed = mine::MinSpeed;
	bool slowing = false;

	float cart_x = 0.0f;
	float cart_yaw = 0.0f;
	//the camera leans toward the chosen lane before the split, when the cart itself cannot:
	float cam_yaw = 0.0f;


	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right;

	//one environment scene plus the cart is drawn each frame:
	Scene junction; //cave-cart, with its built-in cart panels dropped
	Scene tunnel;
	Scene cart;     //cart-front, panels already parented to cart_root in Blender
	//dangers live in the tunnel only, so nothing about the choice can be read off the screen:
	static constexpr int BatCount = 5;
	static constexpr float BatSpeed = 6.0f;   //toward the cart, on top of the cart's own speed
	static constexpr float RockDropY = 6.0f;  //a little ahead of where the cart will be
	static constexpr float RockDropZ = 3.2f;

	std::array< Scene, BatCount > bats;
	std::array< Scene::Transform *, BatCount > bat_root = {};
	std::array< Scene::Transform *, BatCount > bat_wing_l = {};
	std::array< Scene::Transform *, BatCount > bat_wing_r = {};
	struct BatState {
		float x = 0.0f, z = 1.5f, scale = 0.3f, delay = 0.0f, flap_phase = 0.0f;
	};
	std::array< BatState, BatCount > bat_state;

	Scene rock;
	Scene::Transform *rock_root = nullptr;
	float rock_x = 0.0f;

	//seconds since this tunnel run began; drives every danger animation:
	float tunnel_t = 0.0f;
	//kept apart from the game rng so replays of a seed stay identical:
	std::mt19937 visual_rng;

	Scene::Camera *camera = nullptr;
	glm::quat camera_base_rotation; //the scene's own "look down +Y", yawed away from each frame
	Scene::Transform *cart_root = nullptr;

	std::shared_ptr< Sound::PlayingSample > cart_loop;

	void begin_phase(Phase next);
	void place_cart_and_camera();
	void spawn_danger();
	void animate_danger(float elapsed);
};
