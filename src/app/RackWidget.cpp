#include <map>
#include <algorithm>
#include <queue>
#include <functional>

#include <osdialog.h>

#include <app/RackWidget.hpp>
#include <widget/TransparentWidget.hpp>
#include <app/RailWidget.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <plugin.hpp>
#include <engine/Engine.hpp>
#include <context.hpp>
#include <system.hpp>
#include <asset.hpp>
#include <patch.hpp>
#include <helpers.hpp>


namespace rack {
namespace app {


struct RackWidget::Internal {
	RailWidget* rail = NULL;
	widget::Widget* moduleContainer = NULL;
	widget::Widget* plugContainer = NULL;
	widget::Widget* cableContainer = NULL;
	int nextCableColorId = 0;
	/** The last mouse position in the RackWidget */
	math::Vec mousePos;

	bool selecting = false;
	math::Vec selectionStart;
	math::Vec selectionEnd;
	std::set<ModuleWidget*> selectedModules;
	std::map<widget::Widget*, math::Vec> moduleOldPositions;

	bool multiPatching = false;
	/** Type of the free end of the collected cables, so the type of port they are patched into. */
	engine::Port::Type multiPatchFreeType = engine::Port::INPUT;
	/** Whether the collected cables' free ends are the plugs at the ports that were clicked, which
	is the case when they were unplugged or duplicated rather than newly created. Further clicks on
	ports of the free type then take more cables instead of patching.
	*/
	bool multiPatchGrabMode = false;
	/** A collected port, with the cable that follows the cursor until it is patched. */
	struct MultiPatchPort {
		WeakPtr<PortWidget> port;
		WeakPtr<CableWidget> cable;
		/** Port the cable was grabbed from, so it can be put back if it is never patched. */
		WeakPtr<PortWidget> grabbedPort;
		/** Owned. Pushed to the history if the grabbed cable is patched, deleted if it is put back. */
		history::CableRemove* grabHistory = NULL;
	};
	/** Collected ports, in the order they will be patched. */
	std::vector<MultiPatchPort> multiPatchPorts;
	/** Index of the next collected port to be patched.
	Zero while still collecting ports.
	*/
	size_t multiPatchIndex = 0;
	/** Created when multi-patching starts, pushed to the history when it ends. */
	history::ComplexAction* multiPatchHistory = NULL;
};


/** Creates a new Module and ModuleWidget */
static ModuleWidget* moduleWidgetFromJson(json_t* moduleJ) {
	plugin::Model* model = plugin::modelFromJson(moduleJ);
	assert(model);
	INFO("Creating module %s", model->getFullName().c_str());
	engine::Module* module = model->createModule();
	assert(module);
	module->fromJson(moduleJ);

	// Create ModuleWidget
	INFO("Creating module widget %s", model->getFullName().c_str());
	ModuleWidget* moduleWidget = module->model->createModuleWidget(module);
	assert(moduleWidget);
	return moduleWidget;
}


struct ModuleContainer : widget::Widget {
	void draw(const DrawArgs& args) override {
		// Draw ModuleWidget shadows
		Widget::drawLayer(args, -1);

		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		// Draw lights after translucent rectangle
		if (layer == 1) {
			Widget::drawLayer(args, 1);
		}
	}
};


/** Children PlugWidgets are owned by CableWidgets. */
struct PlugContainer : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		// Don't draw on layer 0
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 2) {
			// Draw Plugs
			Widget::draw(args);

			// Draw plug lights
			nvgSave(args.vg);
			nvgGlobalTint(args.vg, color::WHITE);
			Widget::drawLayer(args, 1);
			nvgRestore(args.vg);
		}
	}
};


struct CableContainer : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		// Don't draw on layer 0
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 3) {
			// Draw cable shadows
			Widget::drawLayer(args, -1);

			// Draw cables
			Widget::draw(args);
		}
	}
};


RackWidget::RackWidget() {
	internal = new Internal;

	internal->rail = new RailWidget;
	addChild(internal->rail);

	internal->moduleContainer = new ModuleContainer;
	addChild(internal->moduleContainer);

	internal->plugContainer = new PlugContainer;
	addChild(internal->plugContainer);

	internal->cableContainer = new CableContainer;
	addChild(internal->cableContainer);
}

RackWidget::~RackWidget() {
	clear();
	delete internal;
}

void RackWidget::step() {
	// Stop multi-patching if the setting was disabled
	if (internal->multiPatching && !settings::multiPatch)
		endMultiPatch();

	// Snap the next multi-patch cable to the hovered port, like a dragged cable
	if (internal->multiPatching) {
		widget::Widget* hoveredWidget = APP->event->getDraggedWidget() ? APP->event->getDragHoveredWidget() : APP->event->getHoveredWidget();
		PortWidget* hoveredPw = dynamic_cast<PortWidget*>(hoveredWidget);
		if (hoveredPw && !(canMultiPatchPort(hoveredPw) && hoveredPw->type == internal->multiPatchFreeType))
			hoveredPw = NULL;

		for (size_t i = 0; i < internal->multiPatchPorts.size(); i++) {
			CableWidget* cw = internal->multiPatchPorts[i].cable.get();
			if (!cw)
				continue;
			// Only the cable that will be patched by the next click snaps to the port
			cw->getHoveredPort(internal->multiPatchFreeType) = (i == internal->multiPatchIndex) ? hoveredPw : NULL;
		}
	}

	Widget::step();
}

void RackWidget::draw(const DrawArgs& args) {
	float b = settings::rackBrightness;

	// Draw rack rails and modules
	Widget::draw(args);

	// Draw translucent dark rectangle
	if (b < 1.f) {
		// Get zoom level
		float t[6];
		nvgCurrentTransform(args.vg, t);
		float zoom = t[3];
		// Draw mouse spotlight
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.0, 0.0, VEC_ARGS(box.size));
		nvgFillPaint(args.vg, nvgRadialGradient(args.vg,
			VEC_ARGS(internal->mousePos), 0.0,
			settings::spotlightRadius / zoom,
			nvgRGBAf(0, 0, 0, 1.f - b - settings::spotlightBrightness),
			nvgRGBAf(0, 0, 0, 1.f - b)));
		nvgFill(args.vg);
	}

	// Draw lights and halos
	Widget::drawLayer(args, 1);

	// Tint all draws after this point
	nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1));

	// Draw plugs
	Widget::drawLayer(args, 2);

	// Draw cables
	Widget::drawLayer(args, 3);

	// Draw multi-patch port highlights
	if (internal->multiPatching) {
		for (size_t i = 0; i < internal->multiPatchPorts.size(); i++) {
			// Mark the end the cable is still attached to, which is where it hangs from
			CableWidget* cw = internal->multiPatchPorts[i].cable.get();
			PortWidget* pw = NULL;
			if (cw)
				pw = cw->inputPort ? cw->inputPort : cw->outputPort;
			if (!pw)
				pw = internal->multiPatchPorts[i].port.get();
			if (!pw)
				continue;
			math::Vec pos = pw->getRelativeOffset(pw->box.size.div(2), this);
			float radius = std::max(pw->box.size.x, pw->box.size.y) / 2 + 2.0;
			// Highlight the port that will be patched by the next click
			bool next = (i == internal->multiPatchIndex);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, VEC_ARGS(pos), radius);
			nvgStrokeWidth(args.vg, next ? 2.5 : 1.5);
			// Dim ports that are already patched
			bool patched = (i < internal->multiPatchIndex);
			float alpha = next ? 1.0 : patched ? 0.25 : 0.5;
			nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, alpha));
			nvgStroke(args.vg);
		}
	}

	// Draw selection rectangle
	if (internal->selecting) {
		nvgBeginPath(args.vg);
		math::Rect selectionBox = math::Rect::fromCorners(internal->selectionStart, internal->selectionEnd);
		nvgRect(args.vg, RECT_ARGS(selectionBox));
		nvgFillColor(args.vg, nvgRGBAf(1, 0, 0, 0.25));
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 2.0);
		nvgStrokeColor(args.vg, nvgRGBAf(1, 0, 0, 0.5));
		nvgStroke(args.vg);
	}
}

void RackWidget::onHover(const HoverEvent& e) {
	// Set before calling children's onHover()
	internal->mousePos = e.pos;

	OpaqueWidget::onHover(e);
}

void RackWidget::onHoverKey(const HoverKeyEvent& e) {
	OpaqueWidget::onHoverKey(e);
	if (e.isConsumed())
		return;

	if (e.action == GLFW_PRESS && e.isKeyCommand(GLFW_KEY_ESCAPE, 0)) {
		if (internal->multiPatching) {
			endMultiPatch();
			e.consume(this);
		}
	}
}

void RackWidget::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.isConsumed())
		return;

	// Cancel multi-patching when clicking the empty rack, but handle the click as usual
	if (e.action == GLFW_PRESS && internal->multiPatching) {
		endMultiPatch();
	}

	if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (e.action == GLFW_PRESS) {
			APP->scene->browser->show();
		}
		e.consume(this);
	}
}

void RackWidget::onDragStart(const DragStartEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		// Deselect all modules
		updateSelectionFromRect();
		internal->selecting = true;
		internal->selectionStart = internal->mousePos;
		internal->selectionEnd = internal->mousePos;
	}
}

void RackWidget::onDragEnd(const DragEndEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		internal->selecting = false;
	}
}

void RackWidget::onDragHover(const DragHoverEvent& e) {
	// Set before calling children's onDragHover()
	internal->mousePos = e.pos;

	if (internal->selecting) {
		internal->selectionEnd = internal->mousePos;
		updateSelectionFromRect();
	}

	OpaqueWidget::onDragHover(e);
}

widget::Widget* RackWidget::getModuleContainer() {
	return internal->moduleContainer;
}

widget::Widget* RackWidget::getPlugContainer() {
	return internal->plugContainer;
}

widget::Widget* RackWidget::getCableContainer() {
	return internal->cableContainer;
}

math::Vec RackWidget::getMousePos() {
	return internal->mousePos;
}

void RackWidget::clear() {
	// Abort multi-patching, discarding its history since its cables are about to be removed
	if (internal->multiPatchHistory) {
		delete internal->multiPatchHistory;
		internal->multiPatchHistory = NULL;
	}
	endMultiPatch();
	// This isn't required because removing all ModuleWidgets should remove all cables, but do it just in case.
	clearCables();
	// Remove ModuleWidgets
	for (ModuleWidget* mw : getModules()) {
		removeModule(mw);
		delete mw;
	}
}

void RackWidget::mergeJson(json_t* rootJ) {
	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return;
	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		// module
		json_t* idJ = json_object_get(moduleJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);
		// TODO Legacy v0.6?
		ModuleWidget* mw = getModule(id);
		if (!mw) {
			WARN("Cannot find ModuleWidget %lld", (long long) id);
			continue;
		}

		// pos
		math::Vec pos = mw->box.pos.minus(RACK_OFFSET);
		pos = pos.div(RACK_GRID_SIZE).round();
		json_t* posJ = json_pack("[i, i]", (int) pos.x, (int) pos.y);
		json_object_set_new(moduleJ, "pos", posJ);
	}

	// Calculate plug orders
	std::map<Widget*, int> plugOrders;
	int plugOrder = 1;
	for (Widget* w : internal->plugContainer->children) {
		plugOrders[w] = plugOrder++;
	}

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (!cablesJ)
		return;
	size_t cableIndex;
	json_t* cableJ;
	json_array_foreach(cablesJ, cableIndex, cableJ) {
		// cable
		json_t* idJ = json_object_get(cableJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);
		CableWidget* cw = getCable(id);
		if (!cw) {
			WARN("Cannot find CableWidget %lld", (long long) id);
			continue;
		}

		cw->mergeJson(cableJ);

		// inputPlugOrder
		auto plugOrderIt = plugOrders.find(cw->inputPlug);
		if (plugOrderIt != plugOrders.end()) {
			int inputPlugOrder = plugOrderIt->second;
			json_object_set_new(cableJ, "inputPlugOrder", json_integer(inputPlugOrder));
		}

		// outputPlugOrder
		plugOrderIt = plugOrders.find(cw->outputPlug);
		if (plugOrderIt != plugOrders.end()) {
			int outputPlugOrder = plugOrderIt->second;
			json_object_set_new(cableJ, "outputPlugOrder", json_integer(outputPlugOrder));
		}
	}
}

void RackWidget::fromJson(json_t* rootJ) {
	// version
	std::string version;
	json_t* versionJ = json_object_get(rootJ, "version");
	if (versionJ)
		version = json_string_value(versionJ);

	bool legacyV05 = false;
	if (string::startsWith(version, "0.3.") || string::startsWith(version, "0.4.") || string::startsWith(version, "0.5.") || version == "dev") {
		legacyV05 = true;
	}

	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return;

	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		// Get module ID
		json_t* idJ = json_object_get(moduleJ, "id");
		int64_t id;
		if (idJ)
			id = json_integer_value(idJ);
		else
			id = moduleIndex;

		// Get Module
		engine::Module* module = APP->engine->getModule(id);
		if (!module) {
			WARN("Cannot find Module %lld", (long long) id);
			continue;
		}

		// Create ModuleWidget
		INFO("Creating module widget %s", module->model->getFullName().c_str());
		ModuleWidget* mw = module->model->createModuleWidget(module);

		// pos
		json_t* posJ = json_object_get(moduleJ, "pos");
		double x = 0.0, y = 0.0;
		json_unpack(posJ, "[F, F]", &x, &y);
		math::Vec pos = math::Vec(x, y);
		if (legacyV05) {
			// In <=v0.5, positions were in pixel units
		}
		else {
			pos = pos.mult(RACK_GRID_SIZE);
		}
		pos = pos.plus(RACK_OFFSET);
		setModulePosForce(mw, pos);

		internal->moduleContainer->addChild(mw);
	}

	updateExpanders();

	std::map<Widget*, int> plugOrders;

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	// In <=v0.6, cables were called wires
	if (!cablesJ)
		cablesJ = json_object_get(rootJ, "wires");
	if (!cablesJ)
		return;
	size_t cableIndex;
	json_t* cableJ;
	json_array_foreach(cablesJ, cableIndex, cableJ) {
		// Get cable ID
		json_t* idJ = json_object_get(cableJ, "id");
		int64_t id;
		// In <=v0.6, the cable ID was the index in the array.
		if (idJ)
			id = json_integer_value(idJ);
		else
			id = cableIndex;

		// Get Cable
		engine::Cable* cable = APP->engine->getCable(id);
		if (!cable) {
			WARN("Cannot find Cable %lld", (long long) id);
			continue;
		}

		// Create CableWidget
		CableWidget* cw = new CableWidget;
		try {
			cw->setCable(cable);
			cw->fromJson(cableJ);
		}
		catch (Exception& e) {
			delete cw;
			// If creating CableWidget fails, remove Cable from Engine.
			APP->engine->removeCable(cable);
			delete cable;
			continue;
		}
		addCable(cw);

		// inputPlugOrder
		json_t* inputPlugOrderJ = json_object_get(cableJ, "inputPlugOrder");
		if (inputPlugOrderJ) {
			plugOrders[cw->inputPlug] = json_integer_value(inputPlugOrderJ);
		}

		// outputPlugOrder
		json_t* outputPlugOrderJ = json_object_get(cableJ, "outputPlugOrder");
		if (outputPlugOrderJ) {
			plugOrders[cw->outputPlug] = json_integer_value(outputPlugOrderJ);
		}
	}

	// Reorder plugs, approximately O(n log(n) log(n))
	internal->plugContainer->children.sort([&](Widget* w1, Widget* w2) {
		return get(plugOrders, w1, 0) < get(plugOrders, w2, 0);
	});
}

struct PasteJsonResult {
	/** Old module ID -> new module */
	std::map<int64_t, ModuleWidget*> newModules;
};
static PasteJsonResult RackWidget_pasteJson(RackWidget* that, json_t* rootJ, history::ComplexAction* complexAction) {
	that->deselectAll();

	std::map<int64_t, ModuleWidget*> newModules;
	math::Vec minPos(INFINITY, INFINITY);
	math::Vec maxPos(-INFINITY, -INFINITY);

	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return {};

	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		json_t* idJ = json_object_get(moduleJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);

		engine::Module::jsonStripIds(moduleJ);

		ModuleWidget* mw;
		try {
			mw = moduleWidgetFromJson(moduleJ);
		}
		catch (Exception& e) {
			WARN("%s", e.what());
			continue;
		}
		assert(mw);
		assert(mw->module);

		APP->engine->addModule(mw->module);

		// pos
		json_t* posJ = json_object_get(moduleJ, "pos");
		double x = 0.0, y = 0.0;
		json_unpack(posJ, "[F, F]", &x, &y);
		math::Vec pos = math::Vec(x, y);
		mw->box.pos = pos * RACK_GRID_SIZE + RACK_OFFSET;
		minPos = minPos.min(mw->box.getTopLeft());
		maxPos = maxPos.max(mw->box.getBottomRight());

		that->internal->moduleContainer->addChild(mw);
		that->select(mw);

		newModules[id] = mw;
	}

	// Adjust center of selection to appear at center of rack view
	math::Vec selectionCenter = (minPos + maxPos) / 2;
	math::Vec mousePos = that->internal->mousePos;
	math::Vec deltaPos = ((mousePos - selectionCenter) / RACK_GRID_SIZE).round() * RACK_GRID_SIZE;

	for (auto pair : newModules) {
		ModuleWidget* mw = pair.second;
		mw->box.pos += deltaPos;
	}

	// This calls updateExpanders()
	that->setSelectionPosNearest(math::Vec(0, 0));

	// Add positioned selected modules to history
	for (ModuleWidget* mw : that->getSelected()) {
		// history::ModuleAdd
		history::ModuleAdd* h = new history::ModuleAdd;
		h->setModule(mw);
		complexAction->push(h);
	}

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (cablesJ) {
		size_t cableIndex;
		json_t* cableJ;
		json_array_foreach(cablesJ, cableIndex, cableJ) {
			engine::Cable::jsonStripIds(cableJ);

			// Overwrite old module IDs with new module IDs
			json_t* inputModuleIdJ = json_object_get(cableJ, "inputModuleId");
			if (!inputModuleIdJ)
				continue;
			int64_t inputModuleId = json_integer_value(inputModuleIdJ);
			auto inputModuleIdIt = newModules.find(inputModuleId);
			if (inputModuleIdIt == newModules.end())
				continue;
			inputModuleId = inputModuleIdIt->second->module->id;
			json_object_set_new(cableJ, "inputModuleId", json_integer(inputModuleId));

			json_t* outputModuleIdJ = json_object_get(cableJ, "outputModuleId");
			if (!outputModuleIdJ)
				continue;
			int64_t outputModuleId = json_integer_value(outputModuleIdJ);
			auto outputModuleIdIt = newModules.find(outputModuleId);
			if (outputModuleIdIt == newModules.end())
				continue;
			outputModuleId = outputModuleIdIt->second->module->id;
			json_object_set_new(cableJ, "outputModuleId", json_integer(outputModuleId));

			// Create Cable
			engine::Cable* cable = new engine::Cable;
			try {
				cable->fromJson(cableJ);
				APP->engine->addCable(cable);
			}
			catch (Exception& e) {
				WARN("Cannot paste cable: %s", e.what());
				delete cable;
				continue;
			}

			// Create CableWidget
			app::CableWidget* cw = new app::CableWidget;
			cw->setCable(cable);
			cw->fromJson(cableJ);
			that->addCable(cw);

			// history::CableAdd
			history::CableAdd* h = new history::CableAdd;
			h->setCable(cw);
			complexAction->push(h);
		}
	}

	return {newModules};
}

void RackWidget::pasteJsonAction(json_t* rootJ) {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.pasteModules");
	DEFER({
		if (!complexAction->isEmpty())
			APP->history->push(complexAction);
		else
			delete complexAction;
	});

	RackWidget_pasteJson(this, rootJ, complexAction);
}

void RackWidget::pasteModuleJsonAction(json_t* moduleJ) {
	engine::Module::jsonStripIds(moduleJ);

	ModuleWidget* mw;
	try {
		mw = moduleWidgetFromJson(moduleJ);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		return;
	}
	assert(mw);
	assert(mw->module);

	history::ComplexAction* h = new history::ComplexAction;
	h->name = string::translate("RackWidget.history.pasteModule");

	APP->engine->addModule(mw->module);

	updateModuleOldPositions();
	addModuleAtMouse(mw);
	h->push(getModuleDragAction());

	// history::ModuleAdd
	history::ModuleAdd* ha = new history::ModuleAdd;
	ha->setModule(mw);
	h->push(ha);

	APP->history->push(h);
}

void RackWidget::pasteClipboardAction() {
	const char* json = glfwGetClipboardString(APP->window->win);
	if (!json) {
		WARN("Could not get text from clipboard.");
		return;
	}

	json_error_t error;
	json_t* rootJ = json_loads(json, 0, &error);
	if (!rootJ) {
		WARN("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
		return;
	}
	DEFER({json_decref(rootJ);});

	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (modulesJ) {
		// JSON is a selection of modules
		pasteJsonAction(rootJ);
	}
	else {
		// JSON is a single module
		pasteModuleJsonAction(rootJ);
	}
}

void RackWidget::addModule(ModuleWidget* m) {
	assert(m);

	// Module must be 3U high and at least 1HP wide
	if (m->box.size.x < RACK_GRID_WIDTH / 2)
		throw Exception("Module %s width is %g px, must be at least %g px", m->model->getFullName().c_str(), m->box.size.x, RACK_GRID_WIDTH);

	if (m->box.size.y != RACK_GRID_HEIGHT)
		throw Exception("Module %s height is %g px, must be %g px", m->model->getFullName().c_str(), m->box.size.y, RACK_GRID_HEIGHT);

	internal->moduleContainer->addChild(m);

	updateExpanders();
}

void RackWidget::addModuleAtMouse(ModuleWidget* mw) {
	assert(mw);
	// Move module nearest to the mouse position
	math::Vec pos = internal->mousePos.minus(mw->box.size.div(2));

	if (settings::squeezeModules)
		setModulePosSqueeze(mw, pos);
	else
		setModulePosNearest(mw, pos);

	addModule(mw);
}

void RackWidget::removeModule(ModuleWidget* m) {
	// Unset touchedParamWidget
	if (touchedParam) {
		ModuleWidget* touchedModule = touchedParam->getAncestorOfType<ModuleWidget>();
		if (touchedModule == m)
			touchedParam = NULL;
	}

	// Disconnect cables
	m->disconnect();

	// Deselect module if selected
	internal->selectedModules.erase(m);

	// Remove module from ModuleContainer
	internal->moduleContainer->removeChild(m);

	updateExpanders();
}

ModuleWidget* RackWidget::getModule(int64_t moduleId) {
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		if (mw->module->id == moduleId)
			return mw;
	}
	return NULL;
}

std::vector<ModuleWidget*> RackWidget::getModules() {
	std::vector<ModuleWidget*> mws;
	mws.reserve(internal->moduleContainer->children.size());
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		mws.push_back(mw);
	}
	return mws;
}

bool RackWidget::hasModules() {
	return internal->moduleContainer->children.empty();
}

bool RackWidget::requestModulePos(ModuleWidget* mw, math::Vec pos) {
	// Check intersection with other modules
	math::Rect mwBox = math::Rect(pos, mw->box.size);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		// Don't intersect with self
		if (mw == w2)
			continue;
		// Check intersection
		math::Rect w2Box = w2->box;
		if (mwBox.intersects(w2Box))
			return false;
	}

	// Accept requested position
	mw->setPosition(mwBox.pos);
	updateExpanders();
	return true;
}

static math::Vec eachNearestGridPos(math::Vec pos, std::function<bool(math::Vec pos)> f) {
	math::Vec leftPos = (pos / RACK_GRID_SIZE).round();
	math::Vec rightPos = leftPos + math::Vec(1, 0);

	while (true) {
		if (f(leftPos * RACK_GRID_SIZE))
			return leftPos * RACK_GRID_SIZE;
		leftPos.x -= 1;

		if (f(rightPos * RACK_GRID_SIZE))
			return rightPos * RACK_GRID_SIZE;
		rightPos.x += 1;
	}

	assert(false);
	return math::Vec();
}

void RackWidget::setModulePosNearest(ModuleWidget* mw, math::Vec pos) {
	eachNearestGridPos(pos, [&](math::Vec pos) -> bool {
		return requestModulePos(mw, pos);
	});
}

static bool compareModuleLeft(ModuleWidget* a, ModuleWidget* b) {
	return a->getGridBox().getLeft() < b->getGridBox().getLeft();
}

void RackWidget::setModulePosForce(ModuleWidget* mw, math::Vec pos) {
	math::Rect mwBox;
	mwBox.pos = ((pos - RACK_OFFSET) / RACK_GRID_SIZE).round();
	mwBox.size = mw->getGridSize();

	// Collect modules to the left and right of new pos
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = (ModuleWidget*) w2;
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	// Set module position
	mw->setGridPosition(mwBox.pos);

	// Shove left modules
	math::Vec cursor = mwBox.getTopLeft();
	for (auto it = leftModules.rbegin(); it != leftModules.rend(); it++) {
		ModuleWidget* mw2 = (ModuleWidget*) *it;
		math::Rect mw2Box = mw2->getGridBox();

		if (mw2Box.getRight() <= cursor.x)
			break;

		mw2Box.pos.x = cursor.x - mw2Box.size.x;
		mw2->setGridPosition(mw2Box.pos);
		cursor.x = mw2Box.getLeft();
	}

	// Shove right modules
	cursor = mwBox.getTopRight();
	for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
		ModuleWidget* mw2 = (ModuleWidget*) *it;
		math::Rect mw2Box = mw2->getGridBox();

		if (mw2Box.getLeft() >= cursor.x)
			break;

		mw2Box.pos.x = cursor.x;
		mw2->setGridPosition(mw2Box.pos);
		cursor.x = mw2Box.getRight();
	}

	updateExpanders();
}

void RackWidget::setModulePosSqueeze(ModuleWidget* mw, math::Vec pos) {
	// Reset modules to their old positions, including this module
	for (auto& pair : internal->moduleOldPositions) {
		widget::Widget* w2 = pair.first;
		w2->box.pos = pair.second;
	}

	unsqueezeModulePos(mw);
	squeezeModulePos(mw, pos);

	updateExpanders();
}

void RackWidget::squeezeModulePos(ModuleWidget* mw, math::Vec pos) {
	math::Rect mwBox;
	mwBox.pos = ((pos - RACK_OFFSET) / RACK_GRID_SIZE).round();
	mwBox.size = mw->getGridSize();

	// Collect modules to the left and right of new pos
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	ModuleWidget* leftModule = leftModules.empty() ? NULL : *leftModules.rbegin();
	ModuleWidget* rightModule = rightModules.empty() ? NULL : *rightModules.begin();

	// If there isn't enough space between the last leftModule and first rightModule, place module to the right of the leftModule and shove right modules.
	if (leftModule && rightModule && leftModule->getGridBox().getRight() + mwBox.getWidth() > rightModule->getGridBox().getLeft()) {
		mwBox.pos.x = leftModule->getGridBox().getRight();

		// Shove right modules
		float xRight = mwBox.getRight();
		for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
			widget::Widget* w2 = *it;
			ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
			math::Rect mw2Box = mw2->getGridBox();
			// Break when module no longer needs to be shoved
			if (mw2Box.getLeft() >= xRight)
				break;
			// Shove module to the right of the last module
			math::Rect newBox = mw2Box;
			newBox.pos.x = xRight;
			mw2->setGridPosition(newBox.pos);
			xRight = newBox.getRight();
		}
	}
	// Place right of leftModule
	else if (leftModule && leftModule->getGridBox().getRight() > mwBox.getLeft()) {
		mwBox.pos.x = leftModule->getGridBox().getRight();
	}
	// Place left of rightModule
	else if (rightModule && rightModule->getGridBox().getLeft() < mwBox.getRight()) {
		mwBox.pos.x = rightModule->getGridBox().getLeft() - mwBox.getWidth();
	}

	// Commit new pos
	mw->setGridPosition(mwBox.pos);
}

void RackWidget::unsqueezeModulePos(ModuleWidget* mw) {
	math::Rect mwBox = mw->getGridBox();

	// Collect modules to the left and right of old pos, including this module.
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	// Immediate right/left modules
	ModuleWidget* leftModule = leftModules.empty() ? NULL : *leftModules.rbegin();
	ModuleWidget* rightModule = rightModules.empty() ? NULL : *rightModules.begin();

	// Shove right modules back to empty space left by module.
	if (leftModule && rightModule && (leftModule != mw) && (rightModule != mw) && leftModule->getGridBox().getRight() >= mwBox.getLeft() && rightModule->getGridBox().getLeft() <= mwBox.getRight()) {
		float xLeft = mwBox.getLeft();
		float xRight = mwBox.getRight();
		for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
			widget::Widget* w2 = *it;
			ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
			math::Rect mw2Box = mw2->getGridBox();
			// Break when module is no longer touching
			if (xRight < mw2Box.getLeft())
				break;
			// Shove module to the left
			math::Rect newBox = mw2Box;
			newBox.pos.x = xLeft;
			mw2->setGridPosition(newBox.pos);
			xLeft = newBox.getRight();
			xRight = mw2Box.getRight();
		}
	}
}

void RackWidget::updateModuleOldPositions() {
	internal->moduleOldPositions.clear();
	for (ModuleWidget* mw : getModules()) {
		internal->moduleOldPositions[mw] = mw->getPosition();
	}
}

history::ComplexAction* RackWidget::getModuleDragAction() {
	history::ComplexAction* h = new history::ComplexAction;
	h->name = string::translate("RackWidget.history.moveModules");

	for (ModuleWidget* mw : getModules()) {
		// Create ModuleMove action if the module was moved.
		auto it = internal->moduleOldPositions.find(mw);
		if (it == internal->moduleOldPositions.end())
			continue;
		math::Vec oldPos = it->second;
		if (!oldPos.equals(mw->box.pos)) {
			history::ModuleMove* mmh = new history::ModuleMove;
			mmh->moduleId = mw->module->id;
			mmh->oldPos = oldPos;
			mmh->newPos = mw->box.pos;
			h->push(mmh);
		}
	}

	return h;
}

void RackWidget::updateSelectionFromRect() {
	math::Rect selectionBox = math::Rect::fromCorners(internal->selectionStart, internal->selectionEnd);
	deselectAll();
	for (ModuleWidget* mw : getModules()) {
		bool selected = internal->selecting && selectionBox.intersects(mw->box);
		if (selected)
			select(mw);
	}
}

void RackWidget::selectAll() {
	internal->selectedModules.clear();
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		internal->selectedModules.insert(mw);
	}
}

void RackWidget::deselectAll() {
	internal->selectedModules.clear();
}

void RackWidget::select(ModuleWidget* mw, bool selected) {
	if (selected) {
		internal->selectedModules.insert(mw);
	}
	else {
		auto it = internal->selectedModules.find(mw);
		if (it != internal->selectedModules.end())
			internal->selectedModules.erase(it);
	}
}

bool RackWidget::hasSelection() {
	return !internal->selectedModules.empty();
}

const std::set<ModuleWidget*>& RackWidget::getSelected() {
	return internal->selectedModules;
}

bool RackWidget::isSelected(ModuleWidget* mw) {
	auto it = internal->selectedModules.find(mw);
	return (it != internal->selectedModules.end());
}

json_t* RackWidget::selectionToJson(bool cables) {
	json_t* rootJ = json_object();

	std::set<engine::Module*> modules;

	// modules
	json_t* modulesJ = json_array();
	for (ModuleWidget* mw : getSelected()) {
		json_t* moduleJ = mw->toJson();

		// pos
		math::Vec pos = mw->box.pos.minus(RACK_OFFSET);
		pos = pos.div(RACK_GRID_SIZE).round();
		json_t* posJ = json_pack("[i, i]", (int) pos.x, (int) pos.y);
		json_object_set_new(moduleJ, "pos", posJ);

		json_array_append_new(modulesJ, moduleJ);
		modules.insert(mw->getModule());
	}
	json_object_set_new(rootJ, "modules", modulesJ);

	if (cables) {
		// cables
		json_t* cablesJ = json_array();
		// Only add complete cables to JSON
		for (CableWidget* cw : getCompleteCables()) {
			engine::Cable* cable = cw->getCable();
			if (!cable || !cable->inputModule || !cable->outputModule)
				continue;
			const auto inputIt = modules.find(cable->inputModule);
			if (inputIt == modules.end())
				continue;
			const auto outputIt = modules.find(cable->outputModule);
			if (outputIt == modules.end())
				continue;

			json_t* cableJ = cable->toJson();
			cw->mergeJson(cableJ);

			json_array_append_new(cablesJ, cableJ);
		}
		json_object_set_new(rootJ, "cables", cablesJ);
	}

	return rootJ;
}

static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";

void RackWidget::loadSelection(std::string path) {
	FILE* file = std::fopen(path.c_str(), "r");
	if (!file)
		throw Exception("Could not load selection file %s", path.c_str());
	DEFER({std::fclose(file);});

	INFO("Loading selection %s", path.c_str());

	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	if (!rootJ)
		throw Exception("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
	DEFER({json_decref(rootJ);});

	// Set mouse position to center of rack viewport, so selection is placed in view.
	internal->mousePos = getViewport().getCenter();

	pasteJsonAction(rootJ);
}

void RackWidget::loadSelectionDialog() {
	std::string dir = settings::lastSelectionDirectory;

	// Use fallback <Rack user dir>/selections
	if (dir == "" || !system::isDirectory(dir)) {
		dir = asset::user("selections");
		system::createDirectory(dir);
	}

	osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
	DEFER({osdialog_filters_free(filters);});

	char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
	if (!pathC) {
		// Cancel silently
		return;
	}
	std::string path = pathC;
	std::free(pathC);

	try {
		loadSelection(path);
	}
	catch (Exception& e) {
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, e.what());
	}

	settings::lastSelectionDirectory = system::getDirectory(path);
}

void RackWidget::saveSelection(std::string path) {
	INFO("Saving selection %s", path.c_str());

	json_t* rootJ = selectionToJson();
	assert(rootJ);
	DEFER({json_decref(rootJ);});

	engine::Module::jsonStripIds(rootJ);

	FILE* file = std::fopen(path.c_str(), "w");
	if (!file) {
		std::string message = string::f(string::translate("RackWidget.saveSelectionFailed"), path);
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		return;
	}
	DEFER({std::fclose(file);});

	json_dumpf(rootJ, file, JSON_INDENT(2));
}

void RackWidget::saveSelectionDialog() {
	std::string dir = settings::lastSelectionDirectory;

	// Use fallback <Rack user dir>/selections
	if (dir == "" || !system::isDirectory(dir)) {
		dir = asset::user("selections");
		system::createDirectory(dir);
	}

	osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
	DEFER({osdialog_filters_free(filters);});

	char* pathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled.vcvs", filters);
	if (!pathC) {
		// No path selected
		return;
	}
	std::string path = pathC;
	std::free(pathC);

	// Automatically append .vcvs extension
	if (system::getExtension(path) != ".vcvs")
		path += ".vcvs";

	saveSelection(path);

	settings::lastSelectionDirectory = system::getDirectory(path);
}

void RackWidget::copyClipboardSelection() {
	json_t* rootJ = selectionToJson();
	DEFER({json_decref(rootJ);});
	char* moduleJson = json_dumps(rootJ, JSON_INDENT(2));
	DEFER({std::free(moduleJson);});
	glfwSetClipboardString(APP->window->win, moduleJson);
}

void RackWidget::resetSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.resetModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);

		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->moduleId = mw->module->id;
		h->oldModuleJ = mw->toJson();

		APP->engine->resetModule(mw->module);

		h->newModuleJ = mw->toJson();
		complexAction->push(h);
	}

	APP->history->push(complexAction);
}

void RackWidget::randomizeSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.randomizeModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);

		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->moduleId = mw->module->id;
		h->oldModuleJ = mw->toJson();

		APP->engine->randomizeModule(mw->module);

		h->newModuleJ = mw->toJson();
		complexAction->push(h);
	}

	APP->history->push(complexAction);
}

void RackWidget::disconnectSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.disconnectCables");

	for (ModuleWidget* mw : getSelected()) {
		mw->appendDisconnectActions(complexAction);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;
}

void RackWidget::cloneSelectionAction(bool cloneCables) {
	json_t* rootJ = selectionToJson(cloneCables);
	DEFER({json_decref(rootJ);});

	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.duplicateModules");
	DEFER({
		if (!complexAction->isEmpty())
			APP->history->push(complexAction);
		else
			delete complexAction;
	});

	auto p = RackWidget_pasteJson(this, rootJ, complexAction);

	// Clone cables attached to inputs of selected modules but outputs of non-selected modules
	if (cloneCables) {
		for (CableWidget* cw : getCompleteCables()) {
			auto inputIt = p.newModules.find(cw->getCable()->inputModule->id);
			if (inputIt == p.newModules.end())
				continue;

			auto outputIt = p.newModules.find(cw->getCable()->outputModule->id);
			if (outputIt != p.newModules.end())
				continue;

			engine::Module* clonedInputModule = inputIt->second->module;

			// Create cable attached to cloned ModuleWidget's input
			engine::Cable* clonedCable = new engine::Cable;
			clonedCable->inputModule = clonedInputModule;
			clonedCable->inputId = cw->cable->inputId;
			clonedCable->outputModule = cw->cable->outputModule;
			clonedCable->outputId = cw->cable->outputId;
			APP->engine->addCable(clonedCable);

			app::CableWidget* clonedCw = new app::CableWidget;
			clonedCw->setCable(clonedCable);
			clonedCw->color = cw->color;
			APP->scene->rack->addCable(clonedCw);

			// history::CableAdd
			history::CableAdd* hca = new history::CableAdd;
			hca->setCable(clonedCw);
			complexAction->push(hca);
		}
	}
}

void RackWidget::bypassSelectionAction(bool bypassed) {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = bypassed ? string::translate("RackWidget.history.bypassModules") : string::translate("RackWidget.history.unbypassModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);
		if (mw->module->isBypassed() == bypassed)
			continue;

		// history::ModuleBypass
		history::ModuleBypass* h = new history::ModuleBypass;
		h->moduleId = mw->module->id;
		h->bypassed = bypassed;
		complexAction->push(h);

		APP->engine->bypassModule(mw->module, bypassed);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;
}

bool RackWidget::isSelectionBypassed() {
	for (ModuleWidget* mw : getSelected()) {
		if (!mw->getModule()->isBypassed())
			return false;
	}
	return true;
}

void RackWidget::deleteSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.deleteModules");

	// Copy selected set since removing ModuleWidgets modifies it.
	std::set<ModuleWidget*> selectedModules = getSelected();
	for (ModuleWidget* mw : selectedModules) {
		mw->appendDisconnectActions(complexAction);

		// history::ModuleRemove
		history::ModuleRemove* moduleRemove = new history::ModuleRemove;
		moduleRemove->setModule(mw);
		complexAction->push(moduleRemove);

		removeModule(mw);
		delete mw;
	}

	APP->history->push(complexAction);
}

bool RackWidget::requestSelectionPos(math::Vec delta) {
	// Calculate new positions
	std::map<widget::Widget*, math::Rect> mwBoxes;
	for (ModuleWidget* mw : getSelected()) {
		math::Rect mwBox = mw->box;
		mwBox.pos += delta;
		mwBoxes[mw] = mwBox;
	}

	// Check intersection with other modules
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		// Don't intersect with selected modules
		auto it = mwBoxes.find(w2);
		if (it != mwBoxes.end())
			continue;
		math::Rect w2Box = w2->box;
		// Check intersection with all selected modules
		for (const auto& pair : mwBoxes) {
			if (pair.second.intersects(w2Box))
				return false;
		}
	}

	// Accept requested position
	for (const auto& pair : mwBoxes) {
		pair.first->setPosition(pair.second.pos);
	}
	updateExpanders();
	return true;
}

void RackWidget::setSelectionPosNearest(math::Vec delta) {
	eachNearestGridPos(delta, [&](math::Vec delta) -> bool {
		return requestSelectionPos(delta);
	});
}

void RackWidget::appendSelectionContextMenu(ui::Menu* menu) {
	int n = getSelected().size();
	menu->addChild(createMenuLabel(n == 1 ? string::translate("RackWidget.selectedOne") : string::f(string::translate("RackWidget.selectedMany"), n)));

	// Enable alwaysConsume of menu items if the number of selected modules changes

	// Select all
	menu->addChild(createMenuItem(string::translate("RackWidget.selectAll"), widget::getKeyCommandName(GLFW_KEY_A, RACK_MOD_CTRL), [=]() {
		selectAll();
	}, false, true));

	// Deselect
	menu->addChild(createMenuItem(string::translate("RackWidget.deselect"), widget::getKeyCommandName(GLFW_KEY_A, RACK_MOD_CTRL | GLFW_MOD_SHIFT), [=]() {
		deselectAll();
	}, n == 0, true));

	// Copy
	menu->addChild(createMenuItem(string::translate("RackWidget.copy"), widget::getKeyCommandName(GLFW_KEY_C, RACK_MOD_CTRL), [=]() {
		copyClipboardSelection();
	}, n == 0));

	// Paste
	menu->addChild(createMenuItem(string::translate("RackWidget.paste"), widget::getKeyCommandName(GLFW_KEY_V, RACK_MOD_CTRL), [=]() {
		pasteClipboardAction();
	}, false, true));

	// Save
	menu->addChild(createMenuItem(string::translate("RackWidget.saveAs"), "", [=]() {
		saveSelectionDialog();
	}, n == 0));

	// Initialize
	menu->addChild(createMenuItem(string::translate("RackWidget.initialize"), widget::getKeyCommandName(GLFW_KEY_I, RACK_MOD_CTRL), [=]() {
		resetSelectionAction();
	}, n == 0));

	// Randomize
	menu->addChild(createMenuItem(string::translate("RackWidget.randomize"), widget::getKeyCommandName(GLFW_KEY_R, RACK_MOD_CTRL), [=]() {
		randomizeSelectionAction();
	}, n == 0));

	// Disconnect cables
	menu->addChild(createMenuItem(string::translate("RackWidget.disconnectCables"), widget::getKeyCommandName(GLFW_KEY_U, RACK_MOD_CTRL), [=]() {
		disconnectSelectionAction();
	}, n == 0));

	// Bypass
	std::string bypassText = widget::getKeyCommandName(GLFW_KEY_E, RACK_MOD_CTRL);
	bool bypassed = (n > 0) && isSelectionBypassed();
	if (bypassed)
		bypassText += " " CHECKMARK_STRING;
	menu->addChild(createMenuItem(string::translate("RackWidget.bypass"), bypassText, [=]() {
		bypassSelectionAction(!bypassed);
	}, n == 0, true));

	// Duplicate
	menu->addChild(createMenuItem(string::translate("RackWidget.duplicate"), widget::getKeyCommandName(GLFW_KEY_D, RACK_MOD_CTRL), [=]() {
		cloneSelectionAction(false);
	}, n == 0));

	// Duplicate with cables
	menu->addChild(createMenuItem("└ " + string::translate("RackWidget.duplicateWithCables"), widget::getKeyCommandName(GLFW_KEY_D, RACK_MOD_CTRL | GLFW_MOD_SHIFT), [=]() {
		cloneSelectionAction(true);
	}, n == 0));

	// Delete
	menu->addChild(createMenuItem(string::translate("RackWidget.delete"), widget::getKeyCommandName(GLFW_KEY_BACKSPACE, 0) + "/" + widget::getKeyCommandName(GLFW_KEY_DELETE, 0), [=]() {
		deleteSelectionAction();
	}, n == 0, true));
}

void RackWidget::clearCables() {
	// Since cables manage plugs, all plugs will be removed from plugContainer
	internal->cableContainer->clearChildren();
}

void RackWidget::clearCablesAction() {
	// Add CableRemove for every cable to a ComplexAction
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.clearCables");

	for (CableWidget* cw : getCompleteCables()) {
		// history::CableRemove
		history::CableRemove* h = new history::CableRemove;
		h->setCable(cw);
		complexAction->push(h);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;

	clearCables();
}

void RackWidget::clearCablesOnPort(PortWidget* port) {
	for (CableWidget* cw : getCablesOnPort(port)) {
		removeCable(cw);
		delete cw;
	}
}

void RackWidget::addCable(CableWidget* cw) {
	internal->cableContainer->addChild(cw);
}

void RackWidget::removeCable(CableWidget* cw) {
	internal->cableContainer->removeChild(cw);
}

CableWidget* RackWidget::getIncompleteCable() {
	for (auto it = internal->cableContainer->children.rbegin(); it != internal->cableContainer->children.rend(); it++) {
		CableWidget* cw = dynamic_cast<CableWidget*>(*it);
		assert(cw);
		if (!cw->isComplete() && !isMultiPatchCable(cw))
			return cw;
	}
	return NULL;
}

PlugWidget* RackWidget::getTopPlug(PortWidget* port) {
	assert(port);
	for (auto it = internal->plugContainer->children.rbegin(); it != internal->plugContainer->children.rend(); it++) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(*it);
		assert(plug);
		CableWidget* cw = plug->getCable();
		// Multi-patch cables are never grabbed, so they are never on top
		if (isMultiPatchCable(cw))
			continue;
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			return plug;
	}
	return NULL;
}

CableWidget* RackWidget::getTopCable(PortWidget* port) {
	PlugWidget* plug = getTopPlug(port);
	if (plug)
		return plug->getCable();
	return NULL;
}

CableWidget* RackWidget::getCable(int64_t cableId) {
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (!cw->cable)
			continue;
		if (cw->cable->id == cableId)
			return cw;
	}
	return NULL;
}

CableWidget* RackWidget::getCable(PortWidget* outputPort, PortWidget* inputPort) {
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (cw->outputPort == outputPort && cw->inputPort == inputPort)
			return cw;
	}
	return NULL;
}

std::vector<CableWidget*> RackWidget::getCables() {
	std::vector<CableWidget*> cws;
	cws.reserve(internal->cableContainer->children.size());
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCompleteCables() {
	std::vector<CableWidget*> cws;
	// Assume that most cables are complete, so pre-allocate and shrink vector.
	cws.reserve(internal->cableContainer->children.size());
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (cw->isComplete())
			cws.push_back(cw);
	}
	cws.shrink_to_fit();
	return cws;
}

std::vector<CableWidget*> RackWidget::getIncompleteCables() {
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (!cw->isComplete() && !isMultiPatchCable(cw))
			cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCablesOnPort(PortWidget* port) {
	assert(port);
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->plugContainer->children) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(w);
		assert(plug);
		CableWidget* cw = plug->getCable();
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCompleteCablesOnPort(PortWidget* port) {
	assert(port);
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->plugContainer->children) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(w);
		assert(plug);
		CableWidget* cw = plug->getCable();
		if (!cw->isComplete())
			continue;
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			cws.push_back(cw);
	}
	return cws;
}


int RackWidget::getNextCableColorId() {
	return internal->nextCableColorId;
}


void RackWidget::setNextCableColorId(int id) {
	internal->nextCableColorId = id;
}


NVGcolor RackWidget::getNextCableColor() {
	if (settings::cableColors.empty())
		return color::WHITE;

	int id = internal->nextCableColorId;
	if (settings::cableAutoRotate) {
		internal->nextCableColorId++;
	}
	if (id >= (int) settings::cableColors.size())
		id = 0;
	if (internal->nextCableColorId >= (int) settings::cableColors.size())
		internal->nextCableColorId = 0;
	return settings::cableColors[id];
}


ParamWidget* RackWidget::getTouchedParam() {
	return touchedParam;
}


void RackWidget::setTouchedParam(ParamWidget* pw) {
	touchedParam = pw;
}

/** Creates a cable attached to the port on one end and following the cursor on the other. */
static CableWidget* createMultiPatchCable(RackWidget* rack, PortWidget* pw) {
	CableWidget* cw = new CableWidget;
	cw->color = rack->getNextCableColor();
	cw->getPort(pw->type) = pw;
	rack->addCable(cw);
	return cw;
}

/** Returns the port's top cable if it can be unplugged and carried by the cursor. */
static CableWidget* getMultiPatchGrabCable(RackWidget* rack, PortWidget* pw) {
	CableWidget* cw = rack->getTopCable(pw);
	if (cw && !cw->isComplete())
		cw = NULL;
	return cw;
}

/** Collects a cable from the port, leaving its free end on the cursor.
MULTI_PATCH_GRAB unplugs the port's top cable and MULTI_PATCH_CLONE duplicates it, both handing
over the plug that was in this port. Otherwise a new cable is attached to the port, handing over a
plug for the opposite type instead.
*/
static void collectMultiPatchCable(RackWidget* rack, RackWidget::Internal::MultiPatchPort& p, PortWidget* pw, RackWidget::MultiPatchMode mode) {
	engine::Port::Type otherType = (pw->type == engine::Port::INPUT) ? engine::Port::OUTPUT : engine::Port::INPUT;
	CableWidget* topCw = (mode != RackWidget::MULTI_PATCH_CREATE) ? getMultiPatchGrabCable(rack, pw) : NULL;
	CableWidget* cw;

	if (topCw && mode == RackWidget::MULTI_PATCH_GRAB) {
		// Remember how to plug the cable back in if it is never patched
		p.grabbedPort = pw;
		p.grabHistory = new history::CableRemove;
		p.grabHistory->setCable(topCw);
		// Unplug this end, leaving the cable hanging from its other end
		cw = topCw;
		cw->getPort(pw->type) = NULL;
		cw->updateCable();
	}
	else if (topCw) {
		// Duplicate the cable, keeping the end this port isn't plugged into, and its color
		cw = new CableWidget;
		cw->color = topCw->color;
		cw->getPort(otherType) = topCw->getPort(otherType);
		rack->addCable(cw);
	}
	else {
		cw = createMultiPatchCable(rack, pw);
	}

	p.port = pw;
	p.cable = cw;
}

/** Plugs a grabbed cable back in where it came from, or removes a cable that was created.
Leaves the entry holding no cable.
*/
static void releaseMultiPatchCable(RackWidget* rack, RackWidget::Internal::MultiPatchPort& p) {
	CableWidget* cw = p.cable.get();
	PortWidget* grabbedPort = p.grabbedPort.get();
	p.cable.set(NULL);
	p.grabbedPort.set(NULL);

	// A grabbed cable is plugged back in, so its removal never reaches the history
	if (p.grabHistory) {
		delete p.grabHistory;
		p.grabHistory = NULL;
		if (cw && grabbedPort) {
			cw->hoveredOutputPort = NULL;
			cw->hoveredInputPort = NULL;
			cw->getPort(grabbedPort->type) = grabbedPort;
			cw->updateCable();
			return;
		}
	}

	if (cw) {
		rack->removeCable(cw);
		delete cw;
	}
}

void RackWidget::endMultiPatch() {
	// Reset the state before releasing the cables, so they are no longer multi-patch cables
	std::vector<Internal::MultiPatchPort> ports;
	ports.swap(internal->multiPatchPorts);
	internal->multiPatching = false;
	internal->multiPatchGrabMode = false;
	internal->multiPatchIndex = 0;

	// Plug back or remove the cables that are still following the cursor
	for (Internal::MultiPatchPort& p : ports) {
		releaseMultiPatchCable(this, p);
	}

	// Push history
	history::ComplexAction* h = internal->multiPatchHistory;
	internal->multiPatchHistory = NULL;
	if (!h) {
		// Not multi-patching, or the history was discarded
	}
	else if (h->isEmpty()) {
		// No cables were patched, don't push anything
		delete h;
	}
	else if (h->actions.size() == 1) {
		// Push single history action
		APP->history->push(h->actions[0]);
		h->actions.clear();
		delete h;
	}
	else {
		// Push ComplexAction
		APP->history->push(h);
	}
}

bool RackWidget::isMultiPatching() {
	return internal->multiPatching;
}

bool RackWidget::canMultiPatchPort(PortWidget* pw) {
	if (!internal->multiPatching)
		return false;
	if (!pw || !pw->module)
		return false;
	// Cables can always be plugged into ports of their free end's type
	if (pw->type == internal->multiPatchFreeType)
		return true;
	// A collection of unplugged cables only grows by unplugging more ports of the same type, so it
	// is never a mix of unplugged inputs and unplugged outputs
	if (internal->multiPatchGrabMode)
		return false;
	// A collection of new cables grows by starting cables on ports of the opposite type
	return internal->multiPatchIndex == 0;
}

bool RackWidget::isMultiPatchCable(CableWidget* cw) {
	if (!internal->multiPatching || !cw)
		return false;
	for (const Internal::MultiPatchPort& p : internal->multiPatchPorts) {
		if (p.cable.get() == cw)
			return true;
	}
	return false;
}

void RackWidget::multiPatchPort(PortWidget* pw) {
	multiPatchPort(pw, MULTI_PATCH_GRAB);
}

void RackWidget::multiPatchCreateCable(PortWidget* pw) {
	multiPatchPort(pw, MULTI_PATCH_CREATE);
}

void RackWidget::multiPatchCloneCable(PortWidget* pw) {
	multiPatchPort(pw, MULTI_PATCH_CLONE);
}

void RackWidget::multiPatchPort(PortWidget* pw, MultiPatchMode mode) {
	if (!pw || !pw->module)
		return;

	engine::Port::Type otherType = (pw->type == engine::Port::INPUT) ? engine::Port::OUTPUT : engine::Port::INPUT;
	// Unplugging and duplicating both hand over the plug at this port, so the cable is patched into
	// a port of this type. A new cable hands over a plug for the opposite type instead.
	bool takesClickedEnd = (mode != MULTI_PATCH_CREATE) && getMultiPatchGrabCable(this, pw);
	engine::Port::Type freeType = takesClickedEnd ? pw->type : otherType;

	// Begin collecting cables
	if (!internal->multiPatching) {
		internal->multiPatchPorts.clear();
		internal->multiPatchIndex = 0;

		Internal::MultiPatchPort p;
		collectMultiPatchCable(this, p, pw, mode);
		internal->multiPatchPorts.push_back(p);

		// The first click sets the side the collection's free ends are on, so later clicks on ports
		// of the free type take more cables instead of patching
		internal->multiPatching = true;
		internal->multiPatchGrabMode = takesClickedEnd;
		internal->multiPatchFreeType = freeType;

		internal->multiPatchHistory = new history::ComplexAction;
		internal->multiPatchHistory->name = string::translate("RackWidget.history.multiPatch");
		return;
	}

	if (!canMultiPatchPort(pw))
		return;

	auto& ports = internal->multiPatchPorts;

	if (internal->multiPatchIndex == 0) {
		// Clicking a collected port again drops it from the collection
		auto it = std::find_if(ports.begin(), ports.end(), [&](const Internal::MultiPatchPort& p) {
			return p.port.get() == pw;
		});
		if (it != ports.end()) {
			releaseMultiPatchCable(this, *it);
			ports.erase(it);
			if (ports.empty())
				endMultiPatch();
			return;
		}

		// Only collect a cable whose free end matches the collection, so every collected cable is
		// patched into the same type of port
		bool collect;
		if (pw->type != internal->multiPatchFreeType) {
			// Only a new cable started here leaves its free end on the other type
			collect = true;
			mode = MULTI_PATCH_CREATE;
		}
		else {
			// Only taking this port's plug leaves the free end on this type
			collect = internal->multiPatchGrabMode && takesClickedEnd;
		}

		if (collect) {
			Internal::MultiPatchPort p;
			collectMultiPatchCable(this, p, pw, mode);
			ports.push_back(p);
			return;
		}
	}

	// Patching phase: skip collected cables that have been deleted
	while (internal->multiPatchIndex < ports.size() && !ports[internal->multiPatchIndex].cable.get()) {
		releaseMultiPatchCable(this, ports[internal->multiPatchIndex]);
		internal->multiPatchIndex++;
	}
	if (internal->multiPatchIndex >= ports.size()) {
		endMultiPatch();
		return;
	}

	Internal::MultiPatchPort& p = ports[internal->multiPatchIndex];
	CableWidget* cw = p.cable.get();
	engine::Port::Type anchorType = (internal->multiPatchFreeType == engine::Port::INPUT) ? engine::Port::OUTPUT : engine::Port::INPUT;
	PortWidget* anchorPw = cw->getPort(anchorType);
	PortWidget* outputPort = (internal->multiPatchFreeType == engine::Port::OUTPUT) ? pw : anchorPw;
	PortWidget* inputPort = (internal->multiPatchFreeType == engine::Port::OUTPUT) ? anchorPw : pw;
	internal->multiPatchIndex++;

	// Plugging a grabbed cable back where it came from, or where a similar cable already exists,
	// changes nothing, so just put the cable back
	if (!anchorPw || pw == p.grabbedPort.get() || getCable(outputPort, inputPort)) {
		releaseMultiPatchCable(this, p);
	}
	else {
		// Clear the hovered ports, which would otherwise dangle if the cable is grabbed again later
		cw->hoveredOutputPort = NULL;
		cw->hoveredInputPort = NULL;
		cw->getPort(internal->multiPatchFreeType) = pw;
		cw->updateCable();

		// A grabbed cable was unplugged from its old port before being plugged into this one
		if (p.grabHistory) {
			if (internal->multiPatchHistory)
				internal->multiPatchHistory->push(p.grabHistory);
			else
				delete p.grabHistory;
			p.grabHistory = NULL;
		}
		p.grabbedPort.set(NULL);
		p.cable.set(NULL);

		// history::CableAdd
		history::CableAdd* h = new history::CableAdd;
		h->setCable(cw);
		if (internal->multiPatchHistory)
			internal->multiPatchHistory->push(h);
		else
			delete h;
	}

	// Stop when every collected cable has been patched
	if (internal->multiPatchIndex >= ports.size())
		endMultiPatch();
}


void RackWidget::updateExpanders() {
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = (ModuleWidget*) w;

		math::Vec pLeft = mw->getGridBox().getTopLeft();
		math::Vec pRight = mw->getGridBox().getTopRight();
		ModuleWidget* mwLeft = NULL;
		ModuleWidget* mwRight = NULL;

		// Find adjacent modules
		for (widget::Widget* w2 : internal->moduleContainer->children) {
			ModuleWidget* mw2 = (ModuleWidget*) w2;
			if (mw2 == mw)
				continue;

			math::Vec p2Left = mw2->getGridBox().getTopLeft();
			math::Vec p2Right = mw2->getGridBox().getTopRight();

			// Check if this is a left module
			if (p2Right.equals(pLeft))
				mwLeft = mw2;

			// Check if this is a right module
			if (p2Left.equals(pRight))
				mwRight = mw2;
		}

		mw->module->leftExpander.moduleId = mwLeft ? mwLeft->module->id : -1;
		mw->module->rightExpander.moduleId = mwRight ? mwRight->module->id : -1;
	}
}


} // namespace app
} // namespace rack
