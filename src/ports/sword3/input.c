#include "input.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AXIS_PRESS_THRESHOLD 16000
#define AXIS_RELEASE_THRESHOLD 10000

typedef struct ControllerState {
	SDL_GameController *controller;
	SDL_JoystickID instance_id;
	unsigned short key_refs[SDL_NUM_SCANCODES];
	SDL_bool button_down[SDL_CONTROLLER_BUTTON_MAX];
	Sint8 axis_direction[SDL_CONTROLLER_AXIS_MAX];
	struct ControllerState *next;
} ControllerState;

struct Sword3Input {
	ControllerState *controllers;
	unsigned short key_refs[SDL_NUM_SCANCODES];
	SDL_bool keyboard_down[SDL_NUM_SCANCODES];
	SDL_Scancode button_map[SDL_CONTROLLER_BUTTON_MAX];
	SDL_Scancode axis_negative[SDL_CONTROLLER_AXIS_MAX];
	SDL_Scancode axis_positive[SDL_CONTROLLER_AXIS_MAX];
	Sword3InputEvent *queue;
	size_t queue_capacity;
	size_t queue_head;
	size_t queue_count;
	Sword3InputCallback callback;
	void *callback_userdata;
};

static SDL_bool valid_scancode(SDL_Scancode scancode)
{
	return scancode > SDL_SCANCODE_UNKNOWN &&
	       scancode < SDL_NUM_SCANCODES;
}

static int reserve_queue(Sword3Input *input, size_t additional)
{
	size_t required;

	if (additional > SIZE_MAX - input->queue_count) {
		errno = ENOMEM;
		return -1;
	}
	required = input->queue_count + additional;
	if (required > input->queue_capacity) {
		size_t old_capacity = input->queue_capacity;
		size_t new_capacity = old_capacity ? old_capacity * 2 : 32;
		Sword3InputEvent *queue;

		if (new_capacity < old_capacity)
			new_capacity = required;
		while (new_capacity < required) {
			if (new_capacity > SIZE_MAX / 2) {
				new_capacity = required;
				break;
			}
			new_capacity *= 2;
		}
		if (new_capacity > SIZE_MAX / sizeof(*queue)) {
			errno = ENOMEM;
			return -1;
		}
		queue = malloc(new_capacity * sizeof(*queue));
		if (!queue) {
			errno = ENOMEM;
			return -1;
		}
		for (size_t i = 0; i < input->queue_count; i++)
			queue[i] = input->queue[
				(input->queue_head + i) % old_capacity];
		free(input->queue);
		input->queue = queue;
		input->queue_capacity = new_capacity;
		input->queue_head = 0;
	}
	return 0;
}

static void enqueue_reserved(Sword3Input *input, SDL_Scancode scancode,
			     SDL_bool pressed)
{
	Sword3InputEvent event = {scancode, pressed};

	input->queue[(input->queue_head + input->queue_count) %
		     input->queue_capacity] = event;
	input->queue_count++;
	if (input->callback)
		input->callback(&event, input->callback_userdata);
}

static int change_global_key(Sword3Input *input, SDL_Scancode scancode,
			     SDL_bool pressed)
{
	unsigned short *references;

	if (!valid_scancode(scancode))
		return 0;
	references = &input->key_refs[scancode];
	if (pressed) {
		if (*references == (unsigned short)-1) {
			errno = EOVERFLOW;
			return -1;
		}
		if (!*references && reserve_queue(input, 1) < 0)
			return -1;
		(*references)++;
		if (*references == 1)
			enqueue_reserved(input, scancode, SDL_TRUE);
		return 0;
	}
	if (!*references)
		return 0;
	if (*references == 1 && reserve_queue(input, 1) < 0)
		return -1;
	(*references)--;
	if (!*references)
		enqueue_reserved(input, scancode, SDL_FALSE);
	return 0;
}

static int change_controller_key(Sword3Input *input, ControllerState *state,
				 SDL_Scancode scancode, SDL_bool pressed)
{
	unsigned short *controller_references;
	unsigned short *global_references;

	if (!valid_scancode(scancode))
		return 0;
	controller_references = &state->key_refs[scancode];
	global_references = &input->key_refs[scancode];
	if (pressed) {
		if (*controller_references == (unsigned short)-1 ||
		    *global_references == (unsigned short)-1) {
			errno = EOVERFLOW;
			return -1;
		}
		if (!*global_references && reserve_queue(input, 1) < 0)
			return -1;
		(*controller_references)++;
		(*global_references)++;
		if (*global_references == 1)
			enqueue_reserved(input, scancode, SDL_TRUE);
		return 0;
	}
	if (!*controller_references)
		return 0;
	if (*global_references < *controller_references) {
		errno = EPROTO;
		return -1;
	}
	if (*global_references == 1 && reserve_queue(input, 1) < 0)
		return -1;
	(*controller_references)--;
	(*global_references)--;
	if (!*global_references)
		enqueue_reserved(input, scancode, SDL_FALSE);
	return 0;
}

static ControllerState *find_controller(Sword3Input *input,
					SDL_JoystickID instance_id)
{
	for (ControllerState *state = input->controllers; state;
	     state = state->next) {
		if (state->instance_id == instance_id)
			return state;
	}
	return NULL;
}

static int add_controller(Sword3Input *input, int device_index)
{
	SDL_GameController *controller;
	SDL_Joystick *joystick;
	SDL_JoystickID instance_id;
	ControllerState *state;

	if (!SDL_IsGameController(device_index))
		return 0;
	controller = SDL_GameControllerOpen(device_index);
	if (!controller)
		return -1;
	joystick = SDL_GameControllerGetJoystick(controller);
	if (!joystick) {
		SDL_GameControllerClose(controller);
		errno = EIO;
		return -1;
	}
	instance_id = SDL_JoystickInstanceID(joystick);
	if (instance_id < 0) {
		SDL_GameControllerClose(controller);
		return -1;
	}
	if (find_controller(input, instance_id)) {
		SDL_GameControllerClose(controller);
		return 0;
	}
	state = calloc(1, sizeof(*state));
	if (!state) {
		SDL_GameControllerClose(controller);
		errno = ENOMEM;
		return -1;
	}
	state->controller = controller;
	state->instance_id = instance_id;
	state->next = input->controllers;
	input->controllers = state;
	return 0;
}

static int release_controller(Sword3Input *input, ControllerState *state)
{
	size_t releases = 0;

	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		unsigned short owned = state->key_refs[scancode];

		if (owned > input->key_refs[scancode]) {
			errno = EPROTO;
			return -1;
		}
		if (owned && owned == input->key_refs[scancode])
			releases++;
	}
	if (reserve_queue(input, releases) < 0)
		return -1;
	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		unsigned short owned = state->key_refs[scancode];

		if (!owned)
			continue;
		input->key_refs[scancode] -= owned;
		state->key_refs[scancode] = 0;
		if (!input->key_refs[scancode])
			enqueue_reserved(input, (SDL_Scancode)scancode, SDL_FALSE);
	}
	memset(state->button_down, 0, sizeof(state->button_down));
	memset(state->axis_direction, 0, sizeof(state->axis_direction));
	return 0;
}

static int remove_controller(Sword3Input *input, SDL_JoystickID instance_id)
{
	ControllerState **link = &input->controllers;
	ControllerState *state;
	int rc;

	while (*link && (*link)->instance_id != instance_id)
		link = &(*link)->next;
	if (!*link)
		return 0;
	state = *link;
	rc = release_controller(input, state);
	if (rc < 0)
		return -1;
	*link = state->next;
	SDL_GameControllerClose(state->controller);
	free(state);
	return 0;
}

static void set_default_mappings(Sword3Input *input)
{
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
		input->button_map[i] = SDL_SCANCODE_UNKNOWN;
	for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
		input->axis_negative[i] = SDL_SCANCODE_UNKNOWN;
		input->axis_positive[i] = SDL_SCANCODE_UNKNOWN;
	}

	input->button_map[SDL_CONTROLLER_BUTTON_A] = SDL_SCANCODE_RETURN;
	input->button_map[SDL_CONTROLLER_BUTTON_B] = SDL_SCANCODE_ESCAPE;
	input->button_map[SDL_CONTROLLER_BUTTON_X] = SDL_SCANCODE_X;
	input->button_map[SDL_CONTROLLER_BUTTON_Y] = SDL_SCANCODE_Y;
	input->button_map[SDL_CONTROLLER_BUTTON_BACK] = SDL_SCANCODE_TAB;
	input->button_map[SDL_CONTROLLER_BUTTON_START] = SDL_SCANCODE_SPACE;
	input->button_map[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = SDL_SCANCODE_Q;
	input->button_map[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = SDL_SCANCODE_E;
	input->button_map[SDL_CONTROLLER_BUTTON_DPAD_UP] = SDL_SCANCODE_UP;
	input->button_map[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = SDL_SCANCODE_DOWN;
	input->button_map[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = SDL_SCANCODE_LEFT;
	input->button_map[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = SDL_SCANCODE_RIGHT;
	input->axis_negative[SDL_CONTROLLER_AXIS_LEFTX] = SDL_SCANCODE_LEFT;
	input->axis_positive[SDL_CONTROLLER_AXIS_LEFTX] = SDL_SCANCODE_RIGHT;
	input->axis_negative[SDL_CONTROLLER_AXIS_LEFTY] = SDL_SCANCODE_UP;
	input->axis_positive[SDL_CONTROLLER_AXIS_LEFTY] = SDL_SCANCODE_DOWN;
}

Sword3Input *sword3_input_create(Sword3InputCallback callback, void *userdata)
{
	Sword3Input *input = calloc(1, sizeof(*input));
	int joystick_count;

	if (!input) {
		errno = ENOMEM;
		return NULL;
	}
	input->callback = callback;
	input->callback_userdata = userdata;
	set_default_mappings(input);

	joystick_count = SDL_NumJoysticks();
	if (joystick_count < 0) {
		sword3_input_destroy(input);
		return NULL;
	}
	for (int i = 0; i < joystick_count; i++) {
		if (add_controller(input, i) < 0) {
			sword3_input_destroy(input);
			return NULL;
		}
	}
	return input;
}

void sword3_input_destroy(Sword3Input *input)
{
	ControllerState *state;

	if (!input)
		return;
	(void)sword3_input_release_all(input);
	state = input->controllers;
	while (state) {
		ControllerState *next = state->next;
		SDL_GameControllerClose(state->controller);
		free(state);
		state = next;
	}
	free(input->queue);
	free(input);
}

static int handle_keyboard(Sword3Input *input, const SDL_KeyboardEvent *event)
{
	SDL_Scancode scancode = event->keysym.scancode;
	SDL_bool pressed = event->type == SDL_KEYDOWN;

	if (!valid_scancode(scancode) || (pressed && event->repeat))
		return 0;
	if (input->keyboard_down[scancode] == pressed)
		return 0;
	if (change_global_key(input, scancode, pressed) < 0)
		return -1;
	input->keyboard_down[scancode] = pressed;
	return 0;
}

static int handle_axis(Sword3Input *input,
		       const SDL_ControllerAxisEvent *event)
{
	ControllerState *state = find_controller(input, event->which);
	Sint8 old_direction;
	Sint8 new_direction;
	SDL_GameControllerAxis axis = (SDL_GameControllerAxis)event->axis;

	if (!state || axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX)
		return 0;
	old_direction = state->axis_direction[axis];
	new_direction = old_direction;
	if (old_direction < 0 && event->value > -AXIS_RELEASE_THRESHOLD)
		new_direction = 0;
	else if (old_direction > 0 && event->value < AXIS_RELEASE_THRESHOLD)
		new_direction = 0;
	if (!new_direction) {
		if (event->value <= -AXIS_PRESS_THRESHOLD)
			new_direction = -1;
		else if (event->value >= AXIS_PRESS_THRESHOLD)
			new_direction = 1;
	}
	if (new_direction == old_direction)
		return 0;
	if (old_direction &&
	    change_controller_key(input, state,
				  old_direction < 0
					? input->axis_negative[axis]
					: input->axis_positive[axis],
				  SDL_FALSE) < 0)
		return -1;
	state->axis_direction[axis] = 0;
	if (new_direction &&
	    change_controller_key(input, state,
				  new_direction < 0
					? input->axis_negative[axis]
					: input->axis_positive[axis],
				  SDL_TRUE) < 0)
		return -1;
	state->axis_direction[axis] = new_direction;
	return 0;
}

int sword3_input_handle_event(Sword3Input *input, const SDL_Event *event)
{
	ControllerState *state;
	SDL_GameControllerButton button;

	if (!input || !event) {
		errno = EINVAL;
		return -1;
	}
	switch (event->type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		return handle_keyboard(input, &event->key) < 0 ? -1 : 1;
	case SDL_CONTROLLERDEVICEADDED:
		return add_controller(input, event->cdevice.which) < 0 ? -1 : 1;
	case SDL_CONTROLLERDEVICEREMOVED:
		return remove_controller(input, event->cdevice.which) < 0 ? -1 : 1;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		state = find_controller(input, event->cbutton.which);
		button = (SDL_GameControllerButton)event->cbutton.button;
		if (state && button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX) {
			SDL_bool pressed =
				event->type == SDL_CONTROLLERBUTTONDOWN;

			if (state->button_down[button] == pressed)
				return 1;
			if (change_controller_key(input, state,
						  input->button_map[button],
						  pressed) < 0)
				return -1;
			state->button_down[button] = pressed;
		}
		return 1;
	case SDL_CONTROLLERAXISMOTION:
		return handle_axis(input, &event->caxis) < 0 ? -1 : 1;
	case SDL_WINDOWEVENT:
		if (event->window.event != SDL_WINDOWEVENT_FOCUS_LOST)
			return 0;
		return sword3_input_release_all(input) < 0 ? -1 : 1;
	default:
		return 0;
	}
}

int sword3_input_poll(Sword3Input *input, Sword3InputEvent *event)
{
	if (!input || !event) {
		errno = EINVAL;
		return -1;
	}
	if (!input->queue_count)
		return 0;
	*event = input->queue[input->queue_head];
	input->queue_head = (input->queue_head + 1) % input->queue_capacity;
	input->queue_count--;
	return 1;
}

SDL_bool sword3_input_is_down(const Sword3Input *input,
			      SDL_Scancode scancode)
{
	return input && valid_scancode(scancode) && input->key_refs[scancode]
		? SDL_TRUE : SDL_FALSE;
}

int sword3_input_release_all(Sword3Input *input)
{
	size_t releases = 0;

	if (!input)
		return 0;
	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		if (input->key_refs[scancode])
			releases++;
	}
	if (reserve_queue(input, releases) < 0)
		return -1;
	memset(input->keyboard_down, 0, sizeof(input->keyboard_down));
	for (ControllerState *state = input->controllers; state; state = state->next) {
		memset(state->key_refs, 0, sizeof(state->key_refs));
		memset(state->button_down, 0, sizeof(state->button_down));
		memset(state->axis_direction, 0, sizeof(state->axis_direction));
	}
	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		if (input->key_refs[scancode]) {
			input->key_refs[scancode] = 0;
			enqueue_reserved(input, (SDL_Scancode)scancode,
					 SDL_FALSE);
		}
	}
	return 0;
}

static int release_controller_inputs(Sword3Input *input)
{
	size_t releases = 0;

	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		size_t owned = 0;

		for (ControllerState *state = input->controllers; state;
		     state = state->next)
			owned += state->key_refs[scancode];
		if (owned > input->key_refs[scancode]) {
			errno = EPROTO;
			return -1;
		}
		if (owned && owned == input->key_refs[scancode])
			releases++;
	}
	if (reserve_queue(input, releases) < 0)
		return -1;
	for (int scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++) {
		size_t owned = 0;

		for (ControllerState *state = input->controllers; state;
		     state = state->next) {
			owned += state->key_refs[scancode];
			state->key_refs[scancode] = 0;
		}
		input->key_refs[scancode] -= (unsigned short)owned;
		if (owned && !input->key_refs[scancode])
			enqueue_reserved(input, (SDL_Scancode)scancode,
					 SDL_FALSE);
	}
	for (ControllerState *state = input->controllers; state;
	     state = state->next) {
		memset(state->button_down, 0, sizeof(state->button_down));
		memset(state->axis_direction, 0, sizeof(state->axis_direction));
	}
	return 0;
}

int sword3_input_map_button(Sword3Input *input, SDL_GameControllerButton button,
			    SDL_Scancode scancode)
{
	if (!input || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX ||
	    (scancode != SDL_SCANCODE_UNKNOWN && !valid_scancode(scancode))) {
		errno = EINVAL;
		return -1;
	}
	if (release_controller_inputs(input) < 0)
		return -1;
	input->button_map[button] = scancode;
	return 0;
}

int sword3_input_map_axis(Sword3Input *input, SDL_GameControllerAxis axis,
			  SDL_Scancode negative, SDL_Scancode positive)
{
	if (!input || axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX ||
	    (negative != SDL_SCANCODE_UNKNOWN && !valid_scancode(negative)) ||
	    (positive != SDL_SCANCODE_UNKNOWN && !valid_scancode(positive))) {
		errno = EINVAL;
		return -1;
	}
	if (release_controller_inputs(input) < 0)
		return -1;
	input->axis_negative[axis] = negative;
	input->axis_positive[axis] = positive;
	return 0;
}
