//============================================================================
// Name        : NovadControl.cpp
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
// Description : Controls the Novad process itself, in terms of stopping and starting
//============================================================================

#include "messaging/MessageManager.h"
#include "Commands.h"
#include "Logger.h"
#include "Lock.h"

#include <iostream>
#include <stdio.h>
#include <unistd.h>

using namespace Nova;
using namespace std;


pthread_mutex_t messageQueueMutex;
queue<Message*> messageQueue;
pthread_cond_t popWakeupCondition;

namespace Nova
{

bool StartNovad(bool blocking)
{
	if(IsNovadUp())
	{
		return true;
	}

	if (!blocking)
	{
		if(system("nohup novad > /dev/null&") != 0)
		{
			return false;
		}
		else
		{
			return true;
		}
	}
	else
	{
		if(system("novad") != 0)
		{
			return false;
		}
		else
		{
			return true;
		}

	}
}

void StopNovad()
{
	Message killRequest;
	killRequest.m_contents.set_m_type(CONTROL_EXIT_REQUEST);
	MessageManager::Instance().WriteMessage(&killRequest, killRequest.m_contents.m_sessionindex());

	DisconnectFromNovad();
}

bool HardStopNovad()
{
	// THIS METHOD SHOULD ONLY BE CALLED ON DEADLOCK FOR NOVAD
	if(system(string("killall novad").c_str()) != -1)
	{
		LOG(INFO, "Nova has experienced a hard stop", "");
		return true;
	}
	LOG(ERROR, "Something happened while trying to kill Novad", "");
	return false;
}

void SaveAllSuspects(std::string file)
{
	Message saveRequest;
	saveRequest.m_contents.set_m_type(CONTROL_SAVE_SUSPECTS_REQUEST);
	saveRequest.m_contents.set_m_filepath(file.c_str());

	MessageManager::Instance().WriteMessage(&saveRequest, saveRequest.m_contents.m_sessionindex());
}

void ClearAllSuspects()
{
	Message clearRequest;
	clearRequest.m_contents.set_m_type(CONTROL_CLEAR_ALL_REQUEST);
	MessageManager::Instance().WriteMessage(&clearRequest, clearRequest.m_contents.m_sessionindex());
}

void ClearSuspect(SuspectID_pb suspectId)
{
	Message clearRequest;
	clearRequest.m_contents.set_m_type(CONTROL_CLEAR_SUSPECT_REQUEST);

	*clearRequest.m_contents.mutable_m_suspectid() = suspectId;
	MessageManager::Instance().WriteMessage(&clearRequest, clearRequest.m_contents.m_sessionindex());
}

void ReclassifyAllSuspects()
{
	Message request;
	request.m_contents.set_m_type(CONTROL_RECLASSIFY_ALL_REQUEST);
	MessageManager::Instance().WriteMessage(&request, request.m_contents.m_sessionindex());
}

void StartPacketCapture()
{
	Message request;
	request.m_contents.set_m_type(CONTROL_START_CAPTURE);
	MessageManager::Instance().WriteMessage(&request, request.m_contents.m_sessionindex());
}

void StopPacketCapture()
{
	Message request;
	request.m_contents.set_m_type(CONTROL_STOP_CAPTURE);
	MessageManager::Instance().WriteMessage(&request, request.m_contents.m_sessionindex());
}

//bool WaitForMessage(enum MessageType type, uint msMaxWait)
//{
//	Lock lock(&m_queueMutex);
//
//	while(m_queue.empty())
//	{
//		pthread_cond_wait(&m_popWakeupCondition, &m_queueMutex);
//	}
//}

void *ClientMessageWorker(void *arg)
{
	pthread_mutex_init(&messageQueueMutex, NULL);
	pthread_cond_init(&popWakeupCondition, NULL);
	while(true)
	{
		Message *message = MessageManager::Instance().DequeueMessage();

		//Handle the message in the context of the UI_Core
		switch(message->m_contents.m_type())
		{
			case UPDATE_SUSPECT:
			{
				break;
			}
			case UPDATE_ALL_SUSPECTS_CLEARED:
			{
				break;
			}
			case UPDATE_SUSPECT_CLEARED:
			{
				break;
			}
			case REQUEST_PONG:
			{
				break;
			}
			default:
			{
				break;
			}
		}

		//Hand the message off to the UI for further handling
		Lock lock(&messageQueueMutex);
		messageQueue.push(message);
		pthread_cond_signal(&popWakeupCondition);
	}
	return NULL;
}

Message *DequeueUIMessage()
{
	Lock lock(&messageQueueMutex);

	while(messageQueue.empty())
	{
		pthread_cond_wait(&popWakeupCondition, &messageQueueMutex);
	}

	Message *ret = messageQueue.front();
	messageQueue.pop();
	return ret;
}

}
