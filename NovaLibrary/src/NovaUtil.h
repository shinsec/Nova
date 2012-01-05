//============================================================================
// Name        : NovaUtil.h
// Author      : DataSoft Corporation
// Copyright   : GNU GPL v3
// Description : Utility class for storing functions that are used by multiple
//				 processes but don't warrant their own class
//============================================================================/*

#ifndef NOVAUTIL_H_
#define NOVAUTIL_H_

#include <pcap.h>
#include <math.h>
#include <errno.h>
#include <fstream>
#include <net/if.h>
#include <signal.h>
#include <sys/un.h>
#include <ANN/ANN.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>
#include <google/dense_hash_map>
#include <log4cxx/xml/domconfigurator.h>

#include "NOVAConfiguration.h"
#include "TrafficEvent.h"
#include "GUIMsg.h"
#include "Point.h"

using namespace std;
using namespace Nova;


/*********************************/
/******** Common Defines *********/
/*********************************/

//dimension
#define DIM 9

//The maximum message size, packets can only fragment up to this size
#define MAX_MSG_SIZE 65535

//The num of bytes returned by suspect serialization if it hit the maximum size;
// SerializeSuspect reqs 89 bytes :: SerializeFeatureData reqs 36 bytes
// The remaining 65410 bytes can be used for table entries :: 65410/8 == 8176.25
// Max table entries takes 65408 bytes
#define MORE_DATA 65533

//The maximum GUI message size, while this can be increased up to MAX_MSG_SIZE
// it should remain as small as possible, GUI messages aren't large.
#define MAX_GUIMSG_SIZE 256

//Number of messages to queue in a listening socket before ignoring requests until the queue is open
#define SOCKET_QUEUE_SIZE 50

//Initial sizes for dense hash map tables
#define INIT_SIZE_SMALL 256
#define INIT_SIZE_MEDIUM 1024
#define INIT_SIZE_LARGE 4096
#define INIT_SIZE_HUGE 65535

//Mode for encryption/decryption
#define ENCRYPT true
#define DECRYPT false

///	Filename of the file to be used as an IPC key
#define KEY_FILENAME "/keys/NovaIPCKey"

//If the feature data is local
#define LOCAL_DATA true
//If the feature data is broadcast from another nova instance
#define BROADCAST_DATA false

//From the Haystack or Doppelganger Module
#define FROM_HAYSTACK_DP	1
//From the Local Traffic Monitor
#define FROM_LTM			0

#define KEY_FILENAME "/keys/NovaIPCKey"
///	Filename of the file to be used as an Doppelganger IPC key
#define KEY_ALARM_FILENAME "/keys/NovaDoppIPCKey"
///	Filename of the file to be used as an Classification Engine IPC key
#define CE_FILENAME "/keys/CEKey"
/// File name of the file to be used as CE_GUI Input IPC key.
#define CE_GUI_FILENAME "/keys/GUI_CEKey"
/// File name of the file to be used as DM_GUI Input IPC key.
#define DM_GUI_FILENAME "/keys/GUI_DMKey"
/// File name of the file to be used as HS_GUI Input IPC key.
#define HS_GUI_FILENAME "/keys/GUI_HSKey"
/// File name of the file to be used as LTM_GUI Input IPC key.
#define LTM_GUI_FILENAME "/keys/GUI_LTMKey"
/// File name of the file that contains nova's install locations
#define PATHS_FILE "/etc/nova/paths"

/************************************************************************/
/********** Equality operators used by google's dense hash maps *********/
/************************************************************************/

struct eqstr
{
  bool operator()(string s1, string s2) const
  {
    return !(s1.compare(s2));
  }
};

struct eqaddr
{
  bool operator()(in_addr_t s1, in_addr_t s2) const
  {
    return (s1 == s2);
  }
};

struct eqport
{
  bool operator()(in_port_t s1, in_port_t s2) const
  {
	    return (s1 == s2);
  }
};

struct eqint
{
  bool operator()(int s1, int s2) const
  {
	    return (s1 == s2);
  }
};

struct eqtime
{
  bool operator()(time_t s1, time_t s2) const
  {
	    return (s1 == s2);
  }
};

/************************************************/
/********* Common Typedefs and Structs **********/
/************************************************/

///Hash table for current TCP Sessions
///Table key is the source network socket, comprised of IP and Port in string format
///	IE: "192.168.1.1-8080"
struct Session
{
	bool fin;
	vector<struct Packet> session;
};

///The Value is a vector of IP headers
typedef google::dense_hash_map<string, struct Session, tr1::hash<string>, eqstr > TCPSessionHashTable;

/*****************************************************************************/
/** NovaUtil namespace is ONLY for functions repeated in multiple processes **/
/** and that don't fit into a category large enough to warrant a new class ***/
/*****************************************************************************/

namespace Nova{

// Encrpyts/decrypts a char buffer of size 'size' depending on mode
void cryptBuffer(u_char * buf, uint size, bool mode);

// Reads the paths file and returns the homePath of nova
string getHomePath();

// Replaces any env vars in 'path' and returns the absolute path
string resolvePathVars(string path);

//Extracts and returns the IP Address from a serialized suspect located at buf
uint getSerializedAddr(u_char * buf);

//Gets number of bits used in the mask
int getMaskBits(in_addr_t range);

}
#endif /* NOVAUTIL_H_ */
