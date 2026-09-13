#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

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

	static constexpr float Speed = 1.5f;

	enum class Phase {
		Approach, //shared straight, lane still changeable
		Branch,   //past the split, committed
		Tunnel,
	};
	Phase phase = Phase::Approach;

	float cart_s = CartStartY;

	int target_lane = 1;
	int locked_lane = 1;

	float cart_x = 0.0f;
	float cart_yaw = 0.0f;

	int junctions_cleared = 0;

	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right;

	//one environment scene plus the cart is drawn each frame:
	Scene junction; //cave-cart, with its built-in cart panels dropped
	Scene tunnel;
	Scene cart;     //cart-front, panels already parented to cart_root in Blender

	Scene::Camera *camera = nullptr;
	Scene::Transform *cart_root = nullptr;

	std::shared_ptr< Sound::PlayingSample > cart_loop;

	void begin_phase(Phase next);
	void place_cart_and_camera();
};
