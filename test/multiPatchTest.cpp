/** Tests for the multi-patch click rules in RackWidget::getMultiPatchAction().

The rules are pure, so they run without a rack, a window, or an engine. Build and run with
`make test` from the Rack directory.
*/
#include <cstdio>
#include <string>
#include <vector>

#include <app/RackWidget.hpp>


using rack::app::RackWidget;
typedef RackWidget::MultiPatchState State;
typedef RackWidget::MultiPatchAction Action;
typedef RackWidget::MultiPatchMode Mode;

static const Mode CLICK = RackWidget::MULTI_PATCH_GRAB;
static const Mode CTRL = RackWidget::MULTI_PATCH_CREATE;
static const Mode CTRL_SHIFT = RackWidget::MULTI_PATCH_CLONE;

static int failures = 0;
static int checks = 0;


static const char* actionName(Action action) {
	switch (action) {
		case RackWidget::MULTI_PATCH_ACTION_NONE: return "nothing";
		case RackWidget::MULTI_PATCH_ACTION_GRAB: return "unplug";
		case RackWidget::MULTI_PATCH_ACTION_CREATE: return "new cable";
		case RackWidget::MULTI_PATCH_ACTION_CLONE: return "duplicate";
		case RackWidget::MULTI_PATCH_ACTION_DROP: return "put back";
		case RackWidget::MULTI_PATCH_ACTION_PATCH: return "patch";
		default: return "?";
	}
}

static const char* modeName(Mode mode) {
	switch (mode) {
		case RackWidget::MULTI_PATCH_GRAB: return "click";
		case RackWidget::MULTI_PATCH_CREATE: return "ctrl+click";
		case RackWidget::MULTI_PATCH_CLONE: return "ctrl+shift+click";
		default: return "?";
	}
}

static void check(const std::string& name, const State& state, Mode mode, Action expected) {
	checks++;
	Action actual = RackWidget::getMultiPatchAction(state, mode);
	if (actual != expected) {
		failures++;
		std::printf("FAIL  %s\n        %s expected %s, got %s\n",
			name.c_str(), modeName(mode), actionName(expected), actionName(actual));
	}
}


/** Nothing collected yet: the click decides what the collection starts with. */
static void testStartingACollection() {
	State empty;
	// A port with no cable has nothing to take, so every gesture starts a new cable
	check("start, empty port", empty, CLICK, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("start, empty port", empty, CTRL, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("start, empty port", empty, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CREATE);

	State patched;
	patched.canTake = true;
	// A patched port gives up its cable, unless Ctrl asks for a new one
	check("start, patched port", patched, CLICK, RackWidget::MULTI_PATCH_ACTION_GRAB);
	check("start, patched port", patched, CTRL, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("start, patched port", patched, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CLONE);
}

/** Cables are held and none has been patched: ports of the free type can still give up cables. */
static void testCollectingOnTheFreeType() {
	State s;
	s.collecting = true;
	s.freeType = true;

	// An empty port has nothing to take, so every gesture wires a cable in
	check("collecting, empty free-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("collecting, empty free-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("collecting, empty free-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_PATCH);

	// A patched port is still a destination on a plain click, so a cable can be wired into a port
	// that already has one. The modifiers reach the cables stacked on it instead.
	s.canTake = true;
	check("collecting, patched free-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("collecting, patched free-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_GRAB);
	check("collecting, patched free-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CLONE);

	// Emptying a port doesn't turn its own clicks into anything else
	s.collected = true;
	check("collecting, emptied free-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_PATCH);
	s.canTake = false;
	check("collecting, emptied free-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("collecting, emptied free-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_PATCH);
}

/** Ports of the other type can only ever start new cables, never hand over their own plug. */
static void testCollectingOnTheOtherType() {
	State s;
	s.collecting = true;
	s.freeType = false;

	check("collecting, empty other-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("collecting, empty other-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("collecting, empty other-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CREATE);

	// Ctrl+shift copies a cable already on the port, from the end the port isn't plugged into
	s.canTake = true;
	check("collecting, patched other-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("collecting, patched other-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("collecting, patched other-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CLONE);

	// A plain click puts back a cable started here, Ctrl keeps multing the port
	s.collected = true;
	check("collecting, collected other-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_DROP);
	check("collecting, collected other-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_CREATE);
	check("collecting, collected other-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_CLONE);
}

/** Once a cable has been patched the collection can only shrink. */
static void testPatching() {
	State s;
	s.collecting = true;
	s.patching = true;
	s.freeType = true;
	s.canTake = true;
	s.collected = true;

	// No modifier reopens collecting, so a stacked port unwinds one click at a time
	check("patching, free-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("patching, free-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_PATCH);
	check("patching, free-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_PATCH);

	// The held cables can't reach the other type at all
	s.freeType = false;
	check("patching, other-type port", s, CLICK, RackWidget::MULTI_PATCH_ACTION_NONE);
	check("patching, other-type port", s, CTRL, RackWidget::MULTI_PATCH_ACTION_NONE);
	check("patching, other-type port", s, CTRL_SHIFT, RackWidget::MULTI_PATCH_ACTION_NONE);
}

/** Every click either takes a cable of the collection's free end, or spends one. */
static void testEveryStateKeepsTheFreeEnd() {
	for (int i = 0; i < 32; i++) {
		State s;
		s.collecting = (i & 1);
		s.patching = (i & 2);
		s.freeType = (i & 4);
		s.collected = (i & 8);
		s.canTake = (i & 16);
		if (!s.collecting && s.patching)
			continue;

		for (Mode mode : {CLICK, CTRL, CTRL_SHIFT}) {
			checks++;
			Action action = RackWidget::getMultiPatchAction(s, mode);
			// Taking the plug in a port only keeps the free end on ports of the free type, and
			// starting a cable there only keeps it on ports of the other type
			bool takesPlug = (action == RackWidget::MULTI_PATCH_ACTION_GRAB)
				|| (action == RackWidget::MULTI_PATCH_ACTION_CLONE && s.freeType);
			bool startsCable = (action == RackWidget::MULTI_PATCH_ACTION_CREATE)
				|| (action == RackWidget::MULTI_PATCH_ACTION_CLONE && !s.freeType);
			if (s.collecting && takesPlug && !s.freeType) {
				failures++;
				std::printf("FAIL  takes a plug of the wrong type (%s)\n", actionName(action));
			}
			if (s.collecting && startsCable && s.freeType) {
				failures++;
				std::printf("FAIL  starts a cable of the wrong type (%s)\n", actionName(action));
			}
			// A cable can only be taken from a port that has one
			if (!s.canTake && (action == RackWidget::MULTI_PATCH_ACTION_GRAB || action == RackWidget::MULTI_PATCH_ACTION_CLONE)) {
				failures++;
				std::printf("FAIL  takes a cable from a port that has none (%s)\n", actionName(action));
			}
		}
	}
}


int main() {
	testStartingACollection();
	testCollectingOnTheFreeType();
	testCollectingOnTheOtherType();
	testPatching();
	testEveryStateKeepsTheFreeEnd();

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
