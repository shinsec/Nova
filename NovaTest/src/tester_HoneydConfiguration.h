//============================================================================
// Name        : tester_HoneydConfiguration.h
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
// Description : This file contains unit tests for the class HoneydConfiguration
//============================================================================

#include "gtest/gtest.h"
#include "HoneydConfiguration/HoneydConfiguration.h"

using namespace Nova;

// The test fixture for testing class HoneydConfiguration.
class HoneydConfigurationTest : public ::testing::Test
{

protected:

	// Objects declared here can be used by all tests in the test case
	HoneydConfiguration * m_config;

	HoneydConfigurationTest()
	{
		m_config = new HoneydConfiguration();
	}

	~HoneydConfigurationTest()
	{
		delete m_config;
	}

	// If the constructor and destructor are not enough for setting up
	// and cleaning up each test, you can define the following methods:
	void SetUp()
	{
		EXPECT_TRUE(m_config != NULL);
		EXPECT_TRUE(m_config->LoadAllTemplates());
	}

	void TearDown()
	{

	}
};

// Tests go here. Multiple small tests are better than one large test, as each test
// will get a pass/fail and debugging information associated with it.

TEST_F(HoneydConfigurationTest, test_getMaskBits)
{
	EXPECT_EQ(0, m_config->GetMaskBits(0));
	EXPECT_EQ(16, m_config->GetMaskBits(~0 - 65535));
	EXPECT_EQ(24, m_config->GetMaskBits(~0 - 255));
	EXPECT_EQ(31, m_config->GetMaskBits(~0 -1));
	EXPECT_EQ(32, m_config->GetMaskBits(~0));
	EXPECT_EQ(-1, m_config->GetMaskBits(240));
}

TEST_F(HoneydConfigurationTest, test_Port)
{
	stringstream ss;
	vector<string> expectedPorts;
	ss.str("");
	EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(0, TCP, OPEN, ""))));
	ss.str("1_TCP_open");
	expectedPorts.push_back(ss.str());
	EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(1, TCP, OPEN, ""))));
	ss.str("65535_UDP_block");
	expectedPorts.push_back(ss.str());
	EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(~0, UDP, BLOCK, ""))));
	ss.str("65535_TCP_reset");
	expectedPorts.push_back(ss.str());
	EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(~0, TCP, RESET, ""))));
	ss.str("65534_TCP_block");
	expectedPorts.push_back(ss.str());
	EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(65534, TCP, BLOCK, ""))));

	std::vector<std::string> scriptNames;
	EXPECT_NO_FATAL_FAILURE(scriptNames = m_config->GetScriptNames());
	uint i = 2;
	while(!scriptNames.empty())
	{
		ss.str("");
		ss << i << "_TCP_" << scriptNames.back();
		EXPECT_TRUE(!(ss.str().compare(m_config->AddPort(i, TCP, SCRIPT, scriptNames.back()))));
		expectedPorts.push_back(ss.str());
		scriptNames.pop_back();
		i++;
	}
	while(!expectedPorts.empty())
	{
		EXPECT_TRUE(m_config->GetPort(expectedPorts.back()) != NULL);
		expectedPorts.pop_back();
	}
}

TEST_F(HoneydConfigurationTest, test_RenameProfile)
{
	//Create dummy profile
	NodeProfile * p = new NodeProfile();
	p->m_name = "TestProfile";
	p->SetEthernet("Dell");
	p->m_icmpAction = "block";
	p->m_parentProfile = "default";
	p->m_udpAction = "block";
	p->m_tcpAction = "reset";
	p->m_uptimeMax = "100";
	p->m_uptimeMin = "10";

	// Delete the profile if it already exists
	m_config->DeleteProfile("TestProfile-renamed");
	m_config->DeleteProfile("TestProfile");

	//Test adding a profile
	EXPECT_TRUE(m_config->AddProfile(p));
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile") != m_config->m_profiles.end());

	//Test renaming a profile
	EXPECT_TRUE(m_config->RenameProfile("TestProfile", "TestProfile-renamed"));

	m_config->UpdateAllProfiles();

	// Save and delete object
	EXPECT_TRUE(m_config->SaveAllTemplates());
	delete m_config;

	// Reload from the config file
	m_config = new HoneydConfiguration();
	EXPECT_TRUE(m_config != NULL);
	EXPECT_TRUE(m_config->LoadAllTemplates());

	EXPECT_TRUE(m_config->m_profiles.find("TestProfile-renamed") != m_config->m_profiles.end());
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile") == m_config->m_profiles.end());


	EXPECT_TRUE(m_config->DeleteProfile("TestProfile-renamed"));
	EXPECT_TRUE(m_config->SaveAllTemplates());
}

TEST_F(HoneydConfigurationTest, test_errorCases)
{
	EXPECT_FALSE(m_config->DeleteProfile(""));
	EXPECT_FALSE(m_config->DeleteProfile("aoeustnhaoesnuhaosenuht"));
	EXPECT_FALSE(m_config->DeleteNode(""));
	EXPECT_FALSE(m_config->DeleteNode("aoeuhaonsehuaonsehu"));
	EXPECT_EQ(NULL, m_config->GetProfile(""));
	EXPECT_EQ(NULL, m_config->GetProfile("aouhaosnuheaonstuh"));
	EXPECT_EQ(NULL, m_config->GetNode(""));
	EXPECT_EQ(NULL, m_config->GetNode("aouhaosnuheaonstuh"));

}

TEST_F(HoneydConfigurationTest, test_Profile)
{
	//Create dummy profile
	NodeProfile * p = new NodeProfile();
	p->m_name = "TestProfile";
	p->SetEthernet("Dell");
	p->m_icmpAction = "block";
	p->m_parentProfile = "default";
	p->m_udpAction = "block";
	p->m_tcpAction = "reset";
	p->m_uptimeMax = "100";
	p->m_uptimeMin = "10";

	bool dmEn = Config::Inst()->GetIsDmEnabled();
	Config::Inst()->SetIsDmEnabled(false);

	// Delete the profile if it already exists
	m_config->DeleteProfile("TestProfile-renamed");
	m_config->DeleteProfile("TestProfile");

	//Test adding a profile
	EXPECT_TRUE(m_config->AddProfile(p));
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile") != m_config->m_profiles.end());

	//Modify the test profile to add a second one.
	p->m_parentProfile = "TestProfile";
	p->m_name = "TestProfile2";
	EXPECT_TRUE(m_config->AddProfile(p));
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile2") != m_config->m_profiles.end());

	//Test renaming a profile
	EXPECT_TRUE(m_config->RenameProfile("TestProfile2", "TestProfile-2"));
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile2") == m_config->m_profiles.end());
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile-2") != m_config->m_profiles.end());

	//Test Inheriting of a profile
	EXPECT_TRUE((m_config->m_profiles.find("TestProfile-2")->second.m_parentProfile.compare("default")));
	EXPECT_TRUE(!(m_config->m_profiles.find("TestProfile-2")->second.m_parentProfile.compare("TestProfile")));
	EXPECT_TRUE(m_config->InheritProfile("TestProfile-2", "default"));
	EXPECT_TRUE(!(m_config->m_profiles.find("TestProfile-2")->second.m_parentProfile.compare("default")));
	EXPECT_TRUE((m_config->m_profiles.find("TestProfile-2")->second.m_parentProfile.compare("TestProfile")));

	//Test deleting a profile
	EXPECT_TRUE(m_config->DeleteProfile("TestProfile"));
	EXPECT_TRUE(m_config->m_profiles.find("TestProfile") == m_config->m_profiles.end());

	Config::Inst()->SetIsDmEnabled(dmEn);
}


TEST_F(HoneydConfigurationTest, test_NewProfileSaving)
{
	EXPECT_TRUE(m_config->AddPort(1, (portProtocol)1, (portBehavior)0, "NA") == "1_TCP_block");
	EXPECT_TRUE(m_config->AddPort(2, (portProtocol)1, (portBehavior)1, "NA") == "2_TCP_reset");
	EXPECT_TRUE(m_config->AddPort(3, (portProtocol)1, (portBehavior)2, "NA") == "3_TCP_open");

	NodeProfile * p = new NodeProfile();
	for (int i = 0; i < INHERITED_MAX; i++)
	{
		p->m_inherited[i] = true;
	}
	p->m_name = "test";
	p->m_parentProfile = "default";
	p->AddPort("1_TCP_block", false, 100);
	p->AddPort("2_TCP_reset", false, 100);
	p->AddPort("3_TCP_open", false, 100);

	EXPECT_TRUE(m_config->AddProfile(p));
	//EXPECT_TRUE(m_config->SaveAllTemplates());
}

TEST_F(HoneydConfigurationTest, test_profileDeletion)
{
	NodeProfile *parent = new NodeProfile();
	parent->SetName("parent");
	parent->SetParentProfile("default");
	EXPECT_TRUE(m_config->AddProfile(parent));

	m_config->UpdateAllProfiles();

	NodeProfile *child = new NodeProfile();
	child->SetName("child");
	child->SetParentProfile("parent");
	EXPECT_TRUE(m_config->AddProfile(child));

	// Save, reload
	EXPECT_TRUE(m_config->SaveAllTemplates());
	EXPECT_TRUE(m_config->LoadAllTemplates());

	// Delete the child and make sure it worked after a reload
	EXPECT_TRUE(m_config->DeleteProfile("child"));
	EXPECT_TRUE(m_config->SaveAllTemplates());
	EXPECT_TRUE(m_config->LoadAllTemplates());
	EXPECT_TRUE(m_config->m_profiles.find("child") == m_config->m_profiles.end());

	// Delete the parent and make sure it worked after a reload
	EXPECT_TRUE(m_config->DeleteProfile("parent"));
	EXPECT_TRUE(m_config->SaveAllTemplates());
	EXPECT_TRUE(m_config->LoadAllTemplates());
	EXPECT_TRUE(m_config->m_profiles.find("parent") == m_config->m_profiles.end());
}
