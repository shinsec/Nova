//============================================================================
// Name        : NowjsMethods.js
// Copyright   : DataSoft Corporation 2011-2013
//  Nova is free software: you can redistribute it and/or modify
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
// Description : Nowjs remote callabale functions
//============================================================================

var dns = require('dns');
var fs = require('fs');
var exec = require('child_process').exec;
var sanitizeCheck = require('validator').sanitize;
var NovaCommon = require('./NovaCommon.js');
var LOG = NovaCommon.LOG;

var NovaHomePath = NovaCommon.config.GetPathHome();
var NovaSharedPath = NovaCommon.config.GetPathShared();

var NowjsMethods = function(everyone) {

function objCopy(src, dst) 
{
    for (var member in src) 
    {
        if (typeof src[member] == 'function') 
        {
            dst[member] = src[member]();
        }
        // Need to think about this
        //        else if ( typeof src[member] == 'object' )
        //        {
        //            copyOver(src[member], dst[member]);
        //        }
        else 
        {
            dst[member] = src[member];
        }
    }
}

everyone.now.shutdownQuasar = function() {
    LOG("ALERT", "Quasar is exiting due to user issued shutdown command on the web interface");
    process.exit(1);
};

everyone.now.changeGroup = function(group, cb)
{
    var res = NovaCommon.config.SetGroup(group);
    cb && cb(res);
};

everyone.now.ChangeNodeInterfaces = function(nodes, newIntf, cb)
{
    NovaCommon.honeydConfig.ChangeNodeInterfaces(nodes, newIntf);
    cb && cb();
};

everyone.now.GetProfileNames = function(cb)
{
    var res = NovaCommon.honeydConfig.GetProfileNames();
    cb && cb(res);
};

everyone.now.GetLeafProfileNames = function(cb)
{
  var res = NovaCommon.honeydConfig.GetLeafProfileNames();
  cb && cb(res);
};

everyone.now.addScriptOptionValue = function (script, key, value, cb) {
    NovaCommon.honeydConfig.AddScriptOptionValue(script, key, value);
    NovaCommon.honeydConfig.SaveAll();
    cb && cb();
};

everyone.now.deleteScriptOptionValue = function (script, key, value, cb) {
    NovaCommon.honeydConfig.DeleteScriptOptionValue(script, key, value);
    NovaCommon.honeydConfig.SaveAll();
    cb && cb();
};

everyone.now.createHoneydNodes = function(ipType, ip1, ip2, ip3, ip4, profile, portSet, vendor, ethinterface, count, cb)
{
    var ipAddress;
    if (ipType == "DHCP")
    {
        ipAddress = "DHCP";
    } else {
        ipAddress = ip1 + "." + ip2 + "." + ip3 + "." + ip4;
    }

    var result = null;
    if (!NovaCommon.honeydConfig.AddNodes(profile, portSet, vendor, ipAddress, ethinterface, Number(count)))
    {
        result = "Unable to create new nodes";  
    }

    if (!NovaCommon.honeydConfig.SaveAll())
    {
        result = "Unable to save honeyd configuration";
    }

    cb && cb(result);
};

everyone.now.SaveDoppelganger = function(node, cb)
{
    var ipAddress = node.ip;
    if (node.ipType == "DHCP")
    {
        ipAddress = "DHCP";
    }

    if (!NovaCommon.honeydConfig.SaveDoppelganger(node.profile, node.portSet, ipAddress, node.mac, node.intface))
    {
        cb && cb("SaveDoppelganger Failed");
        return;
    } else {
        if (!NovaCommon.honeydConfig.SaveAll())
        {
            cb && cb("Unable to save honeyd configuration");
        } else {
            cb && cb(null);
        }
    }
};

everyone.now.SaveHoneydNode = function(node, cb)
{
    var ipAddress = node.ip;
    if (node.ipType == "DHCP")
    {
        ipAddress = "DHCP";
    }

    // Delete the old node and then add the new one 
    NovaCommon.honeydConfig.DeleteNode(node.oldName);

    if (node.oldName == "doppelganger")
    {
        if (!NovaCommon.honeydConfig.SetDoppelganger(node.profile, node.portSet, ipAddress, node.mac, node.intface))
        {
            cb && cb("doppelganger Failed");
            return;
        } else {
            if (!NovaCommon.honeydConfig.SaveAll())
            {
                cb && cb("Unable to save honeyd configuration");
            } else {
                cb && cb(null);
            }
        }

    } else {
        if (!NovaCommon.honeydConfig.AddNode(node.profile, node.portSet, ipAddress, node.mac, node.intface))
        {
            cb && cb("AddNode Failed");
            return;
        } else {
            if (!NovaCommon.honeydConfig.SaveAll())
            {
                cb && cb("Unable to save honeyd configuration");
            } else {
                cb && cb(null);
            }
        }
    }

};

everyone.now.ClearAllSuspects = function (cb)
{
    NovaCommon.nova.CheckConnection();
    if (!NovaCommon.nova.ClearAllSuspects())
    {
        console.log("Manually deleting CE state file:" + NovaHomePath + "/" + NovaCommon.config.ReadSetting("CE_SAVE_FILE"));
        // If we weren't able to tell novad to clear the suspects, at least delete the CEStateFile
        try {
            fs.unlinkSync(NovaHomePath + "/" + NovaCommon.config.ReadSetting("CE_SAVE_FILE"));
        } catch (err)
        {
            // this is probably because the file doesn't exist. Just ignore.
        }
    }
};

everyone.now.ClearSuspect = function (suspectIp, ethinterface, cb)
{
    NovaCommon.nova.CheckConnection();
    var result = NovaCommon.nova.ClearSuspect(suspectIp, ethinterface);

    if (cb != undefined)
    {
        cb(result);
    }
};

everyone.now.GetInheritedEthernetList = function (parent, cb)
{
    var prof = NovaCommon.honeydConfig.GetProfile(parent);

    if (prof == null)
    {
        console.log("ERROR Getting profile " + parent);
        cb(null);
    } else {
        cb(prof.GetVendors(), prof.GetVendorCounts());
    }
};

everyone.now.RestartHaystack = function(cb)
{
    NovaCommon.StopHaystack();

    // Note: the other honeyd may be shutting down still,
    // but the slight overlap doesn't cause problems
    NovaCommon.StartHaystack(false);

    cb && cb();
};

everyone.now.StartHaystack = function()
{
    if(!NovaCommon.nova.IsHaystackUp())
    {
        NovaCommon.StartHaystack(false);
    }

  setTimeout(function() {
  if(!NovaCommon.nova.IsHaystackUp())
  {
    everyone.now.HaystackStartFailed();
  }
  else
  {
    try 
    {
        everyone.now.updateHaystackStatus(NovaCommon.nova.IsHaystackUp())
    } 
    catch(err)
    {};
    }
    }, 1000);
};

everyone.now.StopHaystack = function()
{
    NovaCommon.StopHaystack();
    try 
    {
        everyone.now.updateHaystackStatus(NovaCommon.nova.IsHaystackUp());
    } 
    catch(err)
    {};
};

everyone.now.IsHaystackUp = function(cb)
{
    cb(NovaCommon.nova.IsHaystackUp());
};

everyone.now.IsNovadUp = function(cb)
{
    cb(NovaCommon.nova.IsNovadUp(false));
};

everyone.now.StartNovad = function()
{
    var result = NovaCommon.StartNovad(false);

    setTimeout(function() {
    result = NovaCommon.nova.CheckConnection();
    try 
    {
        everyone.now.updateNovadStatus(NovaCommon.nova.IsNovadUp(false));
    }
    catch(err){};
    }, 1000);
};

everyone.now.StopNovad = function(cb)
{
    if(NovaCommon.StopNovad() == false)
    {
        cb && cb('false');
    return;
    }
    NovaCommon.nova.CloseNovadConnection();
    try 
    {
        everyone.now.updateNovadStatus(NovaCommon.nova.IsNovadUp(false));
    }
    catch(err){};
};

everyone.now.HardStopNovad = function(passwd)
{
  NovaCommon.nova.HardStopNovad(passwd);
  NovaCommon.nova.CloseNovadConnection();
  try 
  {
    everyone.now.updateNovadStatus(NovaCommon.nova.IsNovadUp(false));
  }
  catch(err){};
};

everyone.now.sendAllSuspects = function (cb)
{
    NovaCommon.nova.CheckConnection();
    NovaCommon.nova.sendSuspectList(cb);
};

everyone.now.sendSuspect = function (ethinterface, ip, cb)
{
    var suspect = NovaCommon.nova.sendSuspect(ethinterface, ip);
    if (suspect.GetIdString === undefined)
    {
        console.log("Failed to get suspect");
        return;
    }
    var s = new Object();
    objCopy(suspect, s);
    cb(s);
};

// Deletes a honeyd node
everyone.now.deleteNodes = function (nodeNames, cb)
{
    var nodeName;
    for (var i = 0; i < nodeNames.length; i++)
    {
        nodeName = nodeNames[i];
        if (nodeName != null && !NovaCommon.honeydConfig.DeleteNode(nodeName))
        {
            cb(false, "Failed to delete node " + nodeName);
            return;
        }

    }

    if (!NovaCommon.honeydConfig.SaveAll())
    {
        cb(false, "Failed to save XML templates");
        return;
    }

    cb(true, "");
};

everyone.now.deleteProfiles = function (profileNames, cb)
{
    var profileName;
    for (var i = 0; i < profileNames.length; i++)
    {
        profileName = profileNames[i];

        if (!NovaCommon.honeydConfig.DeleteProfile(profileName))
        {
            cb(false, "Failed to delete profile " + profileName);
            return;
        }


        if (!NovaCommon.honeydConfig.SaveAll())
        {
            cb(false, "Failed to save XML templates");
            return;
        }
    }

    cb(true, "");
};

everyone.now.addWhitelistEntry = function (ethinterface, entry, cb)
{
    // TODO: Input validation. Should be IP address or 'IP/netmask'
    // Should also be sanitized for newlines/trailing whitespace
    if (NovaCommon.whitelistConfig.AddEntry(ethinterface + "," + entry))
    {
        cb(true, "");
    } else {
        cb(true, "Attempt to add whitelist entry failed");
    }
};

everyone.now.deleteWhitelistEntry = function (whitelistEntryNames, cb)
{
    var whitelistEntryName;
    for (var i = 0; i < whitelistEntryNames.length; i++)
    {
        whitelistEntryName = whitelistEntryNames[i];

        if (!NovaCommon.whitelistConfig.DeleteEntry(whitelistEntryName))
        {
            cb(false, "Failed to delete whitelistEntry " + whitelistEntryName);
            return;
        }
    }

    cb(true, "");
};

everyone.now.GetScript = function (scriptName, cb)
{
    var script = NovaCommon.honeydConfig.GetScript(scriptName);
    var methodlessScript = {};

    objCopy(script, methodlessScript);

    cb(methodlessScript);

};

everyone.now.GetVendors = function (profileName, cb)
{
    var profile = NovaCommon.honeydConfig.GetProfile(profileName);

    if (profile == null)
    {
        console.log("ERROR Getting profile " + profileName);
        cb(null);
        return;
    }


    var ethVendorList = [];

    var profVendors = profile.GetVendors();
    var profDists = profile.GetVendorCounts();

    for (var i = 0; i < profVendors.length; i++)
    {
        var element = {
            vendor: "",
            count: ""
        };
        element.vendor = profVendors[i];
        element.count = parseFloat(profDists[i]);
        ethVendorList.push(element);
    }

    cb(profVendors, profDists);
};

function jsProfileToHoneydProfile(profile)
{
    var honeydProfile = new NovaCommon.novaconfig.HoneydProfileBinding(profile.parentProfile, profile.name);
    
        //Set Ethernet vendors
    var ethVendors = [];
    var ethDists = [];

    for (var i in profile.ethernet)
    {
        ethVendors.push(profile.ethernet[i].vendor);
        ethDists.push(parseFloat(Number(profile.ethernet[i].count)));
    }
    honeydProfile.SetVendors(ethVendors, ethDists);
    

    // Move the Javascript object values to the C++ object
    honeydProfile.SetUptimeMin(Number(profile.uptimeMin));
    honeydProfile.SetUptimeMax(Number(profile.uptimeMax));
    honeydProfile.SetDropRate(Number(profile.dropRate));
    honeydProfile.SetPersonality(profile.personality);
    honeydProfile.SetCount(profile.count);

    honeydProfile.SetIsPersonalityInherited(Boolean(profile.isPersonalityInherited));
    honeydProfile.SetIsDropRateInherited(Boolean(profile.isDropRateInherited));
    honeydProfile.SetIsUptimeInherited(Boolean(profile.isUptimeInherited));


    // Add new ports
    honeydProfile.ClearPorts();
    var portName;
    for (var i = 0; i < profile.portSets.length; i++) 
    {
        //Make a new port set
        var encodedName = sanitizeCheck(profile.portSets[i].setName).entityEncode();
        honeydProfile.AddPortSet(encodedName);

        honeydProfile.SetPortSetBehavior(encodedName, "tcp", profile.portSets[i].TCPBehavior);
        honeydProfile.SetPortSetBehavior(encodedName, "udp", profile.portSets[i].UDPBehavior);
        honeydProfile.SetPortSetBehavior(encodedName, "icmp", profile.portSets[i].ICMPBehavior);

        for (var j = 0; j < profile.portSets[i].PortExceptions.length; j++)
        {
            var scriptConfigKeys = new Array();
            var scriptConfigValues = new Array();

            for (var key in profile.portSets[i].PortExceptions[j].scriptConfiguration)
            {
                scriptConfigKeys.push(key);
                scriptConfigValues.push(profile.portSets[i].PortExceptions[j].scriptConfiguration[key]);
            }

            honeydProfile.AddPort(encodedName,
                    profile.portSets[i].PortExceptions[j].behavior, 
                    profile.portSets[i].PortExceptions[j].protocol, 
                    Number(profile.portSets[i].PortExceptions[j].portNum), 
                    profile.portSets[i].PortExceptions[j].scriptName,
                    scriptConfigKeys,
                    scriptConfigValues);
        }
    }

    return honeydProfile;
}


//portSets = A 2D array. (array of portSets, which are arrays of Ports)
everyone.now.SaveProfile = function (profile, newProfile, cb)
{
    // Check input
    var profileNameRegexp = new RegExp("[a-zA-Z]+[a-zA-Z0-9 ]*");
    var match = profileNameRegexp.exec(profile.name);
    
    if (match == null) 
    {
        var err = "ERROR: Attempt to save a profile with an invalid name. Must be alphanumeric and not begin with a number.";
        cb(err);
        return;
    }

    // Check for duplicate profile
    if (newProfile) 
    {
        var existingProfile = NovaCommon.honeydConfig.GetProfile(profile.name);
        if (existingProfile != null)
	{
	    cb && cb("ERROR: Profile with name already exists");
	    return;
	}
    }


    // Check we have ethernet vendors
    if (profile.ethernet.length == 0)
    {
        var err = "ERROR: Must have at least one ethernet vendor!";
        cb && cb(err);
        return;
    }


    // Check for valid drop percentage
    if (isNaN(parseInt(profile.dropRate)))
    {
        cb && cb("ERROR: Can't convert drop rate to integer");
        return;
    }

    profile.dropRate = parseInt(profile.dropRate);

    if (profile.dropRate < 0 || profile.dropRate > 100)
    {
        cb && cb("ERROR: Droprate must be between 0 and 100");
        return;
    }

    // Check uptimes
    if (profile.uptimeValueMax < 0 || profile.uptimeValueMin < 0)
    {
        cb && cb("ERROR: Uptime must be a positive integer");
        return;
    }


    // Check that we have the scriptnames set for profiles that need scripts
    for (var i = 0; i < profile.portSets.length; i++) 
    {
        for (var j = 0; j < profile.portSets[i].PortExceptions.length; j++)
        {
            var port = profile.portSets[i].PortExceptions[j];


            if (isNaN(parseInt(port.portNum)))
            {
                cb && cb("ERROR: unable to parse port into an integer!");
                return;
            }


            if (parseInt(port.portNum) <= 0 || parseInt(port.portNum) > 65535) {
                cb && cb("ERROR: Unable to save profile with invalid port number!");
                return;
            }

            if (port.behavior == "script" || port.behavior == "tarpit script") {
                if (port.scriptName == "" || port.scriptName == "NA") {
                    var err = "ERROR: Attempt to save a profile with an invalid port script value.";
                    cb && cb(err);
                    return;
                }
            }

        }
    }


    var honeydProfile = jsProfileToHoneydProfile(profile);
    honeydProfile.Save();

    // Save the profile
    if (!NovaCommon.honeydConfig.SaveAll())
    {
        result = "Unable to save honeyd configuration";
    }

    cb();
};

everyone.now.RenamePortset = function(profile, oldName, newName, cb)
{
  var encodedName = sanitizeCheck(newName).entityEncode();
  var result = NovaCommon.honeydConfig.RenamePortset(oldName, encodedName, profile);
  NovaCommon.honeydConfig.SaveAll();
  if(typeof cb == 'function')
  {
    cb();
  }
};

everyone.now.WouldProfileSaveDeleteNodes = function (profile, cb)
{
    var honeydProfile = jsProfileToHoneydProfile(profile);

    cb(honeydProfile.WouldAddProfileCauseNodeDeletions());
};

everyone.now.ShowAutoConfig = function (nodeInterface, numNodesType, numNodes, subnets, groupName, append, cb, route)
{
    var executionString = 'haystackautoconfig';

    var hhconfigArgs = new Array();


    hhconfigArgs.push('--nodeinterface');
    hhconfigArgs.push(nodeInterface);

    if(numNodesType == "fixed") 
    {
        if(numNodes !== undefined) 
        {
            hhconfigArgs.push('-n');
            hhconfigArgs.push(numNodes);
        }
    } 
    else if(numNodesType == "ratio") 
    {
        if(numNodes !== undefined) 
        {
            hhconfigArgs.push('-r');
            hhconfigArgs.push(numNodes);
        }
    }
    else if(numNodesType == 'range')
    {
      if(numNodes !== undefined)
      {
        hhconfigArgs.push('e');
        hhconfigArgs.push(numNodes);
      }
    }
    
    if(subnets !== undefined && subnets.length > 0)
    {
        hhconfigArgs.push('-a');
        hhconfigArgs.push(subnets);
    }

    if (!append) {
        hhconfigArgs.push('-t');
        hhconfigArgs.push(groupName);
        NovaCommon.honeydConfig.AddConfiguration(groupName, 'false', '');
        NovaCommon.config.SetCurrentConfig(groupName);
    } else {
        hhconfigArgs.push('-t');
        hhconfigArgs.push(groupName);
    }

    var util = require('util');
    var spawn = require('child_process').spawn;
    
    console.log("Running: " + executionString.toString());
    console.log("Args: " + hhconfigArgs);

    autoconfig = spawn(executionString.toString(), hhconfigArgs);

    autoconfig.stdout.on('data', function (data)
    {
      if(typeof cb == 'function')
      {
          cb('' + data);
        }
    });

    autoconfig.stderr.on('data', function (data)
    {
        if (/^execvp\(\)/.test(data))
        {
            console.log("haystackautoconfig failed to start.");
            var response = "haystackautoconfig failed to start.";
            everyone.now.SwitchConfigurationTo('default');
            if(typeof route == 'function')
            {
              route("/autoConfig", response);
            }
        }
    });

    autoconfig.on('exit', function (code, signal)
    {
        console.log("autoconfig exited with code " + code);
        var response = "autoconfig exited with code " + code;
      if(typeof route == 'function' && signal != 'SIGTERM')
    {
      route("/honeydConfigManage", response);
    }
    if(signal == 'SIGTERM')
    {
      response = "autoconfig scan terminated early";
      route("/autoConfig", response);
    }
    });
};

everyone.now.CancelAutoScan = function(groupName)
{
  try
  {
    autoconfig.kill();
    autoconfig = undefined;
    everyone.now.RemoveConfiguration(groupName);
    
    everyone.now.SwitchConfigurationTo('default');
  }
  catch(e)
  {
    LOG("ERROR", "CancelAutoScan threw an error: " + e);
  }
};

everyone.now.WriteHoneydConfig = function(cb)
{
   NovaCommon.honeydConfig.WriteHoneydConfiguration(NovaCommon.config.GetPathConfigHoneydHS());
   
   if(typeof cb == 'function')
   {
     cb(); 
   }
};

everyone.now.GetConfigSummary = function(configName, cb)
{
  NovaCommon.honeydConfig.LoadAllTemplates();
  
  var scriptProfileBindings = NovaCommon.GetPorts();
  var profiles = NovaCommon.honeydConfig.GetProfileNames();
  var profileObj = {};
  
  for (var i = 0; i < profiles.length; i++) 
  {
    if(profiles[i] != undefined && profiles[i] != '')
    {
      var prof = NovaCommon.honeydConfig.GetProfile(profiles[i]);
      var obj = {};
      var vendorNames = prof.GetVendors();
      var vendorDist = prof.GetVendorCounts();
      
      obj.name = prof.GetName();
      obj.parent = prof.GetParentProfile();
      obj.os = prof.GetPersonality();
      obj.packetDrop = prof.GetDropRate();
      obj.vendors = [];
      
      for(var j = 0; j < vendorNames.length; j++)
      {
        var push = {};
        
        push.name = vendorNames[j];
        push.count = vendorDist[j];
        obj.vendors.push(push);
      }
      
      if(prof.GetUptimeMin() == prof.GetUptimeMax())
      {
        obj.fixedOrRange = 'fixed';
        obj.uptimeValue = prof.GetUptimeMin();
      }
      else
      {
        obj.fixedOrRange = 'range';
        obj.uptimeValueMin = prof.GetUptimeMin();
        obj.uptimeValueMax = prof.GetUptimeMax();        
      }
      
      //obj.defaultTCP = prof.GetTcpAction();
      //obj.defaultUDP = prof.GetUdpAction();
      //obj.defaultICMP = prof.GetIcmpAction();
      profileObj[profiles[i]] = obj;
    }
  }
  
  var nodeNames = NovaCommon.honeydConfig.GetNodeMACs();
  var nodeList = [];
  
  for (var i = 0; i < nodeNames.length; i++)
  {
    var node = NovaCommon.honeydConfig.GetNode(nodeNames[i]);
    var push = NovaCommon.cNodeToJs(node);
    nodeList.push(push);
  }
  
  nodeNames = null;
  
  if(typeof cb == 'function')
  {
    cb(scriptProfileBindings, profileObj, profiles, nodeList);
  }
};

everyone.now.SwitchConfigurationTo = function(configName)
{
    NovaCommon.honeydConfig.SwitchConfiguration(configName); 
    NovaCommon.config.WriteSetting('CURRENT_CONFIG', configName);
};

everyone.now.RemoveConfiguration = function(configName, cb)
{
  if(configName == 'default')
  {
    console.log('Cannot delete default haystack');
  }
  
  NovaCommon.honeydConfig.RemoveConfiguration(configName);
  
  if(typeof cb == 'function')
  {
    cb(configName);
  }
};

everyone.now.RemoveScript = function(scriptName, cb)
{
  NovaCommon.honeydConfig.RemoveScript(scriptName);
  
  NovaCommon.honeydConfig.SaveAllTemplates();
  
  if(typeof cb == 'function')
  {
    cb();
  }
};

everyone.now.GetLocalIP = function (iface, cb)
{
    cb(NovaCommon.nova.GetLocalIP(iface));
};

everyone.now.GetSubnetFromInterface = function (iface, index, cb)
{
  cb(iface, NovaCommon.nova.GetSubnetFromInterface(iface), index);
};

everyone.now.RemoveScriptFromProfiles = function(script, cb)
{
    NovaCommon.honeydConfig.DeleteScriptFromPorts(script);

    NovaCommon.honeydConfig.SaveAllTemplates();

    if(typeof cb == 'function')
    {
        cb();
    }
};

everyone.now.GenerateMACForVendor = function(vendor, cb)
{
    cb(NovaCommon.honeydConfig.GenerateRandomUnusedMAC(vendor));
};

everyone.now.restoreDefaultSettings = function(cb)
{
    var source = NovaSharedPath + "/../userFiles/config/NOVAConfig.txt";
    var destination = NovaHomePath + "/config/NOVAConfig.txt";
    exec('cp -f ' + source + ' ' + destination, function(err)
    {
        cb();
    }); 
};

everyone.now.reverseDNS = function(ip, cb)
{
    dns.reverse(ip, cb);
};

everyone.now.addTrainingPoint = function(ip, ethinterface, features, hostility, cb)
{
    if (hostility != '0' && hostility != '1')
    {
        cb("Error: Invalid hostility. Should be 0 or 1");
        return;
    }
    
    if (features.toString().split(" ").length != NovaCommon.nova.GetDIM()) {
        cb("Error: Invalid number of features!")
        return;
    }

    var point = features.toString() + " " + hostility + "\n";
    fs.appendFile(NovaHomePath + "/config/training/data.txt", point, function(err)
    {
        if (err)
        {
            console.log("Error: " + err);
            cb(err);
            return;
        }

        var d = new Date();
        var trainingDbString = "";
        trainingDbString += hostility + ' "User customized training point for suspect ' + ip + " added on " + d.toString() + '"\n';
        trainingDbString += "\t" + features.toString();
        trainingDbString += "\n\n";

        fs.appendFile(NovaHomePath + "/config/training/training.db", trainingDbString, function(err)
        {
            if (!NovaCommon.nova.ReclassifyAllSuspects())
            {
                cb("Error: Unable to reclassify suspects with new training data");
                return;
            }
            cb();
        });

    }); 
};

everyone.now.GetHaystackDHCPStatus = function(cb)
{
    fs.readFile("/var/log/honeyd/ipList", 'utf8', function (err, data)
    {
        var DHCPIps = new Array();
        if (err)
        {
            RenderError(res, "Unable to open Honeyd status file for reading due to error: " + err);
            return;
        } else {

            data = data.toString().split("\n");
            for(var i = 0; i < data.length; i++)
            {
                if (data[i] == "") {continue};
                var entry = {
                    ip: data[i].toString().split(",")[0],
                    mac: data[i].toString().split(",")[1]
                };
                DHCPIps.push(entry);
            }   

            cb(DHCPIps);
        }
    });
};

everyone.now.deleteClassifier = function(index, cb)
{
    NovaCommon.classifiers.deleteClassifier(index);
    if (cb) cb();
};

everyone.now.saveClassifier = function(classifier, index, cb)
{
    // Convert the model instance settings to strings for config file
    var enabledFeaturesString = "";
    var weightString = "";
    var thresholdString = "";

    for (var i = 0; i < classifier.features.length; i++)
    {
        if (classifier.features[i].enabled)
        {
            enabledFeaturesString += "1";
        }
        else
        {
            enabledFeaturesString += "0";
        }

        if (classifier.type == "KNN")
        {
            weightString += String(classifier.features[i].weight) + " ";
        }
        else if (classifier.type == "THRESHOLD_TRIGGER")
        {
            thresholdString += classifier.features[i].threshold + " ";
        }
    }
    
    classifier.strings = {};
    classifier.strings["ENABLED_FEATURES"] = enabledFeaturesString;
    if (classifier.type == "KNN")
    {
        classifier.strings["FEATURE_WEIGHTS"] = weightString;
    }
    else if (classifier.type == "THRESHOLD_TRIGGER")
    {
        classifier.strings["THRESHOLD_HOSTILE_TRIGGERS"] = thresholdString;
    }

    NovaCommon.classifiers.saveClassifier(classifier, index);
    if (cb) cb();
};

everyone.now.GetProfile = function (profileName, cb)
{
    var profile = NovaCommon.honeydConfig.GetProfile(profileName);

    
    if (profile == null)
    {
        cb(null);
        return;
    }

    // Nowjs can't pass the object with methods, they need to be member vars
    profile.name = profile.GetName();
    profile.personality = profile.GetPersonality();

    profile.uptimeMin = profile.GetUptimeMin();
    profile.uptimeMax = profile.GetUptimeMax();
    profile.dropRate = profile.GetDropRate();
    profile.parentProfile = profile.GetParentProfile();

    profile.isPersonalityInherited = profile.IsPersonalityInherited();
    profile.isUptimeInherited = profile.IsUptimeInherited();
    profile.isDropRateInherited = profile.IsDropRateInherited();

    profile.count = profile.GetCount();
    profile.portSets = GetPortSets(profileName);


    var ethVendorList = [];

    var profVendors = profile.GetVendors();
    var profCounts = profile.GetVendorCounts();

    for (var i = 0; i < profVendors.length; i++)
    {
        var element = {};
        element.vendor = profVendors[i];
        element.count = profCounts[i];
        ethVendorList.push(element);
    }

    profile.ethernet = ethVendorList;


    cb(profile);
};


var GetPortSets = function (profileName, cb)
{
    var portSetNames = NovaCommon.honeydConfig.GetPortSetNames(profileName);
    
    var portSets = [];  

    for (var i = 0; i < portSetNames.length; i++)
    {
        var portSet = NovaCommon.honeydConfig.GetPortSet( profileName, portSetNames[i] );
        portSet.setName = portSet.GetName();
        portSet.TCPBehavior = portSet.GetTCPBehavior();
        portSet.UDPBehavior = portSet.GetUDPBehavior();
        portSet.ICMPBehavior = portSet.GetICMPBehavior();

        portSet.PortExceptions = portSet.GetPorts();
        for (var j = 0; j < portSet.PortExceptions.length; j++)
        {
            portSet.PortExceptions[j].portNum = portSet.PortExceptions[j].GetPortNum();
            portSet.PortExceptions[j].protocol = portSet.PortExceptions[j].GetProtocol();
            portSet.PortExceptions[j].behavior = portSet.PortExceptions[j].GetBehavior();
            portSet.PortExceptions[j].scriptName = portSet.PortExceptions[j].GetScriptName();
            portSet.PortExceptions[j].scriptConfiguration = portSet.PortExceptions[j].GetScriptConfiguration();
        }
        portSets.push(portSet);
    }

    if(typeof cb == 'function')
    {
    cb(portSets, profileName);
  }
  return portSets;
};
everyone.now.GetPortSets = GetPortSets;


}


module.exports = NowjsMethods;
