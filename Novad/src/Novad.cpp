//============================================================================
// Name        : Novad.cpp
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
// Description : Nova Daemon to perform network anti-reconnaissance
//============================================================================

#include "NOVAConfiguration.h"
#include "SuspectTable.h"
#include "NovaUtil.h"
#include "Logger.h"
#include "Point.h"
#include "Novad.h"
#include "Control.h"

#include <vector>
#include <math.h>
#include <string>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <sys/un.h>
#include <signal.h>
#include <sys/inotify.h>
#include <netinet/if_ether.h>
#include <iostream>

#include <boost/format.hpp>

using namespace std;
using boost::format;

string userHomePath, novaConfigPath;
NOVAConfiguration *globalConfig;
Logger *logger;

//Used in classification algorithm. Store it here so we only need to calculate it once
double sqrtDIM;

// Maintains a list of suspects and information on network activity
SuspectHashTable suspects;
SuspectHashTable suspectsSinceLastSave;
static TCPSessionHashTable SessionTable;

//NOT normalized
vector <Point*> dataPtsWithClass;
static struct sockaddr_in hostAddr;

//** Main (ReceiveSuspectData) **
struct sockaddr_un remote;
struct sockaddr* remoteSockAddr = (struct sockaddr *)&remote;

int IPCsock;

//** Silent Alarm **
struct sockaddr_un alarmRemote;
struct sockaddr* alarmRemotePtr =(struct sockaddr *)&alarmRemote;
struct sockaddr_in serv_addr;
struct sockaddr* serv_addrPtr = (struct sockaddr *)&serv_addr;

int oldClassification;
int sockfd = 1;

//** ReceiveGUICommand **
int GUISocket;

//** SendToUI **
struct sockaddr_un GUISendRemote;
struct sockaddr* GUISendPtr = (struct sockaddr *)&GUISendRemote;
int GUISendSocket;
int GUILen;

//Universal Socket variables (constants that can be re-used)
int socketSize = sizeof(remote);
int inSocketSize = sizeof(serv_addr);
socklen_t * sockSizePtr = (socklen_t*)&socketSize;

// kdtree stuff
int nPts = 0;						//actual number of data points
ANNpointArray dataPts;				//data points
ANNpointArray normalizedDataPts;	//normalized data points
ANNkd_tree*	kdTree;					// search structure

//Used to indicate if the kd tree needs to be reformed
bool updateKDTree = false;

// Used for disabling features
bool featureEnabled[DIM];
uint32_t featureMask;
int enabledFeatures;

// Misc
int len;
const char *outFile;				//output for data points during trainingthos

// Used for data normalization
double maxFeatureValues[DIM];
double minFeatureValues[DIM];
double meanFeatureValues[DIM];

// Nova Configuration Variables (read from config file)
string trainingCapFile;
string SMTP_addr;
string SMTP_domain;
in_port_t SMTP_port;
Nova::userMap service_pref;
vector<string> email_recipients;

// Normalization method to use on each feature
// TODO: Make this a configuration var somewhere in Novaconfig.txt?
normalizationType normalization[] = {
		LINEAR_SHIFT, // Don't normalize IP traffic distribution, already between 0 and 1
		LINEAR_SHIFT,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC
};

// End configured variables

time_t lastLoadTime;
time_t lastSaveTime;

bool enableGUI = true;

//HS Vars
string dhcpListFile = "/var/log/honeyd/ipList";
vector <string> haystackAddresses;
vector <string> haystackDhcpAddresses;
pcap_t *handle;
bpf_u_int32 maskp;				/* subnet mask */
bpf_u_int32 netp; 				/* ip          */

int notifyFd;
int watch;
bool usePcapFile;

pthread_t TCP_timeout_thread;
pthread_rwlock_t sessionLock;
pthread_rwlock_t suspectTableLock;

int main()
{
	//TODO: Perhaps move this into its own init function?
	userHomePath = GetHomePath();
	novaConfigPath = userHomePath + "/Config/NOVAConfig.txt";
	logger = new Logger(novaConfigPath.c_str(), true);

	if(chdir(userHomePath.c_str()) == -1)
		logger->Log(INFO, "Failed to change directory to " + userHomePath, "Failed to change directory to " + userHomePath);

	globalConfig = new NOVAConfiguration();
	globalConfig->LoadConfig();

	pthread_rwlock_init(&suspectTableLock, NULL);
	pthread_rwlock_init(&sessionLock, NULL);

	signal(SIGINT, SaveAndExit);

	lastLoadTime = time(NULL);
	if (lastLoadTime == ((time_t)-1))
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to get system time with time()")%__LINE__%__FILE__).str());

	lastSaveTime = time(NULL);
	if (lastSaveTime == ((time_t)-1))
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to get system time with time()")%__LINE__%__FILE__).str());

	// XXX 'suspects' SuspectTable init todo
	suspects.set_empty_key(0);
	suspects.resize(INIT_SIZE_SMALL);
	SessionTable.set_empty_key("");
	SessionTable.resize(INIT_SIZE_HUGE);

	// XXX 'suspectsSinceLastSave' SuspectTable init todo
	suspectsSinceLastSave.set_empty_key(0);
	suspectsSinceLastSave.resize(INIT_SIZE_SMALL);

	pthread_t classificationLoopThread;
	pthread_t trainingLoopThread;
	pthread_t silentAlarmListenThread;
	pthread_t ipUpdateThread;

	Reload();

	//Are we Training or Classifying?
	if(globalConfig->getIsTraining())
	{
		// We suffix the training capture files with the date/time
		time_t rawtime;
		time ( &rawtime );
		struct tm * timeinfo = localtime(&rawtime);

		char buffer [40];
		strftime (buffer,40,"%m-%d-%y_%H-%M-%S",timeinfo);
		trainingCapFile = userHomePath + "/" + globalConfig->getPathTrainingCapFolder() + "/training" + buffer + ".dump";

		enabledFeatures = DIM;
		pthread_create(&trainingLoopThread,NULL,TrainingLoop,(void *)outFile);
	}
	else
	{
		pthread_create(&classificationLoopThread,NULL,ClassificationLoop, NULL);
		pthread_create(&silentAlarmListenThread,NULL,SilentAlarmLoop, NULL);
	}

	notifyFd = inotify_init ();

	if (notifyFd > 0)
	{
		watch = inotify_add_watch (notifyFd, dhcpListFile.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_DELETE);
		pthread_create(&ipUpdateThread, NULL, UpdateIPFilter,NULL);
	}
	else
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to set up file watcher for the honeyd IP list file. DHCP addresse in honeyd will not be read")
				%__LINE__%__FILE__).str());
	}

	Start_Packet_Handler();

	//Shouldn't get here!
	logger->Log(CRITICAL, (format("File %1% at line %2%: Main thread ended. This should never happen, something went very wrong.")%__LINE__%__FILE__).str());
	close(IPCsock);

	return EXIT_FAILURE;
}

void Nova::AppendToStateFile()
{
	lastSaveTime = time(NULL);
	if (lastSaveTime == ((time_t)-1))
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to get timestamp, call to time() failed")%__LINE__%__FILE__).str());

	// Don't bother saving if no new suspect data, just confuses deserialization
	// XXX 'suspectsSinceLastSave.Size()' SuspectTable todo
	if (suspectsSinceLastSave.size() <= 0)
		return;

	u_char tableBuffer[MAX_MSG_SIZE];
	uint32_t dataSize = 0;
	uint32_t index = 0;

	// Compute the total dataSize
	// XXX 'suspectsSinceLastSave' SuspectTable && iterator todo
	for (SuspectHashTable::iterator it = suspectsSinceLastSave.begin(); it != suspectsSinceLastSave.end(); it++)
	{
		if (!it->second->m_features.m_packetCount)
			continue;

		index = it->second->SerializeSuspect(tableBuffer);
		index += it->second->m_features.SerializeFeatureData(tableBuffer + index);

		dataSize += index;
	}

	// No suspects with packets to update
	if (dataSize == 0)
		return;

	ofstream out(globalConfig->getPathCESaveFile().data(), ofstream::binary | ofstream::app);
	if(!out.is_open())
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the CE state file %3%")%__LINE__%__FILE__%globalConfig->getPathCESaveFile()).str());
		return;
	}

	out.write((char*)&lastSaveTime, sizeof lastSaveTime);
	out.write((char*)&dataSize, sizeof dataSize);

	logger->Log(DEBUG, (format("File %1% at line %2%: Appending %3% bytes to the CE state file")%__LINE__%__FILE__%dataSize).str());

	// Serialize our suspect table
	// XXX 'suspectsSinceLastSave' SuspectTable && iterator todo
	for (SuspectHashTable::iterator it = suspectsSinceLastSave.begin(); it != suspectsSinceLastSave.end(); it++)
	{
		if (!it->second->m_features.m_packetCount)
			continue;

		index = it->second->SerializeSuspect(tableBuffer);
		index += it->second->m_features.SerializeFeatureData(tableBuffer + index);

		out.write((char*) tableBuffer, index);
	}

	out.close();

	// Clear out the unsaved data table (they're all saved now)
	// XXX 'suspectsSinceLastSave' SuspectTable && iterator todo
	for (SuspectHashTable::iterator it = suspectsSinceLastSave.begin(); it != suspectsSinceLastSave.end(); it++)
		delete it->second;
	suspectsSinceLastSave.clear();
}

void Nova::LoadStateFile()
{
	time_t timeStamp;
	uint32_t dataSize;

	lastLoadTime = time(NULL);
	if (lastLoadTime == ((time_t)-1))
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to get timestamp, call to time() failed")%__LINE__%__FILE__).str());

	// Open input file
	ifstream in(globalConfig->getPathCESaveFile().data(), ios::binary | ios::in);
	if(!in.is_open())
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open CE state file. This is normal for the first run.")%__LINE__%__FILE__).str());
		return;
	}

	// get length of input for error checking of partially written files
	in.seekg (0, ios::end);
	uint lengthLeft = in.tellg();
	in.seekg (0, ios::beg);

	while (in.is_open() && !in.eof() && lengthLeft)
	{
		// Bytes left, but not enough to make a header (timestamp + size)?
		if (lengthLeft < (sizeof timeStamp + sizeof dataSize))
		{
			logger->Log(ERROR, "The CE state file may be corrupt",
					(format("File %1% at line %2%: CE state file should have another entry, but only contains %3% more bytes")%__LINE__%__FILE__%lengthLeft).str());
			break;
		}

		in.read((char*) &timeStamp, sizeof timeStamp);
		lengthLeft -= sizeof timeStamp;

		in.read((char*) &dataSize, sizeof dataSize);
		lengthLeft -= sizeof dataSize;

		if (globalConfig->getDataTTL() && (timeStamp < lastLoadTime - globalConfig->getDataTTL()))
		{
			logger->Log(DEBUG, (format("File %1% at line %2%: Throwing out old CE state with timestamp of %3%")%__LINE__%__FILE__%(int)timeStamp).str());
			in.seekg(dataSize, ifstream::cur);
			lengthLeft -= dataSize;
			continue;
		}

		// Not as many bytes left as the size of the entry?
		if (lengthLeft < dataSize)
		{
			logger->Log(ERROR, "The CE state file may be corruput, unable to read all data from it",
					(format("File %1% at line %2%: CE state file should have another entry of size %3% but only has %4% bytes left")
							%__LINE__%__FILE__%dataSize%lengthLeft).str());
			break;
		}

		u_char tableBuffer[dataSize];
		in.read((char*) tableBuffer, dataSize);
		lengthLeft -= dataSize;

		// Read each suspect
		uint32_t bytesSoFar = 0;
		while (bytesSoFar < dataSize)
		{
			Suspect* newSuspect = new Suspect();
			uint32_t suspectBytes = 0;
			suspectBytes += newSuspect->DeserializeSuspect(tableBuffer + bytesSoFar + suspectBytes);
			suspectBytes += newSuspect->m_features.DeserializeFeatureData(tableBuffer + bytesSoFar + suspectBytes);
			bytesSoFar += suspectBytes;

			if(suspects.find(newSuspect->GetIpAddress()) == suspects.end())
			{
				suspects[newSuspect->GetIpAddress()] = newSuspect;
				suspects[newSuspect->GetIpAddress()]->SetNeedsFeatureUpdate(true);
				suspects[newSuspect->GetIpAddress()]->SetNeedsClassificationUpdate(true);
			}
			else
			{
				suspects[newSuspect->GetIpAddress()]->m_features += newSuspect->m_features;
				suspects[newSuspect->GetIpAddress()]->SetNeedsFeatureUpdate(true);
				suspects[newSuspect->GetIpAddress()]->SetNeedsClassificationUpdate(true);
				delete newSuspect;
			}
		}
	}

	in.close();
}

void Nova::RefreshStateFile()
{
	time_t timeStamp;
	uint32_t dataSize;
	vector<in_addr_t> deletedKeys;

	lastLoadTime = time(NULL);
	if (lastLoadTime == ((time_t)-1))
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to get timestamp, call to time() failed")%__LINE__%__FILE__).str());

	// Open input file
	ifstream in(globalConfig->getPathCESaveFile().data(), ios::binary | ios::in);
	if(!in.is_open())
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the CE state file at %3%")%__LINE__%__FILE__%globalConfig->getPathCESaveFile()).str());
		return;
	}

	// Open the tmp file
	string tmpFile = globalConfig->getPathCESaveFile() + ".tmp";
	ofstream out(tmpFile.data(), ios::binary);
	if(!out.is_open())
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the temporary CE state file at %3%")%__LINE__%__FILE__%tmpFile).str());
		in.close();
		return;
	}

	// get length of input for error checking of partially written files
	in.seekg (0, ios::end);
	uint lengthLeft = in.tellg();
	in.seekg (0, ios::beg);

	while (in.is_open() && !in.eof() && lengthLeft)
	{
		// Bytes left, but not enough to make a header (timestamp + size)?
		if (lengthLeft < (sizeof timeStamp + sizeof dataSize))
		{
			logger->Log(ERROR, "The CE state file may be corrupt", (format("File %1% at line %2%: CE state file should have another entry, but only contains %3% more bytes")
					%__LINE__%__FILE__%lengthLeft).str());
			break;
		}

		in.read((char*) &timeStamp, sizeof timeStamp);
		lengthLeft -= sizeof timeStamp;

		in.read((char*) &dataSize, sizeof dataSize);
		lengthLeft -= sizeof dataSize;

		if (globalConfig->getDataTTL() && (timeStamp < lastLoadTime - globalConfig->getDataTTL()))
		{
			logger->Log(DEBUG, (format("File %1% at line %2%: Throwing out old CE state with timestamp of %3%")%__LINE__%__FILE__%(int)timeStamp).str());
			in.seekg(dataSize, ifstream::cur);
			lengthLeft -= dataSize;
			continue;
		}

		// Not as many bytes left as the size of the entry?
		if (lengthLeft < dataSize)
		{
			logger->Log(ERROR, "The CE state file may be corrupt",
					(format("File %1% at line %2%: Data file should have another entry of size %3%, but contains only %4% bytes left")%__LINE__%__FILE__%dataSize%lengthLeft).str());
			break;
		}

		u_char tableBuffer[dataSize];
		in.read((char*) tableBuffer, dataSize);
		lengthLeft -= dataSize;

		// Read each suspect
		uint32_t bytesSoFar = 0;
		while (bytesSoFar < dataSize)
		{
			Suspect* newSuspect = new Suspect();
			uint32_t suspectBytes = 0;
			suspectBytes += newSuspect->DeserializeSuspect(tableBuffer + bytesSoFar + suspectBytes);
			suspectBytes += newSuspect->m_features.DeserializeFeatureData(tableBuffer + bytesSoFar + suspectBytes);

			// Did a suspect get cleared? Not in suspects anymore, but still is suspectsSinceLastSave
			// XXX 'suspectsSinceLastSave' SuspectTableIterator todo
			SuspectHashTable::iterator saveIter = suspectsSinceLastSave.find(newSuspect->GetIpAddress());
			// XXX 'suspectsSinceLastSave' SuspectTableIterator todo
			SuspectHashTable::iterator normalIter = suspects.find(newSuspect->GetIpAddress());
			if (normalIter == suspects.end() && saveIter != suspectsSinceLastSave.end())
			{
				cout << "Deleting suspect" << endl;
				in_addr_t key = newSuspect->GetIpAddress();
				deletedKeys.push_back(key);
				Suspect *currentSuspect = suspectsSinceLastSave[key];
				suspectsSinceLastSave.set_deleted_key(5);
				suspectsSinceLastSave.erase(key);
				suspectsSinceLastSave.clear_deleted_key();

				delete currentSuspect;
			}

			vector<in_addr_t>::iterator vIter = find (deletedKeys.begin(), deletedKeys.end(), newSuspect->GetIpAddress());
			if (vIter != deletedKeys.end())
			{
				// Shift the rest of the data over on top of our bad suspect
				memmove(tableBuffer+bytesSoFar,
						tableBuffer+bytesSoFar+suspectBytes,
						(dataSize-bytesSoFar-suspectBytes)*sizeof(tableBuffer[0]));
				dataSize -= suspectBytes;
			}
			else
			{
				bytesSoFar += suspectBytes;
			}

			delete newSuspect;
		}

		// If the entry is valid still, write it to the tmp file
		if (dataSize > 0)
		{
			out.write((char*) &timeStamp, sizeof timeStamp);
			out.write((char*) &dataSize, sizeof dataSize);
			out.write((char*) tableBuffer, dataSize);
		}
	}

	out.close();
	in.close();

	string copyCommand = "cp -f " + tmpFile + " " + globalConfig->getPathCESaveFile();
	if (system(copyCommand.c_str()) == -1)
		logger->Log(ERROR, "Failed to write to the CE state file. This may be a permission problem, or the folder may not exist.",
				(format("File %1% at line %2%: Unable to copy CE state tmp file to CE state file. System call to '%3' failed")
						%__LINE__%__FILE__%copyCommand).str());
}

void Nova::Reload()
{
	// Aquire a lock to stop the other threads from classifying till we're done
	pthread_rwlock_wrlock(&suspectTableLock);

	// Clear the enabledFeatures count
	enabledFeatures = 0;
	LoadConfiguration();

	// Clear max and min values
	for (int i = 0; i < DIM; i++)
		maxFeatureValues[i] = 0;

	for (int i = 0; i < DIM; i++)
		minFeatureValues[i] = 0;

	for (int i = 0; i < DIM; i++)
		meanFeatureValues[i] = 0;

	dataPtsWithClass.clear();

	// Reload the configuration file
	globalConfig->LoadConfig();

	// Did our data file move?
	globalConfig->getPathTrainingFile() = userHomePath + "/" +globalConfig->getPathTrainingFile();
	outFile = globalConfig->getPathTrainingFile().c_str();

	// Reload the data file
	if (dataPts != NULL)
		annDeallocPts(dataPts);
	if (normalizedDataPts != NULL)
		annDeallocPts(normalizedDataPts);
	LoadDataPointsFromFile(globalConfig->getPathTrainingFile());

	// Set everyone to be reclassified
	// XXX 'suspectsSinceLastSave' SuspectTableIterator todo
	for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
		it->second->SetNeedsClassificationUpdate(true);

	pthread_rwlock_unlock(&suspectTableLock);
}

void *Nova::ClassificationLoop(void *ptr)
{
	//Builds the GUI address
	string GUIKey = userHomePath + NOVAD_IN_FILENAME;
	GUISendRemote.sun_family = AF_UNIX;
	strcpy(GUISendRemote.sun_path, GUIKey.c_str());
	GUILen = strlen(GUISendRemote.sun_path) + sizeof(GUISendRemote.sun_family);

	//Builds the Silent Alarm Network address
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(globalConfig->getSaPort());

	LoadStateFile();

	//Classification Loop
	do
	{
		sleep(globalConfig->getClassificationTimeout());
		pthread_rwlock_wrlock(&suspectTableLock);
		//Calculate the "true" Feature Set for each Suspect
		// XXX 'suspects' SuspectTableIterator todo
		for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
		{
			if(it->second->GetNeedsFeatureUpdate())
			{
				for(uint i = 0; i < it->second->m_evidence.size(); i++)
				{
					it->second->m_features.m_unsentData->UpdateEvidence(it->second->m_evidence[i]);
				}
				it->second->m_evidence.clear();
				it->second->CalculateFeatures(featureMask);
			}
		}
		//Calculate the normalized feature sets, actually used by ANN
		//	Writes into Suspect ANNPoints
		NormalizeDataPoints();

		//Perform classification on each suspect
		// XXX 'suspects' SuspectTableIterator todo
		for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
		{
			if(it->second->GetNeedsClassificationUpdate())
			{
				oldClassification = it->second->GetIsHostile();
				Classify(it->second);

				//If suspect is hostile and this Nova instance has unique information
				// 			(not just from silent alarms)
				if(it->second->GetIsHostile() || oldClassification)
				{
					if(it->second->GetIsLive())
						SilentAlarm(it->second);
				}
				SendToUI(it->second);
			}
		}
		pthread_rwlock_unlock(&suspectTableLock);

		if (globalConfig->getSaveFreq() > 0)
			if ((time(NULL) - lastSaveTime) > globalConfig->getSaveFreq())
			{
				pthread_rwlock_wrlock(&suspectTableLock);
				AppendToStateFile();
				pthread_rwlock_unlock(&suspectTableLock);
			}

		if (globalConfig->getDataTTL() > 0)
			if ((time(NULL) - lastLoadTime) > globalConfig->getDataTTL())
			{
				pthread_rwlock_wrlock(&suspectTableLock);
				AppendToStateFile();
				RefreshStateFile();

				// XXX 'suspects' SuspectTableIterator todo
				for (SuspectHashTable::iterator it = suspects.begin(); it != suspects.end(); it++)
					delete it->second;
				suspects.clear();

				// XXX 'suspects' SuspectTableIterator todo
				for (SuspectHashTable::iterator it = suspectsSinceLastSave.begin(); it != suspectsSinceLastSave.end(); it++)
					delete it->second;
				suspectsSinceLastSave.clear();

				LoadStateFile();
				pthread_rwlock_unlock(&suspectTableLock);
			}

	} while(globalConfig->getClassificationTimeout());

	//Shouldn't get here!!
	if(globalConfig->getClassificationTimeout())
		logger->Log(CRITICAL, "The code should never get here, something went very wrong.", (format("File %1% at line %2%: Should never get here")%__LINE__%__FILE__).str());

	return NULL;
}


void *Nova::TrainingLoop(void *ptr)
{
	//Builds the GUI address
	string GUIKey = userHomePath + NOVAD_IN_FILENAME;
	GUISendRemote.sun_family = AF_UNIX;
	strcpy(GUISendRemote.sun_path, GUIKey.c_str());
	GUILen = strlen(GUISendRemote.sun_path) + sizeof(GUISendRemote.sun_family);


	//Training Loop
	do
	{
		sleep(globalConfig->getClassificationTimeout());
		ofstream myfile (trainingCapFile.data(), ios::app);

		if (myfile.is_open())
		{
			pthread_rwlock_wrlock(&suspectTableLock);
			//Calculate the "true" Feature Set for each Suspect
			for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
			{

				if(it->second->GetNeedsFeatureUpdate())
				{
					it->second->CalculateFeatures(featureMask);
					if(it->second->m_annPoint == NULL)
						it->second->m_annPoint = annAllocPt(DIM);


					for(int j=0; j < DIM; j++)
						it->second->m_annPoint[j] = it->second->m_features.m_features[j];

						myfile << string(inet_ntoa(it->second->GetInAddr())) << " ";

						for(int j=0; j < DIM; j++)
							myfile << it->second->m_annPoint[j] << " ";

						myfile << "\n";

					it->second->SetNeedsFeatureUpdate(false);
					//cout << it->second->ToString(featureEnabled);
					SendToUI(it->second);
				}
			}
			pthread_rwlock_unlock(&suspectTableLock);
		}
		else
		{
			logger->Log(CRITICAL, (format("File %1% at line %2%: Unable to open the training capture file %3% for writing. Can not save training data.")
					%__LINE__%__FILE__%trainingCapFile).str());
		}
		myfile.close();
	} while(globalConfig->getClassificationTimeout());

	//Shouldn't get here!
	if (globalConfig->getClassificationTimeout())
		logger->Log(CRITICAL, "The code should never get here, something went very wrong.", (format("File %1% at line %2%: Should never get here")
				%__LINE__%__FILE__).str());

	return NULL;
}

void *Nova::SilentAlarmLoop(void *ptr)
{
	int sockfd;
	u_char buf[MAX_MSG_SIZE];
	struct sockaddr_in sendaddr;

	if((sockfd = socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		logger->Log(CRITICAL, (format("File %1% at line %2%: Unable to create the silent alarm socket. Errno: %3%")%__LINE__%__FILE__%strerror(errno)).str());
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	sendaddr.sin_family = AF_INET;
	sendaddr.sin_port = htons(globalConfig->getSaPort());
	sendaddr.sin_addr.s_addr = INADDR_ANY;

	memset(sendaddr.sin_zero,'\0', sizeof sendaddr.sin_zero);
	struct sockaddr* sockaddrPtr = (struct sockaddr*) &sendaddr;
	socklen_t sendaddrSize = sizeof sendaddr;

	if(bind(sockfd,sockaddrPtr,sendaddrSize) == -1)
	{
		logger->Log(CRITICAL, (format("File %1% at line %2%: Unable to bind to the silent alarm socket. Errno: %3%")%__LINE__%__FILE__%strerror(errno)).str());
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	stringstream ss;
	ss << "sudo iptables -A INPUT -p udp --dport " << globalConfig->getSaPort() << " -j REJECT --reject-with icmp-port-unreachable";
	if(system(ss.str().c_str()) == -1)
	{
	    logger->Log(ERROR, "Failed to update iptables.", "Failed to update iptables.");
	}
	ss.str("");
	ss << "sudo iptables -A INPUT -p tcp --dport " << globalConfig->getSaPort() << " -j REJECT --reject-with tcp-reset";
	if(system(ss.str().c_str()) == -1)
	{
	    logger->Log(ERROR, "Failed to update iptables.", "Failed to update iptables.");
	}

    if(listen(sockfd, SOCKET_QUEUE_SIZE) == -1)
    {
    	logger->Log(CRITICAL, (format("File %1% at line %2%: Unable to listen on the silent alarm socket. Errno: %3%")%__LINE__%__FILE__%strerror(errno)).str());
		close(sockfd);
        exit(EXIT_FAILURE);
    }

	int connectionSocket, bytesRead;

	//Accept incoming Silent Alarm TCP Connections
	while(1)
	{

		bzero(buf, MAX_MSG_SIZE);

		//Blocking call
		if((connectionSocket = accept(sockfd, sockaddrPtr, &sendaddrSize)) == -1)
		{
			logger->Log(ERROR, (format("File %1% at line %2%: Problem when accepting incoming silent alarm connection. Errno: %3%")
					%__LINE__%__FILE__%strerror(errno)).str());
			close(connectionSocket);
			continue;
		}

		if((bytesRead = recv(connectionSocket, buf, MAX_MSG_SIZE, MSG_WAITALL)) == -1)
		{
			logger->Log(CRITICAL, (format("File %1% at line %2%: Problem when recieving incomping silent alarm connection. Errno: %3%")
					%__LINE__%__FILE__%strerror(errno)).str());
			close(connectionSocket);
			continue;
		}

		//If this is from ourselves, then drop it.
		if(hostAddr.sin_addr.s_addr == sendaddr.sin_addr.s_addr)
		{
			close(connectionSocket);
			continue;
		}
		CryptBuffer(buf, bytesRead, DECRYPT);

		pthread_rwlock_wrlock(&suspectTableLock);

		uint addr = GetSerializedAddr(buf);
		SuspectHashTable::iterator it = suspects.find(addr);

		//If this is a new suspect put it in the table
		if(it == suspects.end())
		{
			suspects[addr] = new Suspect();
			suspects[addr]->DeserializeSuspectWithData(buf, BROADCAST_DATA);
			//We set isHostile to false so that when we classify the first time
			// the suspect will go from benign to hostile and be sent to the doppelganger module
			suspects[addr]->SetIsHostile(false);
		}
		//If this suspect exists, update the information
		else
		{
			//This function will overwrite everything except the information used to calculate the classification
			// a combined classification will be given next classification loop
			suspects[addr]->DeserializeSuspectWithData(buf, BROADCAST_DATA);
		}
		suspects[addr]->SetFlaggedByAlarm(true);
		//We need to move host traffic data from broadcast into the bin for this host, and remove the old bin
		logger->Log(CRITICAL, (format("File %1% at line %2%: Got a silent alarm!. Suspect: %3%")%__LINE__%__FILE__%(suspects[addr]->ToString(featureEnabled))).str());

		pthread_rwlock_unlock(&suspectTableLock);

		if(!globalConfig->getClassificationTimeout())
			ClassificationLoop(NULL);

		close(connectionSocket);
	}
	close(sockfd);
	logger->Log(CRITICAL, "The code should never get here, something went very wrong.", (format("File %1% at line %2%: Should never get here")%__LINE__%__FILE__).str());
	return NULL;
}


void Nova::FormKdTree()
{
	delete kdTree;
	//Normalize the data points
	//Foreach data point
	for(int j = 0;j < enabledFeatures;j++)
	{
		//Foreach feature within the data point
		for(int i=0;i < nPts;i++)
		{
			if(maxFeatureValues[j] != 0)
			{
				normalizedDataPts[i][j] = Normalize(normalization[j], dataPts[i][j], minFeatureValues[j], maxFeatureValues[j]);
			}
			else
			{
				logger->Log(ERROR, (format("File %1% at line %2%: The max value of a feature was 0. Is the training data file corrupt or missing?")%__LINE__%__FILE__).str());
				break;
			}
		}
	}
	kdTree = new ANNkd_tree(					// build search structure
			normalizedDataPts,					// the data points
					nPts,						// number of points
					enabledFeatures);						// dimension of space
	updateKDTree = false;
}


void Nova::Classify(Suspect *suspect)
{
	int k = globalConfig->getK();
	ANNidxArray nnIdx = new ANNidx[k];			// allocate near neigh indices
	ANNdistArray dists = new ANNdist[k];		// allocate near neighbor dists

	kdTree->annkSearch(							// search
			suspect->m_annPoint,					// query point
			k,									// number of near neighbors
			nnIdx,								// nearest neighbors (returned)
			dists,								// distance (returned)
			globalConfig->getEps());								// error bound

	for (int i = 0; i < DIM; i++)
		suspect->m_featureAccuracy[i] = 0;

	suspect->SetHostileNeighbors(0);

	//Determine classification according to weight by distance
	//	.5 + E[(1-Dist) * Class] / 2k (Where Class is -1 or 1)
	//	This will make the classification range from 0 to 1
	double classifyCount = 0;

	for (int i = 0; i < k; i++)
	{
		dists[i] = sqrt(dists[i]);				// unsquare distance

		for (int j = 0; j < DIM; j++)
		{
			if (featureEnabled[j])
			{
				double distance = suspect->m_annPoint[j] - kdTree->thePoints()[nnIdx[i]][j];
				if (distance < 0)
					distance *= -1;

				suspect->m_featureAccuracy[j] += distance;
			}
		}

		if(nnIdx[i] == -1)
		{
			logger->Log(ERROR, (format("File %1% at line %2%: Unable to find a nearest neighbor for Data point %3% Try decreasing the Error bound")
					%__LINE__%__FILE__%i).str());
		}
		else
		{
			//If Hostile
			if(dataPtsWithClass[nnIdx[i]]->m_classification == 1)
			{
				classifyCount += (sqrtDIM - dists[i]);
				suspect->SetHostileNeighbors(suspect->GetHostileNeighbors()+1);
			}
			//If benign
			else if(dataPtsWithClass[nnIdx[i]]->m_classification == 0)
			{
				classifyCount -= (sqrtDIM - dists[i]);
			}
			else
			{
				//error case; Data points must be 0 or 1
				logger->Log(ERROR, (format("File %1% at line %2%: Data point has invalid classification. Should by 0 or 1, but is %3%")
						%__LINE__%__FILE__%dataPtsWithClass[nnIdx[i]]->m_classification).str());

				suspect->SetClassification(-1);
				delete [] nnIdx;							// clean things up
				delete [] dists;
				annClose();
				return;
			}
		}
	}

	for (int j = 0; j < DIM; j++)
				suspect->m_featureAccuracy[j] /= k;


	suspect->SetClassification(.5 + (classifyCount / ((2.0 * (double)k) * sqrtDIM )));

	// Fix for rounding errors caused by double's not being precise enough if DIM is something like 2
	if (suspect->GetClassification() < 0)
		suspect->SetClassification(0);
	else if (suspect->GetClassification() > 1)
		suspect->SetClassification(1);

	if( suspect->GetClassification() > globalConfig->getClassificationThreshold())
	{
		suspect->SetIsHostile(true);
	}
	else
	{
		suspect->SetIsHostile(false);
	}
	delete [] nnIdx;							// clean things up
    delete [] dists;

    annClose();
	suspect->SetNeedsClassificationUpdate(false);
}


void Nova::NormalizeDataPoints()
{
	for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
	{
		// Used for matching the 0->DIM index with the 0->enabledFeatures index
		int ai = 0;
		for(int i = 0;i < DIM;i++)
		{
			if (featureEnabled[i])
			{
				if(it->second->m_features.m_features[i] > maxFeatureValues[ai])
				{
					//For proper normalization the upper bound for a feature is the max value of the data.
					it->second->m_features.m_features[i] = maxFeatureValues[ai];
				}
				else if (it->second->m_features.m_features[i] < minFeatureValues[ai])
				{
					it->second->m_features.m_features[i] = minFeatureValues[ai];
				}
				ai++;
			}

		}
	}

	//if(updateKDTree) FormKdTree();

	//Normalize the suspect points
	for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
	{
		if(it->second->GetNeedsFeatureUpdate())
		{
			if(it->second->m_annPoint == NULL)
				it->second->m_annPoint = annAllocPt(enabledFeatures);

			int ai = 0;
			for(int i = 0;i < DIM;i++)
			{
				if (featureEnabled[i])
				{
					if(maxFeatureValues[ai] != 0)
						it->second->m_annPoint[ai] = Normalize(normalization[i], it->second->m_features.m_features[i], minFeatureValues[ai], maxFeatureValues[ai]);
					else
						logger->Log(ERROR, (format("File %1% at line %2%: Max value for a feature is 0. Normalization failed. Is the training data corrupt or missing?")
								%__LINE__%__FILE__).str());
					ai++;
				}

			}
			it->second->SetNeedsFeatureUpdate(false);
		}
	}
}


void Nova::PrintPt(ostream &out, ANNpoint p)
{
	out << "(" << p[0];
	for (int i = 1;i < enabledFeatures;i++)
	{
		out << ", " << p[i];
	}
	out << ")\n";
}


void Nova::LoadDataPointsFromFile(string inFilePath)
{
	ifstream myfile (inFilePath.data());
	string line;

	//string array to check whether a line in data.txt file has the right number of fields
	string fieldsCheck[DIM];
	bool valid = true;

	int i = 0;
	int k = 0;
	int badLines = 0;

	//Count the number of data points for allocation
	if (myfile.is_open())
	{
		while (!myfile.eof())
		{
			if(myfile.peek() == EOF)
			{
				break;
			}
			getline(myfile,line);
			i++;
		}
	}

	else
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the training data file at %3%")%__LINE__%__FILE__%globalConfig->getPathTrainingFile()).str());
	}

	myfile.close();
	int maxPts = i;

	//Open the file again, allocate the number of points and assign
	myfile.open(inFilePath.data(), ifstream::in);
	dataPts = annAllocPts(maxPts, enabledFeatures); // allocate data points
	normalizedDataPts = annAllocPts(maxPts, enabledFeatures);


	if (myfile.is_open())
	{
		i = 0;

		while (!myfile.eof() && (i < maxPts))
		{
			k = 0;

			if(myfile.peek() == EOF)
			{
				break;
			}

			//initializes fieldsCheck to have all array indices contain the string "NotPresent"
			for(int j = 0; j < DIM; j++)
			{
				fieldsCheck[j] = "NotPresent";
			}

			//this will grab a line of values up to a newline or until DIM values have been taken in.
			while(myfile.peek() != '\n' && k < DIM)
			{
				getline(myfile, fieldsCheck[k], ' ');
				k++;
			}

			//starting from the end of fieldsCheck, if NotPresent is still inside the array, then
			//the line of the data.txt file is incorrect, set valid to false. Note that this
			//only works in regards to the 9 data points preceding the classification,
			//not the classification itself.
			for(int m = DIM - 1; m >= 0 && valid; m--)
			{
				if(!fieldsCheck[m].compare("NotPresent"))
				{
					valid = false;
				}
			}

			//if the next character is a newline after extracting as many data points as possible,
			//then the classification is not present. For now, I will merely discard the line;
			//there may be a more elegant way to do it. (i.e. pass the data to Classify or something)
			if(myfile.peek() == '\n' || myfile.peek() == ' ')
			{
				valid = false;
			}

			//if the line is valid, continue as normal
			if(valid)
			{
				dataPtsWithClass.push_back(new Point(enabledFeatures));

				// Used for matching the 0->DIM index with the 0->enabledFeatures index
				int actualDimension = 0;
				for(int defaultDimension = 0;defaultDimension < DIM;defaultDimension++)
				{
					double temp = strtod(fieldsCheck[defaultDimension].data(), NULL);

					if (featureEnabled[defaultDimension])
					{
						dataPtsWithClass[i]->m_annPoint[actualDimension] = temp;
						dataPts[i][actualDimension] = temp;

						//Set the max values of each feature. (Used later in normalization)
						if(temp > maxFeatureValues[actualDimension])
							maxFeatureValues[actualDimension] = temp;

						meanFeatureValues[actualDimension] += temp;

						actualDimension++;
					}
				}
				getline(myfile,line);
				dataPtsWithClass[i]->m_classification = atoi(line.data());
				i++;
			}
			//but if it isn't, just get to the next line without incrementing i.
			//this way every correct line will be inserted in sequence
			//without any gaps due to perhaps multiple line failures, etc.
			else
			{
				getline(myfile,line);
				badLines++;
			}
		}
		nPts = i;

		for (int j = 0; j < DIM; j++)
			meanFeatureValues[j] /= nPts;
	}
	else
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the training data file at %3%")%__LINE__%__FILE__%globalConfig->getPathTrainingFile()).str());
	}
	myfile.close();

	//Normalize the data points

	//Foreach feature within the data point
	for(int j = 0;j < enabledFeatures;j++)
	{
		//Foreach data point
		for(int i=0;i < nPts;i++)
		{
			normalizedDataPts[i][j] = Normalize(normalization[j], dataPts[i][j], minFeatureValues[j], maxFeatureValues[j]);
		}
	}

	kdTree = new ANNkd_tree(					// build search structure
			normalizedDataPts,					// the data points
					nPts,						// number of points
					enabledFeatures);						// dimension of space
}

double Nova::Normalize(normalizationType type, double value, double min, double max)
{
	switch (type)
	{
		case LINEAR:
		{
			return value / max;
		}
		case LINEAR_SHIFT:
		{
			return (value -min) / (max - min);
		}
		case LOGARITHMIC:
		{
			if(!value || !max)
				return 0;
			else return(log(value)/log(max));
			//return (log(value - min + 1)) / (log(max - min + 1));
		}
		case NONORM:
		{
			return value;
		}
		default:
		{
			//logger->Logging(ERROR, "Normalization failed: Normalization type unkown");
			return 0;
		}

		// TODO: A sigmoid normalization function could be very useful,
		// especially if we could somehow use it interactively to set the center and smoothing
		// while looking at the data visualizations to see what works best for a feature
	}
}


void Nova::WriteDataPointsToFile(string outFilePath, ANNkd_tree* tree)
{
	ofstream myfile (outFilePath.data());

	if (myfile.is_open())
	{
		for (int i = 0; i < tree->nPoints(); i++ )
		{
			for(int j=0; j < tree->theDim(); j++)
			{
				myfile << tree->thePoints()[i][j] << " ";
			}
			myfile << dataPtsWithClass[i]->m_classification;
			myfile << "\n";
		}
	}
	else
	{
		logger->Log(ERROR, (format("File %1% at line %2%: Unable to open the training data file at %3%")%__LINE__%__FILE__%outFilePath).str());

	}
	myfile.close();

}


void Nova::SilentAlarm(Suspect *suspect)
{
	char suspectAddr[INET_ADDRSTRLEN];
	string commandLine;
	string hostAddrString = GetLocalIP(globalConfig->getInterface().c_str());
	u_char serializedBuffer[MAX_MSG_SIZE];

	uint dataLen = suspect->SerializeSuspect(serializedBuffer);

	//If the hostility hasn't changed don't bother the DM
	if(oldClassification != suspect->GetIsHostile())
	{
		if(suspect->GetIsHostile() && globalConfig->getIsDmEnabled())
		{
			in_addr temp = suspect->GetInAddr();
			inet_ntop(AF_INET, &(temp), suspectAddr, INET_ADDRSTRLEN);

			commandLine = "sudo iptables -t nat -A PREROUTING -d ";
			commandLine += hostAddrString;
			commandLine += " -s ";
			commandLine += suspectAddr;
			commandLine += " -j DNAT --to-destination ";
			commandLine += globalConfig->getDoppelIp();

			system(commandLine.c_str());
		}
		else
		{
			in_addr temp = suspect->GetInAddr();
			inet_ntop(AF_INET, &(temp), suspectAddr, INET_ADDRSTRLEN);

			commandLine = "sudo iptables -t nat -D PREROUTING -d ";
			commandLine += hostAddrString;
			commandLine += " -s ";
			commandLine += suspectAddr;
			commandLine += " -j DNAT --to-destination ";
			commandLine += globalConfig->getDoppelIp();

			system(commandLine.c_str());
		}
	}
	if(suspect->m_features.m_unsentData->m_packetCount)
	{
		do
		{
			dataLen = suspect->SerializeSuspect(serializedBuffer);

			// Serialize the unsent data
			dataLen += suspect->m_features.m_unsentData->SerializeFeatureData(serializedBuffer+dataLen);
			// Move the unsent data to the sent side
			suspect->m_features.UpdateFeatureData(true);
			// Clear the unsent data
			suspect->m_features.m_unsentData->clearFeatureData();

			//Update other Nova Instances with latest suspect Data
			for(uint i = 0; i < globalConfig->getNeighbors().size(); i++)
			{
				serv_addr.sin_addr.s_addr = globalConfig->getNeighbors()[i];

				stringstream ss;
				string commandLine;

				ss << "sudo iptables -I INPUT -s " << string(inet_ntoa(serv_addr.sin_addr)) << " -p tcp -j ACCEPT";
				commandLine = ss.str();

				if(system(commandLine.c_str()) == -1)
				{
					logger->Log(INFO, "Failed to update iptables.", "Failed to update iptables.");
				}


				int i;
				for(i = 0; i < globalConfig->getSaMaxAttempts(); i++)
				{
					if(KnockPort(OPEN))
					{
						//Send Silent Alarm to other Nova Instances with feature Data
						if ((sockfd = socket(AF_INET,SOCK_STREAM,6)) == -1)
						{
							logger->Log(ERROR, (format("File %1% at line %2%: Unable to open socket to send silent alarm. Errno: %3%")
									%__LINE__%__FILE__%strerror(errno)).str());
							close(sockfd);
							continue;
						}
						if (connect(sockfd, serv_addrPtr, inSocketSize) == -1)
						{
							//If the host isn't up we stop trying
							if(errno == EHOSTUNREACH)
							{
								//set to max attempts to hit the failed connect condition
								i = globalConfig->getSaMaxAttempts();
								logger->Log(ERROR, (format("File %1% at line %2%: Unable to connect to host to send silent alarm. Errno: %3%")
										%__LINE__%__FILE__%strerror(errno)).str());
								break;
							}
							logger->Log(ERROR, (format("File %1% at line %2%: Unable to open socket to send silent alarm. Errno: %3%")
									%__LINE__%__FILE__%strerror(errno)).str());
							close(sockfd);
							continue;
						}
						break;
					}
				}
				//If connecting failed
				if(i == globalConfig->getSaMaxAttempts() )
				{
					close(sockfd);
					ss.str("");
					ss << "sudo iptables -D INPUT -s " << string(inet_ntoa(serv_addr.sin_addr)) << " -p tcp -j ACCEPT";
					commandLine = ss.str();
					if(system(commandLine.c_str()) == -1)
					{
						logger->Log(ERROR, "Failed to update iptables.", "Failed to update iptables.");
					}
					continue;
				}

				if( send(sockfd, serializedBuffer, dataLen, 0) == -1)
				{
					logger->Log(ERROR, (format("File %1% at line %2%: Error in TCP Send of silent alarm. Errno: %3%")%__LINE__%__FILE__%strerror(errno)).str());
					close(sockfd);
					ss.str("");
					ss << "sudo iptables -D INPUT -s " << string(inet_ntoa(serv_addr.sin_addr)) << " -p tcp -j ACCEPT";
					commandLine = ss.str();
					if(system(commandLine.c_str()) == -1)
					{
						logger->Log(INFO, "Failed to update iptables.", "Failed to update iptables.");
					}
					continue;
				}
				close(sockfd);
				KnockPort(CLOSE);
				ss.str("");
				ss << "sudo iptables -D INPUT -s " << string(inet_ntoa(serv_addr.sin_addr)) << " -p tcp -j ACCEPT";
				commandLine = ss.str();
				if(system(commandLine.c_str()) == -1)
				{
					logger->Log(ERROR, "Failed to update iptables.", "Failed to update iptables.");
				}
			}
		}while(dataLen == MORE_DATA);
	}
}


bool Nova::KnockPort(bool mode)
{
	stringstream ss;
	ss << globalConfig->getKey();

	//mode == OPEN (true)
	if(mode)
		ss << "OPEN";

	//mode == CLOSE (false)
	else
		ss << "SHUT";

	uint keyDataLen = globalConfig->getKey().size() + 4;
	u_char keyBuf[1024];
	bzero(keyBuf, 1024);
	memcpy(keyBuf, ss.str().c_str(), ss.str().size());

	CryptBuffer(keyBuf, keyDataLen, ENCRYPT);

	//Send Port knock to other Nova Instances
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 17)) == -1)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Error in port knocking. Can't create socket: %s")%__FILE__%__LINE__%strerror(errno)).str());
		close(sockfd);
		return false;
	}

	if( sendto(sockfd,keyBuf,keyDataLen, 0,serv_addrPtr, inSocketSize) == -1)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Error in UDP Send for port knocking: %s")%__FILE__%__LINE__%strerror(errno)).str());
		close(sockfd);
		return false;
	}

	close(sockfd);
	sleep(globalConfig->getSaSleepDuration());
	return true;
}


bool Nova::Start_Packet_Handler()
{
	char errbuf[PCAP_ERRBUF_SIZE];

	int ret;
	usePcapFile = globalConfig->getReadPcap();
	string haystackAddresses_csv = "";

	struct bpf_program fp;			/* The compiled filter expression */
	char filter_exp[64];


	haystackAddresses = GetHaystackAddresses(globalConfig->getPathConfigHoneydHs());
	haystackDhcpAddresses = GetHaystackDhcpAddresses(dhcpListFile);
	haystackAddresses_csv = ConstructFilterString();

	//If we're reading from a packet capture file
	if(usePcapFile)
	{
		sleep(1); //To allow time for other processes to open
		handle = pcap_open_offline(globalConfig->getPathPcapFile().c_str(), errbuf);

		if(handle == NULL)
		{
			logger->Log(CRITICAL, (format("File %1% at line %2%: Couldn't open file: %3%: %4%")%__FILE__%__LINE__%globalConfig->getPathPcapFile().c_str()%errbuf).str());
			exit(EXIT_FAILURE);
		}
		if (pcap_compile(handle, &fp, haystackAddresses_csv.data(), 0, maskp) == -1)
		{
			logger->Log(CRITICAL, (format("File %1% at line %2%: Couldn't parse filter: %3%: %4%")%__LINE__%filter_exp%pcap_geterr(handle)).str());
			exit(EXIT_FAILURE);
		}

		if (pcap_setfilter(handle, &fp) == -1)
		{
			logger->Log(CRITICAL, (format("File %1% at line %2%: Couldn't install filter: %3%: %4%")% __FILE__%__LINE__%filter_exp%pcap_geterr(handle)).str());
			exit(EXIT_FAILURE);
		}
		//First process any packets in the file then close all the sessions
		pcap_loop(handle, -1, Packet_Handler,NULL);

		TCPTimeout(NULL);
		if (!globalConfig->getIsTraining())
			ClassificationLoop(NULL);
		else
			TrainingLoop(NULL);

		if(globalConfig->getGotoLive()) usePcapFile = false; //If we are going to live capture set the flag.
	}


	if(!usePcapFile)
	{
		//Open in non-promiscuous mode, since we only want traffic destined for the host machine
		handle = pcap_open_live(globalConfig->getInterface().c_str(), BUFSIZ, 0, 1000, errbuf);

		if(handle == NULL)
		{
			logger->Log(ERROR, (format("File %1% at line %2%:  Couldn't open device: %3% %4%")% __FILE__%__LINE__%globalConfig->getInterface().c_str()%errbuf).str());
			exit(EXIT_FAILURE);
		}

		/* ask pcap for the network address and mask of the device */
		ret = pcap_lookupnet(globalConfig->getInterface().c_str(), &netp, &maskp, errbuf);

		if(ret == -1)
		{
			logger->Log(ERROR, (format("File %1% at line %2%: %3%")% __FILE__%__LINE__%errbuf).str());
			exit(EXIT_FAILURE);
		}

		if (pcap_compile(handle, &fp, haystackAddresses_csv.data(), 0, maskp) == -1)
		{
			logger->Log(ERROR, (format("File %1% at line %2%:  Couldn't parse filter: %3% %4%")% __FILE__%__LINE__% filter_exp%pcap_geterr(handle)).str());
			exit(EXIT_FAILURE);
		}

		if (pcap_setfilter(handle, &fp) == -1)
		{
			logger->Log(ERROR, (format("File %1% at line %2%:  Couldn't install filter:%3% %4%")% __FILE__%__LINE__% filter_exp%pcap_geterr(handle)).str());
			exit(EXIT_FAILURE);
		}
		//"Main Loop"
		//Runs the function "Packet_Handler" every time a packet is received
		pthread_create(&TCP_timeout_thread, NULL, TCPTimeout, NULL);

	    pcap_loop(handle, -1, Packet_Handler, NULL);
	}
	return false;
}

void Nova::Packet_Handler(u_char *useless,const struct pcap_pkthdr* pkthdr,const u_char* packet)
{
	//Memory assignments moved outside packet handler to increase performance
	int dest_port;
	Packet packet_info;
	struct ether_header *ethernet;  	/* net/ethernet.h */
	struct ip *ip_hdr; 					/* The IP header */
	char tcp_socket[55];

	if(packet == NULL)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Didn't grab packet!")% __FILE__%__LINE__).str());
		return;
	}


	/* let's start with the ether header... */
	ethernet = (struct ether_header *) packet;

	/* Do a couple of checks to see what packet type we have..*/
	if (ntohs (ethernet->ether_type) == ETHERTYPE_IP)
	{
		ip_hdr = (struct ip*)(packet + sizeof(struct ether_header));

		//Prepare Packet structure
		packet_info.ip_hdr = *ip_hdr;
		packet_info.pcap_header = *pkthdr;
		//If this is to the host
		if(packet_info.ip_hdr.ip_dst.s_addr == hostAddr.sin_addr.s_addr)
			packet_info.fromHaystack = FROM_LTM;
		else
			packet_info.fromHaystack = FROM_HAYSTACK_DP;

		//IF UDP or ICMP
		if(ip_hdr->ip_p == 17 )
		{
			packet_info.udp_hdr = *(struct udphdr*) ((char *)ip_hdr + sizeof(struct ip));
			UpdateSuspect(packet_info);
		}
		else if(ip_hdr->ip_p == 1)
		{
			packet_info.icmp_hdr = *(struct icmphdr*) ((char *)ip_hdr + sizeof(struct ip));
			UpdateSuspect(packet_info);
		}
		//If TCP...
		else if(ip_hdr->ip_p == 6)
		{
			packet_info.tcp_hdr = *(struct tcphdr*)((char*)ip_hdr + sizeof(struct ip));
			dest_port = ntohs(packet_info.tcp_hdr.dest);

			bzero(tcp_socket, 55);
			snprintf(tcp_socket, 55, "%d-%d-%d", ip_hdr->ip_dst.s_addr, ip_hdr->ip_src.s_addr, dest_port);

			pthread_rwlock_wrlock(&sessionLock);
			//If this is a new entry...
			if(SessionTable[tcp_socket].session.size() == 0)
			{
				//Insert packet into Hash Table
				SessionTable[tcp_socket].session.push_back(packet_info);
				SessionTable[tcp_socket].fin = false;
			}

			//If there is already a session in progress for the given LogEntry
			else
			{
				//If Session is ending
				//TODO: The session may continue a few packets after the FIN. Account for this case.
				//See ticket #15
				if(packet_info.tcp_hdr.fin)// Runs appendToStateFile before exiting
				{
					SessionTable[tcp_socket].session.push_back(packet_info);
					SessionTable[tcp_socket].fin = true;
				}
				else
				{
					//Add this new packet to the session vector
					SessionTable[tcp_socket].session.push_back(packet_info);
				}
			}
			pthread_rwlock_unlock(&sessionLock);
		}

		// Allow for continuous classification
		if(!globalConfig->getClassificationTimeout())
		{
			if (!globalConfig->getIsTraining())
				ClassificationLoop(NULL);
			else
				TrainingLoop(NULL);
		}
	}
	else if(ntohs(ethernet->ether_type) == ETHERTYPE_ARP)
	{
		return;
	}
	else
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Unknown Non-IP Packet Received")% __FILE__%__LINE__).str());
		return;
	}
}

void Nova::SaveSuspectsToFile(string filename)
{
	logger->Log(ERROR, (format("File %1% at line %2%:  Got request to save file to %3%")% __FILE__%__LINE__% filename).str());
	dataPtsWithClass.push_back(new Point(enabledFeatures));

	ofstream out(filename.c_str());

	if(!out.is_open())
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Error: Unable to open file %3% to save suspect data.")% __FILE__%__LINE__% filename).str());
		return;
	}

	pthread_rwlock_rdlock(&suspectTableLock);
	for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
	{
		out << it->second->ToString(featureEnabled) << endl;
	}
	pthread_rwlock_unlock(&suspectTableLock);

	out.close();
}


void Nova::SendToUI(Suspect *suspect)
{
	if (!enableGUI)
		return;

	u_char GUIData[MAX_MSG_SIZE];

	uint GUIDataLen = suspect->SerializeSuspect(GUIData);

	if ((GUISendSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Unable to create GUI socket: %3%")% __FILE__%__LINE__% strerror(errno)).str());
		close(GUISendSocket);
		return;
	}

	if (connect(GUISendSocket, GUISendPtr, GUILen) == -1)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Unable to connect to GUI: %3%")% __FILE__%__LINE__% strerror(errno)).str());
		close(GUISendSocket);
		return;
	}

	if (send(GUISendSocket, GUIData, GUIDataLen, 0) == -1)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Unable to send to GUI: %3%")% __FILE__%__LINE__% strerror(errno)).str());
		close(GUISendSocket);
		return;
	}
	close(GUISendSocket);
}


void Nova::LoadConfiguration()
{
	string hostAddrString = GetLocalIP(globalConfig->getInterface().c_str());

	if(hostAddrString.size() == 0)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Bad interface, no IP's associated!")% __FILE__%__LINE__).str());
		exit(EXIT_FAILURE);
	}

	inet_pton(AF_INET,hostAddrString.c_str(),&(hostAddr.sin_addr));


	string enabledFeatureMask = globalConfig->getEnabledFeatures();

	featureMask = 0;
	for (uint i = 0; i < DIM; i++)
	{
		if ('1' == enabledFeatureMask.at(i))
		{
			featureEnabled[i] = true;
			featureMask += pow(2, i);
			enabledFeatures++;
		}
		else
		{
			featureEnabled[i] = false;
		}
	}

	sqrtDIM = sqrt(enabledFeatures);
}


string Nova::ConstructFilterString()
{
	//Flatten out the vectors into a csv string
	string filterString = "";

	for(uint i = 0; i < haystackAddresses.size(); i++)
	{
		filterString += "dst host ";
		filterString += haystackAddresses[i];

		if(i+1 != haystackAddresses.size())
			filterString += " || ";
	}

	if (!haystackDhcpAddresses.empty() && !haystackAddresses.empty())
		filterString += " || ";

	for(uint i = 0; i < haystackDhcpAddresses.size(); i++)
	{
		filterString += "dst host ";
		filterString += haystackDhcpAddresses[i];

		if(i+1 != haystackDhcpAddresses.size())
			filterString += " || ";
	}

	if (filterString == "")
	{
		filterString = "dst host 0.0.0.0";
	}

	logger->Log(DEBUG, "Pcap filter string is %1%" + filterString);
	return filterString;
}

void *Nova::UpdateIPFilter(void *ptr)
{
	while (true)
	{
		if (watch > 0)
		{
			int BUF_LEN =  (1024 * (sizeof (struct inotify_event)) + 16);
			char buf[BUF_LEN];
			struct bpf_program fp;			/* The compiled filter expression */
			char filter_exp[64];

			// Blocking call, only moves on when the kernel notifies it that file has been changed
			int readLen = read (notifyFd, buf, BUF_LEN);
			if (readLen > 0) {
				watch = inotify_add_watch (notifyFd, dhcpListFile.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_DELETE);
				haystackDhcpAddresses = GetHaystackDhcpAddresses(dhcpListFile);
				string haystackAddresses_csv = ConstructFilterString();

				if (pcap_compile(handle, &fp, haystackAddresses_csv.data(), 0, maskp) == -1)
					logger->Log(ERROR, (format("File %1% at line %2%:  Couldn't parse filter: %3% %4%")% __FILE__%__LINE__% filter_exp%pcap_geterr(handle)).str());

				if (pcap_setfilter(handle, &fp) == -1)
					logger->Log(ERROR, (format("File %1% at line %2%:  Couldn't install filter: %3% %4%")% __FILE__%__LINE__% filter_exp%pcap_geterr(handle)).str());
			}
		}
		else
		{
			// This is the case when there's no file to watch, just sleep and wait for it to
			// be created by honeyd when it starts up.
			sleep(2);
			watch = inotify_add_watch (notifyFd, dhcpListFile.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_DELETE);
		}
	}

	return NULL;
}

vector <string> Nova::GetHaystackDhcpAddresses(string dhcpListFile)
{
	ifstream dhcpFile(dhcpListFile.data());
	vector<string> haystackDhcpAddresses;

	if (dhcpFile.is_open())
	{
		while ( dhcpFile.good() )
		{
			string line;
			getline (dhcpFile,line);
			if (strcmp(line.c_str(), ""))
				haystackDhcpAddresses.push_back(line);
		}
		dhcpFile.close();
	}
	else cout << "Unable to open file";

	return haystackDhcpAddresses;
}

vector <string> Nova::GetHaystackAddresses(string honeyDConfigPath)
{
	//Path to the main log file
	ifstream honeydConfFile (honeyDConfigPath.c_str());
	vector <string> retAddresses;
	retAddresses.push_back(GetLocalIP(globalConfig->getInterface().c_str()));

	if( honeydConfFile == NULL)
	{
		logger->Log(ERROR, (format("File %1% at line %2%:  Error opening log file. Does it exist?")% __FILE__%__LINE__).str());
		exit(EXIT_FAILURE);
	}

	string LogInputLine;

	while(!honeydConfFile.eof())
	{
		stringstream LogInputLineStream;

		//Get the next line
		getline(honeydConfFile, LogInputLine);

		//Load the line into a stringstream for easier tokenizing
		LogInputLineStream << LogInputLine;
		string token;

		//Is the first word "bind"?
		getline(LogInputLineStream, token, ' ');

		if(token.compare( "bind" ) != 0)
		{
			continue;
		}

		//The next token will be the IP address
		getline(LogInputLineStream, token, ' ');
		retAddresses.push_back(token);
	}
	return retAddresses;
}

void *Nova::TCPTimeout(void *ptr)
{
	do
	{
		pthread_rwlock_wrlock(&sessionLock);

		time_t currentTime = time(NULL);
		time_t packetTime;

		for (TCPSessionHashTable::iterator it = SessionTable.begin() ; it != SessionTable.end(); it++ )
		{

			if(it->second.session.size() > 0)
			{
				packetTime = it->second.session.back().pcap_header.ts.tv_sec;
				//If were reading packets from a file, assume all packets have been loaded and go beyond timeout threshhold
				if(usePcapFile)
				{
					currentTime = packetTime+3+globalConfig->getTcpTimout();
				}
				// If it exists)
				if(packetTime + 2 < currentTime)
				{
					//If session has been finished for more than two seconds
					if(it->second.fin == true)
					{
						for (uint p = 0; p < (SessionTable[it->first].session).size(); p++)
						{
							pthread_rwlock_unlock(&sessionLock);
							UpdateSuspect(SessionTable[it->first].session[p]);
							pthread_rwlock_wrlock(&sessionLock);
						}

						// Allow for continuous classification
						if(!globalConfig->getClassificationTimeout())
						{
							pthread_rwlock_unlock(&sessionLock);
							if (!globalConfig->getIsTraining())
								ClassificationLoop(NULL);
							else
								TrainingLoop(NULL);
							pthread_rwlock_wrlock(&sessionLock);
						}

						SessionTable[it->first].session.clear();
						SessionTable[it->first].fin = false;
					}
					//If this session is timed out
					else if(packetTime + globalConfig->getTcpTimout() < currentTime)
					{
						for (uint p = 0; p < (SessionTable[it->first].session).size(); p++)
						{
							pthread_rwlock_unlock(&sessionLock);
							UpdateSuspect(SessionTable[it->first].session[p]);
							pthread_rwlock_wrlock(&sessionLock);
						}

						// Allow for continuous classification
						if(!globalConfig->getClassificationTimeout())
						{
							pthread_rwlock_unlock(&sessionLock);
							if (!globalConfig->getIsTraining())
								ClassificationLoop(NULL);
							else
								TrainingLoop(NULL);
							pthread_rwlock_wrlock(&sessionLock);
						}

						SessionTable[it->first].session.clear();
						SessionTable[it->first].fin = false;
					}
				}
			}
		}
		pthread_rwlock_unlock(&sessionLock);
		//Check only once every TCP_CHECK_FREQ seconds
		sleep(globalConfig->getTcpCheckFreq());
	}while(!usePcapFile);

	//After a pcap file is read we do one iteration of this function to clear out the sessions
	//This is return is to prevent an error being thrown when there isn't one.
	if(usePcapFile) return NULL;


	logger->Log(CRITICAL, "The code should never get here, something went very wrong.", (format("File %1% at line %2%: Should never get here")%__LINE__%__FILE__).str());
	return NULL;
}

void Nova::UpdateSuspect(Packet packet)
{
	in_addr_t addr = packet.ip_hdr.ip_src.s_addr;
	pthread_rwlock_wrlock(&suspectTableLock);
	//If our suspect is new
	if(suspects.find(addr) == suspects.end())
		suspects[addr] = new Suspect(packet);
	//Else our suspect exists
	else
		suspects[addr]->AddEvidence(packet);

	suspects[addr]->SetIsLive(usePcapFile);
	pthread_rwlock_unlock(&suspectTableLock);
}

