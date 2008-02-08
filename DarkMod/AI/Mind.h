/***************************************************************************
 *
 * PROJECT: The Dark Mod
 * $Revision: 1435 $
 * $Date: 2007-10-16 18:53:28 +0200 (Di, 16 Okt 2007) $
 * $Author: greebo $
 *
 ***************************************************************************/

#ifndef __AI_MIND_H__
#define __AI_MIND_H__

#include "Memory.h"
#include "States/State.h"
#include "Queue.h"

namespace ai
{

class Mind
{
private:
	// The reference to the owning entity
	idEntityPtr<idAI> _owner;

	StateQueue _stateQueue;

	// The structure holding all the variables
	Memory _memory;

	// This holds the id of the subsystem whose turn it is next frame
	SubsystemId _subsystemIterator;

	// TRUE if the the State is about to be switched the next frame
	bool _switchState;

	// A temporary variable to hold dying states
	// This is needed to avoid immediate destruction of states upon switching
	StatePtr _recycleBin;

public:
	Mind(idAI* owner);
	virtual ~Mind() {}

	/**
	 * greebo: This should be called each frame to let the AI
	 *         think. This distributes the call to the various
	 *         subsystem's Think() methods, maybe in and interleaved way.
	 */
	virtual void Think();

	/**
	 * greebo: Switches the Mind to the named State. The currently active
	 *         State will be pushed back in the queue and is postponed 
	 *         until the new State is finished.
	 */
	virtual void PushState(const idStr& stateName);
	virtual void PushState(const StatePtr& state);

	/**
	 * greebo: Ends the current state - the Mind will pick the next State
	 *         from the StateQueue or create a new Idle State if none is available.
	 *
	 * @returns: TRUE if there are still States in the queue after this one is finished.
	 */
	virtual bool EndState();

	/**
	 * greebo: Sets the new state of this mind (this can be Idle, Combat).
	 * This new state REPLACES the currently active one.
	 */
	virtual void SwitchState(const idStr& stateName);
	virtual void SwitchState(const StatePtr& state);

	/**
	 * greebo: Removes all States from the Queue and falls back to the default State (Idle).
	 */
	virtual void ClearStates();

	/**
	 * Returns the reference to the current state (can be NULL).
	 */
	ID_INLINE const StatePtr& GetState() const {
		assert(_stateQueue.size() > 0);
		return _stateQueue.front();
	}

	// Returns the Memory structure, which holds the various mind variables
	ID_INLINE Memory& GetMemory() {
		return _memory;
	}

	/* greebo: Copied over the comments from the script function
	=====================
	ai_darkmod_base::setAlertPos( void )

	SophisticatedZombie Note: Note that this method and setTarget are mutually exclusive
	as they clear flags that the other needs

	Set the location in space to investigate due to an alert.

	TODO: Add some randomness for sounds.  AI shouldn't know exactly where it came from.

	It's hard to add randomness without risking going outside the map though, and that would
	be bad because the AI would just stand in place and not move at all if its destination is unreachable.

	I guess we could keep picking random points until we find one inside the map.  This part of it
	won't be run every frame, only when it's getting the target for where to investigate, so that
	might be an okay algorithm.  (random vector offsets, throwing out ones that go to an unreachable area)
	=====================
	*/
	virtual void SetAlertPos();

	/** 
	* SophisticatedZombie: This method performs a visual scan and 
	* responds to any alerts generated by it or other things up 
	* until this point for this frame.
	*
	* If the alert level is driven to the combat threshold
	* then combat will be attempted
	*
	* Otherwise, either nothing will be done or the stimuli
	* source will be searched for, depending on the value
	* of b_searchNewStimuli
	*/
	virtual void PerformSensoryScan(bool processNewStimuli);

	/**
	* SZ: This method tests to see if the target can be reached.
	* If it can't, then the AI chooses another action.
	*
	* This method often switches the AI to a new state.
	*
	* @returns: TRUE if combat mode was entered, FALSE otherwise
	*/
	virtual bool PerformCombatCheck();
	/**
	* SZ: setTarget should only be called when the combat threshold has been
	* reached and the AI needs to try to attack something.
	* The only alert that does not set a target is sound.
	* In the case of sound, the AI will keep running at the source of the noise
	* and executing the running search until it sees or bumps into the player
	*
	* @return true if a target was found
	* @return false if no target found
	*
	* SophisticatedZombie Note: Note that this method and setTarget are mutually exclusive
	* as they clear flags that the other needs. 
	*/
	virtual bool SetTarget();

	// Save/Restore routines
	virtual void Save(idSaveGame* savefile) const;
	virtual void Restore(idRestoreGame* savefile);
};
typedef boost::shared_ptr<Mind> MindPtr;

} // namespace ai

#endif /* __AI_MIND_H__ */
