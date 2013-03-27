//============================================================================
// Name        : nova_ui_core.h
// Copyright   : DataSoft Corporation 2011-2013
//	Nova is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   Nova is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with Nova.  If not, see <http://www.gnu.org/licenses/>.
// Description : Set of command API functions available for use as a UI implementation
//============================================================================

#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <vector>
#include "string"

#include "HaystackControl.h"
#include "Suspect.h"
#include "messaging/Message.h"

namespace Nova
{
//************************************************************************
//**						Status Queries								**
//************************************************************************

//Runs the Novad process
//	returns - True upon successfully running the novad process, false on error
//	NOTE: This function will return true if Novad was already running
bool StartNovad(bool blocking = false);

//Asks the novad process to exit nicely
void StopNovad();

//Kills the Novad process in event of a deadlock
//  returns - True upon killing of Novad process, false on error
bool HardStopNovad();

//Queries Novad to see if it is currently up or down
//	the result is eventually stored such that IsNovadUp() can retrieve it
void Ping();


bool IsNovadUp();

//************************************************************************
//**						Connection Operations						**
//************************************************************************

//Initializes a connection out to Novad over IPC
//	NOTE: Must be called before any message can be sent to Novad (but after InitCallbackSocket())
//	returns - true if a successful connection is established, false if no connection (error)
//	NOTE: If a connection already exists, then the function does nothing and returns true
bool ConnectToNovad();

//Disconnects from Novad over IPC. (opposite of ConnectToNovad) Sends no messages
//	NOTE: Cannot be called in the same scope as a Ticket! Disconnecting from a socket
//		requires that we write lock it, while a Ticket has a read lock. Trying to do
//		both will cause a deadlock.
//	NOTE: Safely does nothing if already disconnected
// returns - (Vacuously returns true for the sake of nodejs wrapping)
bool DisconnectFromNovad();


//************************************************************************
//**						Suspect Operations							**
//************************************************************************

// Requests a list of suspect addresses currently classified
//	 listType: Type of list to get (all, just hostile, just benign)
void RequestSuspectList(enum SuspectListType listType);

// Gets a suspect from the daemon
// address: IP address of the suspect
void RequestSuspect(SuspectID_pb address);

// Same as GetSuspect but returns all the featureset data
// address: IP address of the suspect
void RequestSuspectWithData(SuspectID_pb address);

//Request multiple suspects, filtered by listType (all, just hostile, just benign)
void RequestSuspects(enum SuspectListType listType);

//Asks Novad to save the suspect list to persistent storage
//	returns - true if saved correctly, false on error
void SaveAllSuspects(std::string file);

//Asks Novad to forget all data on all suspects that it has
void ClearAllSuspects();

//Asks Novad to forget data on the specified suspect
//	suspectAddress - The IP address (unique identifier) of the suspect to forget
void ClearSuspect(SuspectID_pb suspectAddress);

//Asks Novad to reclassify all suspects
void ReclassifyAllSuspects();

// Asks novad for it's uptime.
void RequestStartTime();

// Command nova to start or stop live packet capture
void StartPacketCapture();
void StopPacketCapture();


//************************************************************************
//**						Event Operations							**
//************************************************************************

//In case you really really want to block until a particular message was handled,
//	this function will do just that for you
//	returns true if message was received and handled, false if timeout
bool WaitForMessage(enum MessageType type, uint msMaxWait);

//Grabs a message off of the message queue
// Returns - A pointer to a valid Message object. Never NULL. Caller is responsible for life cycle of this message
// NOTE: Blocking call. To be called from worker threads
Message *DequeueUIMessage();

void *ClientMessageWorker(void *arg);

}

#endif /* COMMANDS_H_ */
