//============================================================================
// Name        : UI_Message.cpp
// Copyright   : DataSoft Corporation 2011-2012
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
// Description : Parent message class for GUI communication with Nova processes
//============================================================================/*

#include "UI_Message.h"
#include "ControlMessage.h"
#include "CallbackMessage.h"
#include "RequestMessage.h"
#include "ErrorMessage.h"

#include <string>
#include <vector>
#include <sys/socket.h>
#include <errno.h>


using namespace std;
using namespace Nova;

UI_Message::UI_Message()
{

}

UI_Message::~UI_Message()
{

}

UI_Message *UI_Message::ReadMessage(int connectFD, int timeout)
{
	uint32_t length = 0;
	char buff[sizeof(length)];
	uint totalBytesRead = 0;
	int bytesRead = 0;

	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	setsockopt(connectFD, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

	// Read in the message length
	while( totalBytesRead < sizeof(length))
	{
		bytesRead = read(connectFD, buff + totalBytesRead, sizeof(length) - totalBytesRead);

		if( bytesRead < 0 )
		{
			// Was this a timeout error?
			if (errno == EWOULDBLOCK || errno == EAGAIN)
			{
				close(connectFD);
				return new ErrorMessage(ERROR_TIMEOUT);
			}
			else
			{
				close(connectFD);

				//The socket died on us!
				return new ErrorMessage(ERROR_SOCKET_CLOSED);
			}
		}
		else
		{
			totalBytesRead += bytesRead;
		}
	}

	// Make sure the length appears valid
	// TODO: Assign some arbitrary max message size to avoid filling up memory by accident
	memcpy(&length, buff, sizeof(length));
	if (length == 0)
	{
		close(connectFD);
		return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
	}

	char *buffer = (char*)malloc(length);

	if (buffer == NULL)
	{
		// This should never happen. If it does, probably because length is an absurd value (or we're out of memory)
		close(connectFD);
		return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
	}

	// Read in the actual message
	totalBytesRead = 0;
	bytesRead = 0;
	while(totalBytesRead < length)
	{
		bytesRead = read(connectFD, buffer + totalBytesRead, length - totalBytesRead);

		if( bytesRead < 0 )
		{
			// Was this a timeout error?
			if (errno == EWOULDBLOCK || errno == EAGAIN)
			{
				close(connectFD);
				return new ErrorMessage(ERROR_TIMEOUT);
			}
			else
			{
				close(connectFD);

				//The socket died on us!
				return new ErrorMessage(ERROR_SOCKET_CLOSED);
			}
		}
		else
		{
			totalBytesRead += bytesRead;
		}
	}


	if(length < MESSAGE_MIN_SIZE)
	{
		close(connectFD);
		return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
	}

	return UI_Message::Deserialize(buffer, length);
}

bool UI_Message::WriteMessage(UI_Message *message, int connectFD)
{
	if (connectFD == -1)
	{
		return false;
	}

	uint32_t length;
	char *buffer = message->Serialize(&length);

	// Send the message length
	if (write(connectFD, &length, sizeof(length)) < 0)
	{
		free(buffer);
		return false;
	}

	// Send the message
	if( write(connectFD, buffer, length) < 0 )
	{
		free(buffer);
		return false;
	}

	free(buffer);
	return true;
}

UI_Message *UI_Message::Deserialize(char *buffer, uint32_t length)
{
	if(length < MESSAGE_MIN_SIZE)
	{
		return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
	}

	enum UI_MessageType thisType;
	memcpy(&thisType, buffer, MESSAGE_MIN_SIZE);

	switch(thisType)
	{
		case CONTROL_MESSAGE:
		{
			ControlMessage *message = new ControlMessage(buffer, length);
			if(message->m_serializeError)
			{
				delete message;
				return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
			}
			return message;
		}
		case CALLBACK_MESSAGE:
		{
			CallbackMessage *message = new CallbackMessage(buffer, length);
			if(message->m_serializeError)
			{
				delete message;
				return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
			}
			return message;
		}
		case REQUEST_MESSAGE:
		{
			RequestMessage *message = new RequestMessage(buffer, length);
			if(message->m_serializeError)
			{
				delete message;
				return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
			}
			return message;
		}
		case ERROR_MESSAGE:
		{
			ErrorMessage *message = new ErrorMessage(buffer, length);
			if(message->m_serializeError)
			{
				delete message;
				return new ErrorMessage(ERROR_MALFORMED_MESSAGE);
			}
			return message;
		}
		default:
		{
			return new ErrorMessage(ERROR_UNKNOWN_MESSAGE_TYPE);
		}
	}
}

char *UI_Message::Serialize(uint32_t *length)
{
	//Doesn't actually get called
	return NULL;
}
