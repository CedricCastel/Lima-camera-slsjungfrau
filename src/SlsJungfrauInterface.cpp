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

/*************************************************************************/
/*! 
 *  \file   SlsJungfrauInterface.cpp
 *  \brief  SlsJungfrau hardware class implementation 
 *  \author C�dric Castel - SOLEIL (MEDIANE SYSTEME - IT consultant) 
*/
/*************************************************************************/

#include "SlsJungfrauCamera.h"
#include "SlsJungfrauInterface.h"

using namespace lima;
using namespace lima::SlsJungfrau;
using namespace std;

/*******************************************************************
 * \brief constructor
 *******************************************************************/
Interface::Interface(Camera & cam) : m_cam(cam), m_det_info(cam), m_sync(cam)
{
    DEB_CONSTRUCTOR();

    // No hardware binning or hardware ROIs yet for this camera
    HwDetInfoCtrlObj *det_info = &m_det_info;
    m_cap_list.push_back(HwCap(det_info));

    HwSyncCtrlObj *sync = &m_sync;
    m_cap_list.push_back(HwCap(sync));
    
    HwBufferCtrlObj *buffer = cam.getBufferCtrlObj();
    m_cap_list.push_back(HwCap(buffer));

    //event capability
    m_event = m_cam.getEventCtrlObj();    
    m_cap_list.push_back(HwCap(m_event));
}

/*******************************************************************
 * \brief destructor
 *******************************************************************/
Interface::~Interface()
{
    DEB_DESTRUCTOR();
}

//==================================================================
// specifics methods management
//==================================================================
//==================================================================
// capability list management
//==================================================================
/*******************************************************************
 * \brief Gets the capability list
 *******************************************************************/
void Interface::getCapList(HwInterface::CapList & cap_list) const
{
    DEB_MEMBER_FUNCT();
    cap_list = m_cap_list;
}

//==================================================================
// reset management
//==================================================================
/*******************************************************************
 * \brief resets the interface and stop the acquisition
 *******************************************************************/
void Interface::reset(ResetLevel reset_level)
{
    DEB_MEMBER_FUNCT();
    DEB_PARAM() << DEB_VAR1(reset_level);

    stopAcq();

    Size              image_size;
    ImageType         image_type;
    HwBufferCtrlObj * buffer    ;

    m_det_info.getMaxImageSize(image_size);
    m_det_info.getDefImageType(image_type);

    FrameDim frame_dim(image_size, image_type);

    buffer = m_cam.getBufferCtrlObj();
    buffer->setFrameDim      (frame_dim);
    buffer->setNbConcatFrames(1);
    buffer->setNbBuffers     (1);
}

//==================================================================
// acquisition management
//==================================================================
/*******************************************************************
 * \brief prepares the acquisition
 *******************************************************************/
void Interface::prepareAcq()
{
    DEB_MEMBER_FUNCT();
    m_cam.prepareAcq();
}

/*******************************************************************
 * \brief starts the acquisition (start/snap)
 *******************************************************************/
void Interface::startAcq()
{
    DEB_MEMBER_FUNCT();
    m_cam.startAcq();
}

/*******************************************************************
 * \brief stops the acquisition
 *******************************************************************/
void Interface::stopAcq()
{
    DEB_MEMBER_FUNCT();
    m_cam.stopAcq();
}

//==================================================================
// status management
//==================================================================
/*******************************************************************
 * \brief Gets the current status
 *******************************************************************/
void Interface::getStatus(StatusType & status)
{
    DEB_MEMBER_FUNCT();

    HwInterface::StatusType::Basic hw_status;
    Camera::Status camera_status = m_cam.getStatus();
    
    if(camera_status == Camera::Status::Idle)
    {
        hw_status = HwInterface::StatusType::Ready;
    }
    else
    if((camera_status == Camera::Status::Waiting) || 
       (camera_status == Camera::Status::Running))
    {
        hw_status = HwInterface::StatusType::Exposure;
    }
    else
    if(camera_status == Camera::Status::Error)
    {
        hw_status = HwInterface::StatusType::Fault;
    }
    
    status.set(hw_status);

    DEB_RETURN() << DEB_VAR1(status);
}

//==================================================================
// Acquired frames management
//==================================================================
/*******************************************************************
 * \brief Gets the number of acquired frames
 *******************************************************************/
int Interface::getNbHwAcquiredFrames()
{
    DEB_MEMBER_FUNCT();

    uint64_t NbHwAcquiredFrames = m_cam.getNbAcquiredFrames();

    DEB_RETURN() << DEB_VAR1(NbHwAcquiredFrames);

    return static_cast<int>(NbHwAcquiredFrames);
}

//==================================================================
// camera access methods
//==================================================================
/*******************************************************************
 * \brief camera instance access for the client
 *******************************************************************/
Camera & Interface::getCamera() 
{
    return m_cam;
}

/*******************************************************************
 * \brief camera instance const access for the client
 *******************************************************************/
const Camera& Interface::getCamera() const
{ 
    return m_cam; 
}

/*************************************************************************/