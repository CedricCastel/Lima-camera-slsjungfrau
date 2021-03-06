//###########################################################################
// This file is part of LImA, a Library for Image Acquisition
//
// Copyright (C) : 2009-2018
// European Synchrotron Radiation Facility
// BP 220, Grenoble 38043
// FRANCE
//
// This is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//###########################################################################

/*************************************************************************************/
/*! 
 *  \file   SlsJungfrauCameraReceivers.h
 *  \brief  SlsJungfrau detector acquisition receivers controller class implementation 
 *  \author C�dric Castel - SOLEIL (MEDIANE SYSTEME - IT consultant) 
*/
/*************************************************************************************/

#include <cstdlib>
#include <fstream> // for std::ifstream
#include <algorithm>

#include "lima/Exceptions.h"
#include "lima/RegExUtils.h"

#include "SlsJungfrauCameraReceivers.h"
#include "SlsJungfrauCamera.h"

#include <sls_receiver_defs.h>
#include <slsReceiverUsers.h>

using namespace lima;
using namespace lima::SlsJungfrau;


// activate this define to trace the sdk callbacks 
//#define CAMERA_RECEIVERS_ACTIVATE_SDK_CALLBACK_TRACE

/************************************************************************
 * \brief constructor
 ************************************************************************/
CameraReceivers::CameraReceivers(Camera & cam) : m_cam(cam)
{
    DEB_MEMBER_FUNCT();
    DEB_TRACE() << "CameraReceivers::CameraReceivers - BEGIN";

    m_receivers    = NULL;
    m_nb_receivers = 0   ;

    DEB_TRACE() << "CameraReceivers::CameraReceivers - END";
}

/************************************************************************
 * \brief destructor
 ************************************************************************/
CameraReceivers::~CameraReceivers()
{
    DEB_DESTRUCTOR();

    DEB_TRACE() << "CameraReceivers::~CameraReceivers - BEGIN";

    // c-array which contains elements used as user data in sls sdk callbacks
    if(m_receivers != NULL)
    {
        delete [] m_receivers;
        m_receivers = NULL;
    }

    DEB_TRACE() << "CameraReceivers::~CameraReceivers - END";
}

/************************************************************************
 * \brief inits the receivers using the configuration file name
 * \param in_config_file_name complete path to the configuration file
 ************************************************************************/
void CameraReceivers::init(const std::string & in_config_file_name)
{
    DEB_MEMBER_FUNCT();

    std::vector<CameraReceiver> receivers;

    int receiver_index;

    // preparing the args for receivers creation
    char        temp_port[10];
    const int   argc         = 3;
    char      * args[argc]   = {(char*)"slsReceiver", (char*)"--rx_tcpport", temp_port};

    // parsing the config file to build the receivers informations container
    createReceivers(in_config_file_name, receivers);

    //------------------------------------------------------------------------------------
    // creating the receivers instances 
    for(receiver_index = 0 ; receiver_index < receivers.size() ; receiver_index++)
    {
    	int ret = slsReceiverDefs::OK;

        // changing the udp port in the args
        sprintf(temp_port, "%d", receivers[receiver_index].getTcpipPort());

        // creating the receiver using the args
        DEB_TRACE() << "creating the receiver "
                    << "(" << receiver_index << ") "
                    << receivers[receiver_index].getHostName() << " - "
                    << "tcpip port (" << receivers[receiver_index].getTcpipPort() << ")";

        slsReceiverUsers * receiver = new slsReceiverUsers(argc, args, ret);

        // managing a failed result
        if(ret == slsReceiverDefs::FAIL)
        {
            // free the memory
            delete receiver;

            THROW_HW_FATAL(ErrorType::Error) << "CameraReceivers::init failed! " 
                                             << "Could not create the sls receiver (" 
                                             << receiver_index << ")";
        }
        else
        {
            DEB_TRACE() << "receiver created - version (" << receiver->getReceiverVersion() << ")";

            // in case of success, we set the receiver in the receivers informations container
            receivers[receiver_index].setReceiver     (receiver);
            receivers[receiver_index].setReceiverIndex(receiver_index);
            receivers[receiver_index].setReceivers    (this);
        }
    }

    //------------------------------------------------------------------------------------
    // allocating and initing the c-array for using user data in the sls sdk callbacks
    m_receivers    = new CameraReceiver[receivers.size()];
    m_nb_receivers = receivers.size();
    
    // making cases copy
    for(receiver_index = 0 ; receiver_index < m_nb_receivers ; receiver_index++)
    {
        m_receivers[receiver_index] = receivers[receiver_index];
        receivers[receiver_index].setReceiver(NULL);
    }

    receivers.clear(); // no need to keep the container content

    // setting the callbacks for the receivers
    for(receiver_index = 0 ; receiver_index < m_nb_receivers ; receiver_index++)
    {
        slsReceiverUsers * receiver = m_receivers[receiver_index].getReceiver(); // alias

        DEB_TRACE() << "registering the receiver " << "(" << receiver_index << ") callbacks:";

        // CameraReceiver ptr is given to the register methods for access to user data
        CameraReceiver * user_data = m_receivers + receiver_index;

        // callback for start acquisition
        DEB_TRACE() << "registering StartAcq()";
        receiver->registerCallBackStartAcquisition(startedAcquisitionCallBack, user_data); 

        // callback for finished acquisition
        DEB_TRACE() << "registering finishedAcquisition()";
        receiver->registerCallBackAcquisitionFinished(finishedAcquisitionCallBack, user_data);

        // callback for raw data */
        DEB_TRACE() << "registering GetData()";
        receiver->registerCallBackRawDataReady(acquisitionDataReadyCallBack, user_data);
    }

    //------------------------------------------------------------------------------------
    // starting the receivers
    for(receiver_index = 0 ; receiver_index < m_nb_receivers ; receiver_index++)
    {
        slsReceiverUsers * receiver = m_receivers[receiver_index].getReceiver(); // alias

        if (receiver->start() == slsReceiverDefs::FAIL)
        {
            // free the memory
            delete receiver;
            m_receivers[receiver_index].setReceiver(NULL);

            THROW_HW_FATAL(ErrorType::Error) << "CameraReceivers::init failed! " 
                                             << "Could not start the sls receiver (" 
                                             << receiver_index << ")";
        }
    }
}

//==================================================================
// internal callbacks management
//==================================================================
/************************************************************************
 * \brief Started acquisition management
 * \param m_receiver_index receiver index
 ************************************************************************/
void CameraReceivers::startedAcquisition(int in_receiver_index)
{
    DEB_MEMBER_FUNCT();
    DEB_TRACE() << "CameraReceivers::startedAcquisition";
}

/************************************************************************
 * \brief Finished acquisition management
 * \param m_receiver_index receiver index
 * \param in_frames_nb Number of frames caught
 ************************************************************************/
void CameraReceivers::finishedAcquisition(int      in_receiver_index,
                                          uint64_t in_frames_nb     )
{
    DEB_MEMBER_FUNCT();
    DEB_TRACE() << "CameraReceivers::finishedAcquisition";
}

/************************************************************************
 * \brief Acquisition data management
 * \param m_receiver_index receiver index
 * \param in_frame_index frame index (starts at 1)
 * \param in_packet_number number of packets caught for this frame
 * \param in_timestamp time stamp in 10MHz clock
 * \param in_data_pointer frame image pointer
 * \param in_data_size frame image size 
 ************************************************************************/
void CameraReceivers::acquisitionDataReady(const int      in_receiver_index,
                                           uint64_t       in_frame_index   ,
                                           const uint32_t in_packet_number ,
                                           const uint64_t in_timestamp     ,
                                           const char *   in_data_pointer  ,
                                           const uint32_t in_data_size     )
{
    DEB_MEMBER_FUNCT();

    // the camera will manage the new frame
    m_cam.acquisitionDataReady(in_receiver_index, 
                               in_frame_index   ,
                               in_packet_number ,
                               in_timestamp     ,
                               in_data_pointer  ,
                               in_data_size     );
}

//==================================================================
// configuration file parsing
//==================================================================
/************************************************************************
 * \brief creates the receivers container using the configuration file
 * \param in_config_file_name complete path to the configuration file
 * \param out_receivers       receivers container result
 ************************************************************************/
void CameraReceivers::createReceivers(const std::string           & in_config_file_name,
                                      std::vector<CameraReceiver> & out_receivers      )
{
    DEB_MEMBER_FUNCT();

    //-------------------------------------------------------------------------
    // regex used in this method :
    //
    // we need to duplicate anti-slash chars in a c string: \s -> \\s
    //-------------------------------------------------------------------------
    //
    // regex for hostname label :
    // the hostname label starts at the beginning of a line: ^
    // there is a space char after the hostname string: \s
    // the search result is all hostnames and is under parentheses.
    const std::string regex_hostname_label = "^hostname\\s([A-Za-z0-9\\+]+)$";

    // regex for hostname value :
    // a value can ends with a '+' char or an end of line
    // the search result is each hostname and is under parentheses.
    const std::string regex_hostname_value = "([A-Za-z0-9]+)[\\+|$]*";

    // regex for rx_tcpport index :
    // the search result is the controller index and is under parentheses.
    const std::string regex_tcpport_index = "^([0-9]+):rx_tcpport\\s[0-9]+$";
    
    // regex for rx_tcpport port :
    // the search result is the tecpip port and is under parentheses.
    const std::string regex_tcpport_port = "^[0-9]+:rx_tcpport\\s([0-9]+)$";

    //-------------------------------------------------------------------------
    lima::RegEx re;
    lima::RegEx::MatchListType match_list; // for multiple results
    lima::RegEx::FullMatchType full_match; // for single result

    // opening the config file in read only text mode
    std::ifstream file(in_config_file_name.c_str(), std::ifstream::in);
    std::string   line;

    DEB_TRACE() << "createReceivers - starting to parse config file : " << in_config_file_name;

    // parsing the file
    while (std::getline(file, line))
    {
        // we need to remove leading, trailing and extra spaces to facilitate the next steps
        line = trimString(line);

        // we are searching a line which contains hostname to get the controllers list
        re = regex_hostname_label;

		if (re.match(line, full_match)) 
        {
            DEB_TRACE() << "config matching line found : " << line;

            // we are searching the hostnames values
            std::string values = std::string(full_match[1]);

            re = regex_hostname_value;
            re.multiSearch(values, match_list);

		    if (!match_list.empty())
            {
			    for (size_t receiver_index = 0 ; receiver_index < match_list.size(); receiver_index++)
                {
                    // we set the data in the receiver info container
                    manageReceiversResize(receiver_index, out_receivers);
                    out_receivers[receiver_index].setHostName(std::string(match_list[receiver_index][1]));

                    DEB_TRACE() << "hostname (" 
                                << receiver_index << "):" 
                                << out_receivers[receiver_index].getHostName();
                }

                // no need to check other option, we jump to next line
                continue;
            }
            else
            {
                THROW_HW_FATAL(ErrorType::InvalidValue) 
                    << "createReceivers failed! Could not initialize the camera!";
            }
        }

        // communication port between client and receiver and controller index
        re = regex_tcpport_index;

	    if (re.match(line, full_match)) 
        {
            std::string index = std::string(full_match[1]);
            int         receiver_index;
            
            DEB_TRACE() << "config matching line found : " << line;
            DEB_TRACE() << "receiver index : " << index;

            // conversion of string to int to get the controller index
            receiver_index = convertStringToInteger(index, "controller id");

            // getting the tcpip port 
            re = regex_tcpport_port;

	        if (re.match(line, full_match)) 
            {
                std::string tcpip_port = std::string(full_match[1]);
                int         receiver_tcpip_port;

                DEB_TRACE() << "receiver tcpip port : " << tcpip_port;

                // conversion of string to int to get the controller index
                receiver_tcpip_port = convertStringToInteger(tcpip_port, "controller tcpip port");

                // we set the data in the receiver info container
                manageReceiversResize(receiver_index, out_receivers);
                out_receivers[receiver_index].setTcpipPort(receiver_tcpip_port);
            }

            // no need to check other option, we jump to next line
            continue;
        }
    }

    // check if there is at least one set receiver
    if (out_receivers.empty())
    {
        THROW_HW_FATAL(ErrorType::Error) 
            << "readConfigurationFile failed! Please set correctly the hostname.";
    }
    else
    {
        // logging the result
        DEB_TRACE() << "Receivers list";

	    for (size_t receiver_index = 0 ; receiver_index < out_receivers.size(); receiver_index++)
        {
            DEB_TRACE() << "hostname ("   << out_receivers[receiver_index].getHostName()  << ")"
                        << " - "
                        << "tcpip port (" << out_receivers[receiver_index].getTcpipPort() << ")"; 
        }
    }

    DEB_TRACE() << "createReceivers - end of parsing";
}

/************************************************************************
 * \brief removes leading, trailing and extra spaces
 * \param in_string string which will be processed
 * \return trimmed string
 ************************************************************************/
std::string CameraReceivers::trimString(const std::string & in_string) const
{
    std::string temp  = in_string;
    std::string result;

    // in case of empty string, returning immediately
    if (temp.empty())
        return temp;

    // replacing tabulations by spaces
    std::replace(temp.begin(), temp.end(), '\t', ' ');

    // removing leading white spaces
    // is the first char a white space ?
    while (!temp.empty() && std::isspace(temp[0])) 
        temp.erase(temp.begin()); // erasing the first char

    // removing trailing white spaces
    // is the last char a white space ?
    while (!temp.empty() && std::isspace(temp[temp.size() - 1]))
        temp.erase(temp.end()-1); // erasing the last char

    // removing extra spaces
    std::size_t char_index          = 0;
    bool        is_previous_a_space = false;

    while(char_index < temp.size())
    {
        // if the current char is not a space or the previous was not a space
        if(!std::isspace(temp[char_index]) || (!is_previous_a_space))
        {
            result += temp[char_index];
        }

        is_previous_a_space = std::isspace(temp[char_index]);
        char_index++;
    }

    return result;
}

/************************************************************************
 * \brief converts a string to an integer and checks the value.
 *        Throws an exception in case of conversion error.
 * \param in_value string value to be converted
 * \param in_label label used for log purpose
 * \return converted integer value
 ************************************************************************/
int CameraReceivers::convertStringToInteger(const std::string & in_value,
                                            const std::string & in_label) const
{
    DEB_MEMBER_FUNCT();

    std::istringstream ss(in_value);
    int                result;
    
    // conversion of string to int
    ss >> result;

    if (result < 0)
    {
        THROW_HW_FATAL(ErrorType::InvalidValue)
            << "readConfigurationFile failed! " 
            << in_label << " (" << in_value << ") is invalid!";
    }

    return result;
}

/************************************************************************
 * \brief automatically resizes the receivers container 
 *        because elements can be inserted in random order 
 * \param in_element_index index of the element we need to set
 * \param in_out_receivers receivers container
 ************************************************************************/
void CameraReceivers::manageReceiversResize(const size_t                & in_element_index,
                                            std::vector<CameraReceiver> & in_out_receivers)
{
    if(in_element_index >= in_out_receivers.size())
    {
        in_out_receivers.resize(in_element_index + 1);
    }
}

//==================================================================
// sls sdk callbacks
//==================================================================
// Defines colors to print data call back in different colors for different recievers
#define PRINT_IN_COLOR(c,f, ...) 	printf ("\033[%dm" f RESET, 30 + c+1, ##__VA_ARGS__)

/************************************************************************
 * \brief Start Acquisition Call back
 *        slsReceiver writes data if file write enabled.
 *        Users get data to write using call back if 
 *        registerCallBackRawDataReady is registered.
 * \param in_file_path file path
 * \param in_file_name file name
 * \param in_file_index file index
 * \param in_data_size data size in bytes
 * \param in_user_data pointer to user data object
 * \return ignored
 ************************************************************************/
int CameraReceivers::startedAcquisitionCallBack(char     * in_file_path ,
                                                char     * in_file_name ,
                                                uint64_t   in_file_index,
                                                uint32_t   in_data_size ,
                                                void     * in_user_data )
{
#ifdef CAMERA_RECEIVERS_ACTIVATE_SDK_CALLBACK_TRACE
	cprintf(BLUE, "#### startedAcquisitionCallBack:  filepath:%s  filename:%s fileindex:%llu  datasize:%u ####\n",
			in_file_path, in_file_name, in_file_index, in_data_size);
#endif

    // we call the internal management of the callback using the user data
    // the user data allows to have access to the 
    // CameraReceivers instance smart pointer and to the receiver index.
    CameraReceiver * user_data = static_cast<CameraReceiver *>(in_user_data);
    user_data->getReceivers()->startedAcquisition(user_data->getReceiverIndex());

	return 0;
}

/************************************************************************
 * \brief Acquisition Finished Call back
 * \param in_frames_nb Number of frames caught
 * \param in_user_data pointer to user data object
 ************************************************************************/
void CameraReceivers::finishedAcquisitionCallBack(uint64_t   in_frames_nb,
                                                  void     * in_user_data)
{
#ifdef CAMERA_RECEIVERS_ACTIVATE_SDK_CALLBACK_TRACE
	cprintf(BLUE, "#### finishedAcquisition: frames:%llu ####\n",in_frames_nb);
#endif

    // we call the internal management of the callback using the user data
    // the user data allows to have access to the 
    // CameraReceivers instance smart pointer and to the receiver index.
    CameraReceiver * user_data = static_cast<CameraReceiver *>(in_user_data);
    user_data->getReceivers()->finishedAcquisition(user_data->getReceiverIndex(), in_frames_nb);
}

/************************************************************************
 * \brief Get Receiver Data Call back
 * \param in_frame_number frame number
 * \param in_exp_length real time exposure length (in 100ns) or 
 *                      sub frame number (Eiger 32 bit mode only)
 * \param in_packet_number number of packets caught for this frame
 * \param in_bunch_id bunch id from beamline
 * \param in_timestamp time stamp in 10MHz clock (not implemented for most)
 * \param in_mod_id module id	(not implemented for most)
 * \param in_x_coord x coordinates (detector id in 1D)
 * \param in_y_coord y coordinates (not implemented)
 * \param in_z_coord z coordinates (not implemented)
 * \param in_debug debug values if any
 * \param in_round_r_number (not implemented)
 * \param in_det_type detector type see :: detectorType
 * \param in_version version of standard header (structure format)
 * \param in_data_pointer pointer to data
 * \param in_data_size data size in bytes
 * \param in_user_data pointer to user data object
 ************************************************************************/
void CameraReceivers::acquisitionDataReadyCallBack(uint64_t   in_frame_number  ,
                                                   uint32_t   in_exp_length    ,
                                                   uint32_t   in_packet_number ,
                                                   uint64_t   in_bunch_id      ,
                                                   uint64_t   in_timestamp     ,
                                                   uint16_t   in_mod_id        ,
                                                   uint16_t   in_x_coord       ,
                                                   uint16_t   in_y_coord       ,
                                                   uint16_t   in_z_coord       ,
                                                   uint32_t   in_debug         ,
                                                   uint16_t   in_round_r_number,
                                                   uint8_t    in_det_type      ,
                                                   uint8_t    in_version       ,
                                                   char     * in_data_pointer  ,
                                                   uint32_t   in_data_size     ,
                                                   void     * in_user_data     )
{
#ifdef CAMERA_RECEIVERS_ACTIVATE_SDK_CALLBACK_TRACE
    PRINT_IN_COLOR (in_mod_id?in_mod_id:in_x_coord,
			"#### %d GetData: ####\n"
			"frameNumber: %llu\t\texpLength: %u\t\tpacketNumber: %u\t\tbunchId: %llu\t\ttimestamp: %llu\t\tmodId: %u\t\t"
			"xCoord: %u\t\tyCoord: %u\t\tzCoord: %u\t\tdebug: %u\t\troundRNumber: %u\t\tdetType: %u\t\t"
			"version: %u\t\tfirstbytedata: 0x%x\t\tdatsize: %u\n\n",
			in_x_coord, in_frame_number, in_exp_length, in_packet_number, in_bunch_id, in_timestamp, in_mod_id,
			in_x_coord, in_y_coord, in_z_coord, in_debug, in_round_r_number, in_det_type, in_version,
			((uint8_t)(*((uint8_t*)(in_data_pointer)))), in_data_size);
#endif

    // we call the internal management of the callback using the user data
    // the user data allows to have access to the 
    // CameraReceivers instance smart pointer and to the receiver index.
    CameraReceiver * user_data = static_cast<CameraReceiver *>(in_user_data);
    user_data->getReceivers()->acquisitionDataReady(user_data->getReceiverIndex(),
                                                    in_frame_number              ,
                                                    in_packet_number             ,
                                                    in_timestamp                 ,
                                                    in_data_pointer              ,
                                                    in_data_size                 );
}

//========================================================================================
