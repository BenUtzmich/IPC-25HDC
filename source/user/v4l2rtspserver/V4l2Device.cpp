/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2Device.cpp
** 
** -------------------------------------------------------------------------*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <iostream>

#include "AitXU.h"
#include "V4l2Device.h"

// -----------------------------------------
//    V4L2Device
// -----------------------------------------
V4l2Device::V4l2Device(const V4L2DeviceParameters&  params, v4l2_buf_type deviceType) : m_params(params), m_fd(-1), m_deviceType(deviceType), m_bufferSize(0), m_format(0)
{
}

V4l2Device::~V4l2Device() 
{
	this->close();
}

void V4l2Device::close() 
{
	if (m_fd != -1) 		
		::close(m_fd);
	
	m_fd = -1;
}

// query current format
void V4l2Device::queryFormat()
{
	struct v4l2_format     fmt;
	memset(&fmt,0,sizeof(fmt));
	fmt.type  = m_deviceType;
	if (0 == ioctl(m_fd,VIDIOC_G_FMT,&fmt)) 
	{
		m_format     = fmt.fmt.pix.pixelformat;
		m_width      = fmt.fmt.pix.width;
		m_height     = fmt.fmt.pix.height;
		m_bufferSize = fmt.fmt.pix.sizeimage;
	}
}

// intialize the V4L2 connection
bool V4l2Device::init(unsigned int mandatoryCapabilities)
{
        struct stat sb;
        if ( (stat(m_params.m_devName.c_str(), &sb)==0) && ((sb.st_mode & S_IFMT) == S_IFCHR) )
        {
		if (initdevice(m_params.m_devName.c_str(), mandatoryCapabilities) == -1)
		{
			std::cerr << "Cannot init device:" << m_params.m_devName << std::endl;
		}
	}
	else
	{
                // open a normal file
                m_fd = open(m_params.m_devName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
	}
	return (m_fd!=-1);
}

// intialize the V4L2 device
int V4l2Device::initdevice(const char *dev_name, unsigned int mandatoryCapabilities)
{
	m_fd = open(dev_name,  O_RDWR | O_NONBLOCK);
	if (m_fd < 0) 
	{
		std::cerr << "Cannot open device:" << m_params.m_devName << " " << strerror(errno) << std::endl;
		this->close();
		return -1;
	}
	if (checkCapabilities(m_fd,mandatoryCapabilities) !=0)
	{
		this->close();
		return -1;
	}	
	if (configureFormat(m_fd) !=0) 
	{
		this->close();
		return -1;
	}
	if (configureParam(m_fd) !=0)
	{
		this->close();
		return -1;
	}
	
	if (m_params.m_ait)
	{
		AitXU ait(m_fd, m_params.m_devName);
		ait.Init();

		char buf[20];
		ait.GetFWBuildDate(buf, 20);
		std::cout << "AIT Firmware Build Date: " << buf << std::endl;

		ait.GetFWVersion(buf, 20);
		std::cout << "AIT Firmware Version: " << buf << std::endl;

		ait.SetFrameRate(m_params.m_fps);
		ait.SetMirrFlip(m_params.m_aitMirrFlip);
		ait.SetIRCutMode(m_params.m_aitIRCutMode);
		ait.SetPFrameCount(30);
		ait.SetBitrate(2048);
	}

	return m_fd;
}

// check needed V4L2 capabilities
int V4l2Device::checkCapabilities(int fd, unsigned int mandatoryCapabilities)
{
	struct v4l2_capability cap;
	memset(&(cap), 0, sizeof(cap));
	if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) 
	{
		std::cerr << "Cannot get capabilities for device:" << m_params.m_devName << " " << strerror(errno) << std::endl;
		return -1;
	}
	std::cout << "driver:" << cap.driver << " " << std::hex << cap.capabilities << std::endl;
		
	if ((cap.capabilities & V4L2_CAP_READWRITE))     std::cout << m_params.m_devName << " support read/write" << std::endl;
	if ((cap.capabilities & V4L2_CAP_VIDEO_OUTPUT))  std::cout << m_params.m_devName << " support output" << std::endl;
	if ((cap.capabilities & V4L2_CAP_STREAMING))     std::cout << m_params.m_devName << " support streaming" << std::endl;
	if ((cap.capabilities & V4L2_CAP_TIMEPERFRAME))  std::cout << m_params.m_devName << " support timeperframe" << std::endl;
	if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) std::cout << m_params.m_devName << " support capture" << std::endl;
	if ((cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)) std::cout << m_params.m_devName << " support overlay" << std::endl;
	
	if ( (cap.capabilities & mandatoryCapabilities) != mandatoryCapabilities )
	{
		std::cerr << "Mandatory capability not available for device:" << m_params.m_devName << std::endl;
		return -1;
	}
	
	return 0;
}

std::string fourcc(unsigned int format)
{
	char formatArray[] = { (char)(format&0xff), (char)((format>>8)&0xff), (char)((format>>16)&0xff), (char)((format>>24)&0xff), 0 };
	return std::string(formatArray, strlen(formatArray));
}

// configure capture format 
int V4l2Device::configureFormat(int fd)
{
	
	if (m_params.m_format==0) 
	{
		this->queryFormat();
		m_params.m_format = m_format;
		m_params.m_width  = m_width;
		m_params.m_height = m_height;
	}		
		
	struct v4l2_format   fmt;			
	memset(&(fmt), 0, sizeof(fmt));
	fmt.type                = m_deviceType;
	fmt.fmt.pix.width       = m_params.m_width;
	fmt.fmt.pix.height      = m_params.m_height;
	fmt.fmt.pix.pixelformat = m_params.m_format;
	fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
	{
		std::cerr << "Cannot set format for device:" << m_params.m_devName << " " << strerror(errno) << std::endl;
		return -1;
	}			
	if (fmt.fmt.pix.pixelformat != m_params.m_format) 
	{
		std::cerr << "Cannot set pixelformat to:" << fourcc(m_params.m_format) << " format is:" << fourcc(fmt.fmt.pix.pixelformat) << std::endl;
		return -1;
	}
	if ((fmt.fmt.pix.width != m_params.m_width) || (fmt.fmt.pix.height != m_params.m_height))
	{
		std::cerr << "Cannot set size width:" << fmt.fmt.pix.width << " height:" << fmt.fmt.pix.height << std::endl;
	}
	
	m_format     = fmt.fmt.pix.pixelformat;
	m_width      = fmt.fmt.pix.width;
	m_height     = fmt.fmt.pix.height;		
	m_bufferSize = fmt.fmt.pix.sizeimage;
	
	std::cout << std::dec << m_params.m_devName << ":" << fourcc(m_format) << " size:" << m_params.m_width << "x" << m_params.m_height << " bufferSize:" << m_bufferSize << std::endl;
	
	return 0;
}

// configure capture FPS 
int V4l2Device::configureParam(int fd)
{
	if (m_params.m_fps!=0)
	{
		struct v4l2_streamparm   param;			
		memset(&(param), 0, sizeof(param));
		param.type = m_deviceType;
		param.parm.capture.timeperframe.numerator = 1;
		param.parm.capture.timeperframe.denominator = m_params.m_fps;

		if (ioctl(fd, VIDIOC_S_PARM, &param) == -1)
		{
			std::cerr << "Cannot set param for device:" << m_params.m_devName << " " << strerror(errno) << std::endl;
		}
	
		std::cout << std::dec << "fps:" << param.parm.capture.timeperframe.numerator << "/" << param.parm.capture.timeperframe.denominator << std::endl;
		std::cout << "nbBuffer:" << param.parm.capture.readbuffers << std::endl;
	}

	
	return 0;
}


