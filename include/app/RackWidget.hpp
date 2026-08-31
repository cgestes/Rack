#pragma once
#include <app/common.hpp>
#include <widget/OpaqueWidget.hpp>
#include <widget/FramebufferWidget.hpp>
#include <ui/Menu.hpp>
#include <app/RailWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/ParamWidget.hpp>
#include <history.hpp>

#include <set>


namespace rack {
namespace app {


/** Container for ModuleWidget and CableWidget. */
struct RackWidget : widget::OpaqueWidget {
	struct Internal;
	Internal* internal;

	/** DEPRECATED. Use get/setTouchedParam(). */
	ParamWidget* touchedParam = NULL;

	PRIVATE RackWidget();
	PRIVATE ~RackWidget();

	void step() override;
	void draw(const DrawArgs& args) override;

	void onHover(const HoverEvent& e) override;
	void onHoverKey(const HoverKeyEvent& e) override;
	void onButton(const ButtonEvent& e) override;
	void onDragStart(const DragStartEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void onDragHover(const DragHoverEvent& e) override;

	// Rack methods

	widget::Widget* getModuleContainer();
	widget::Widget* getPlugContainer();
	widget::Widget* getCableContainer();
	math::Vec getMousePos();

	/** Completely clear the rack's modules and cables */
	void clear();
	void mergeJson(json_t* rootJ);
	void fromJson(json_t* rootJ);
	/** Pastes module JSON or selection JSON at the mouse position. */
	void pasteJsonAction(json_t* rootJ);
	void pasteModuleJsonAction(json_t* moduleJ);
	void pasteClipboardAction();

	// Module methods

	/** Adds a module and adds it to the Engine, adopting ownership.
	*/
	void addModule(ModuleWidget* mw);
	void addModuleAtMouse(ModuleWidget* mw);
	/** Removes the module and transfers ownership to the caller.
	*/
	void removeModule(ModuleWidget* mw);
	ModuleWidget* getModule(int64_t moduleId);
	std::vector<ModuleWidget*> getModules();
	bool hasModules();

	// Module position methods

	/** Sets a module's box if non-colliding. Returns true if set */
	bool requestModulePos(ModuleWidget* mw, math::Vec pos);
	/** Moves a module to the closest non-colliding position */
	void setModulePosNearest(ModuleWidget* mw, math::Vec pos);
	/** Moves a module to a position, pushing other modules in the same row to the left or right, as needed. */
	void setModulePosForce(ModuleWidget* mw, math::Vec pos);
	/** Moves a module, contracting old module positions and pushing modules to the right as needed to fit. */
	void setModulePosSqueeze(ModuleWidget* mw, math::Vec pos);
	PRIVATE void squeezeModulePos(ModuleWidget* mw, math::Vec pos);
	PRIVATE void unsqueezeModulePos(ModuleWidget* mw);
	/** Saves positions of modules for getModuleDragAction(). */
	void updateModuleOldPositions();
	history::ComplexAction* getModuleDragAction();

	// Module selection methods

	void updateSelectionFromRect();
	void selectAll();
	void deselectAll();
	void select(ModuleWidget* mw, bool selected = true);
	bool hasSelection();
	const std::set<ModuleWidget*>& getSelected();
	bool isSelected(ModuleWidget* mw);
	json_t* selectionToJson(bool cables = true);
	void loadSelection(std::string path);
	void loadSelectionDialog();
	void saveSelection(std::string path);
	void saveSelectionDialog();
	void copyClipboardSelection();
	void resetSelectionAction();
	void randomizeSelectionAction();
	void disconnectSelectionAction();
	void cloneSelectionAction(bool cloneCables = true);
	void bypassSelectionAction(bool bypassed);
	bool isSelectionBypassed();
	void deleteSelectionAction();
	bool requestSelectionPos(math::Vec delta);
	void setSelectionPosNearest(math::Vec delta);
	void appendSelectionContextMenu(ui::Menu* menu);

	// Cable methods

	void clearCables();
	void clearCablesAction();
	/** Removes all cables connected to the port */
	void clearCablesOnPort(PortWidget* port);
	/** Adds a cable and adopts ownership.
	*/
	void addCable(CableWidget* cw);
	/** Removes cable and releases ownership to caller.
	*/
	void removeCable(CableWidget* cw);
	/** Returns the top incomplete cable. Use getIncompleteCables() instead. */
	DEPRECATED CableWidget* getIncompleteCable();
	/** Returns the topmost plug stacked on the port. */
	PlugWidget* getTopPlug(PortWidget* port);
	/** Returns the cable with the topmost plug stacked on the port. */
	CableWidget* getTopCable(PortWidget* port);
	CableWidget* getCable(int64_t cableId);
	CableWidget* getCable(PortWidget* outputPort, PortWidget* inputPort);
	/** Returns all cables, complete and incomplete. */
	std::vector<CableWidget*> getCables();
	/** Returns all cables attached to 2 ports. */
	std::vector<CableWidget*> getCompleteCables();
	/** Returns all cables attached to less than 2 ports. */
	std::vector<CableWidget*> getIncompleteCables();
	/** Returns all cables attached to the port, complete or not. */
	std::vector<CableWidget*> getCablesOnPort(PortWidget* port);
	/** Returns all complete cables attached to the port. */
	std::vector<CableWidget*> getCompleteCablesOnPort(PortWidget* port);
	/** Returns but does not advance the next cable color. */
	int getNextCableColorId();
	void setNextCableColorId(int id);
	/** Returns and advances the next cable color. */
	NVGcolor getNextCableColor();
	ParamWidget* getTouchedParam();
	void setTouchedParam(ParamWidget* pw);

	// Multi-patch methods

	/** Handles a click on a port while `settings::multiPatch` is enabled.
	Collects a cable whose free end follows the cursor, or patches the next collected cable into
	this port. A port with a cable is unplugged, so the cable is moved rather than copied; an empty
	port gets a new cable. Cables that are never patched are plugged back in where they came from.
	Collecting continues while a click yields a cable with the same free end as the collection, so
	unplugged cables are patched into ports of the clicked port's type, and new cables into ports of
	the opposite type. The first click that can't collect starts patching.
	*/
	void multiPatchPort(PortWidget* pw);
	/** Like multiPatchPort(), but always collects a new cable, leaving the cables already on the
	port plugged in. The click equivalent of Ctrl+drag.
	*/
	void multiPatchCreateCable(PortWidget* pw);
	/** Like multiPatchPort(), but duplicates the port's top cable instead of unplugging it: the
	duplicate keeps the cable's other end and color, and its plug for this port follows the cursor.
	The click equivalent of Ctrl+shift+drag.
	*/
	void multiPatchCloneCable(PortWidget* pw);
	/** How a multi-patch click takes a cable from a port. */
	enum MultiPatchMode {
		/** Unplug the port's top cable, or start a new cable if the port has none. */
		MULTI_PATCH_GRAB,
		/** Always start a new cable on the port. */
		MULTI_PATCH_CREATE,
		/** Duplicate the port's top cable, or start a new cable if the port has none. */
		MULTI_PATCH_CLONE,
	};
	PRIVATE void multiPatchPort(PortWidget* pw, MultiPatchMode mode);
	/** Stops multi-patching and pushes the created cables to the history. */
	void endMultiPatch();
	bool isMultiPatching();
	/** Returns whether clicking the port would collect it or patch a cable to it. */
	bool canMultiPatchPort(PortWidget* pw);
	/** Returns whether the cable is a collected multi-patch cable following the cursor.
	These cables are not returned by getIncompleteCables() or getTopPlug(), so they are never grabbed
	or completed by the usual cable dragging.
	*/
	bool isMultiPatchCable(CableWidget* cw);

	PRIVATE void updateExpanders();
};


} // namespace app
} // namespace rack
