#include "stdafx.h"
#include <iostream>
#include <string>
#include <CL/cl.h>
#include <CL/cl_gl.h>
#define WIN32_LEAN_AND_MEAN //reduce imports, save compile time
#include <windows.h>
#define GLEW_STATIC 1
#include <GL/glew.h>
#include <GLFW/glfw3.h>

int main(int argc, char** argv)
{
	GLFWwindow* glfwwindow;
	{//OpenGL/GLFW Init
		if (!glfwInit()) {
			std::cerr << "Error: GLFW init failed" << std::endl;
			exit(EXIT_FAILURE);
		}
		glfwwindow = glfwCreateWindow(200, 200, "Nvidia interop bug demo", nullptr, nullptr);
		glfwMakeContextCurrent(glfwwindow);
		glewInit();
		std::cout << "OpenGL Info: " << (char*)glGetString(GL_VENDOR) << " " << (char*)glGetString(GL_RENDERER) << std::endl;
	}
	cl_context clcontext;
	cl_command_queue clqueue;
	{//OpenCL init
		cl_platform_id platform = nullptr;
		{//Platform init
			cl_uint numPlatforms;
			clGetPlatformIDs(0, nullptr, &numPlatforms);
			if (numPlatforms == 0)
			{
				std::cerr << "Error: No OpenCL platforms available" << std::endl;
				return EXIT_FAILURE;
			}
			cl_platform_id* all_platforms = new cl_platform_id[numPlatforms];
			clGetPlatformIDs(numPlatforms, all_platforms, nullptr);
			for (size_t i = 0; i < numPlatforms; i++) //Select Nvidia out of the platforms
			{
				char name[300];
				clGetPlatformInfo(all_platforms[i], CL_PLATFORM_NAME, sizeof(name), &name, nullptr);
				std::string namestring(name);
				if (namestring.find("NVIDIA") != std::string::npos || namestring.find("Nvidia") != std::string::npos)
					platform = all_platforms[i];	
			}
			if (platform == nullptr) {
				std::cerr << "No Nvidia OpenCL platform found, will default to platform 0 ";
				
			}

			delete[] all_platforms;
		}
		{ //Create shared context
			cl_context_properties properties[7];
			properties[0] = CL_CONTEXT_PLATFORM; //This is different for other operating systems than Windows
			properties[1] = (cl_context_properties)platform;
			properties[2] = CL_GL_CONTEXT_KHR;
			properties[3] = (cl_context_properties)wglGetCurrentContext();
			properties[4] = CL_WGL_HDC_KHR;
			properties[5] = (cl_context_properties)wglGetCurrentDC();
			properties[6] = 0;

			clcontext = clCreateContextFromType(properties, CL_DEVICE_TYPE_GPU, nullptr, nullptr, nullptr);
		}
		cl_device_id cldevice;
		{ //Create cldevice
			cl_device_id* devices;
			cl_command_queue commandQueue = nullptr;
			size_t numDevices = 0;

			// First get the size of the devices buffer
			clGetContextInfo(clcontext, CL_CONTEXT_DEVICES, 0, nullptr, &numDevices);

			if (numDevices == 0)
			{
				std::cerr << "Error: No OpenCL devices available" << std::endl;
				return EXIT_FAILURE;
			}
			devices = new cl_device_id[numDevices];
			clGetContextInfo(clcontext, CL_CONTEXT_DEVICES, numDevices, devices, nullptr);
			cldevice = devices[0];
			delete[] devices;
		}
		{ //Create CL command queue
			clqueue = clCreateCommandQueue(clcontext, cldevice, 0, nullptr);
		}
		char platformname[300];
		char devicename[300];
		clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platformname), &platformname, nullptr);
		clGetDeviceInfo(cldevice, CL_DEVICE_NAME, sizeof(devicename), &devicename, nullptr);
		std::cout << "OpenCL platform " << platformname << " device " << devicename << std::endl;
	}
	size_t size = 200 * 200 * 4; //w=200, h=200, 4 bytes per channel
	char* databuffer = new char[size];
	GLuint glbuffer, gltexture;
	cl_mem unsharedbuffer, sharedbuffer, unsharedtexture, sharedtexture;
	{ //Init test data
		glGenBuffers(1, &glbuffer);
		glBindBuffer(GL_ARRAY_BUFFER, glbuffer);
		glBufferData(GL_ARRAY_BUFFER, size, databuffer, GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);

		glGenTextures(1, &gltexture);
		glBindTexture(GL_TEXTURE_2D, gltexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 200, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, databuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); //Intel needs this for shared textures
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); //Intel needs this for shared textures
		glBindTexture(GL_TEXTURE_2D, GL_NONE);

		sharedtexture = clCreateFromGLTexture(clcontext, CL_MEM_READ_WRITE, GL_TEXTURE_2D, 0, gltexture, nullptr);
		sharedbuffer = clCreateFromGLBuffer(clcontext, CL_MEM_READ_WRITE, glbuffer, nullptr);

		unsharedbuffer = clCreateBuffer(clcontext, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, size, databuffer, nullptr);
		cl_image_format imgformat;
		cl_image_desc desc;
		imgformat.image_channel_data_type = CL_UNSIGNED_INT8;
		imgformat.image_channel_order = CL_RGBA;
		desc.image_type = CL_MEM_OBJECT_IMAGE2D;
		desc.image_width = 200;
		desc.image_height = 200;
		desc.image_depth = 1;
		desc.image_array_size = 1;
		desc.image_row_pitch = 0;
		desc.image_slice_pitch = 0;
		desc.num_mip_levels = 0;
		desc.num_samples = 0;
		desc.buffer = nullptr;
		unsharedtexture = clCreateImage(clcontext, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, &imgformat, &desc, databuffer, nullptr);
	}
	{
		const size_t origin[3] = { 0, 0, 0 };
		const size_t region[3] = { 200, 200, 1 };
		size_t pitch;
		//
		//MAIN PART BEGINS HERE
		//
		{ //OpenGL buffer
			std::cout << "Mapping buffer with OpenGL: ";
			glBindBuffer(GL_ARRAY_BUFFER, glbuffer);
			void* glmapptr = glMapBuffer(GL_ARRAY_BUFFER, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
			glUnmapBuffer(GL_ARRAY_BUFFER);
			glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);
			std::cout << "OK" << std::endl;
			glFinish();
		}
		{ //OpenCL unshared texture
			std::cout << "Mapping unshared texture with OpenCL: ";
			void* unsimgptr = clEnqueueMapImage(clqueue, unsharedtexture, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, origin, region, &pitch, nullptr, 0, nullptr, nullptr, nullptr); //This API call works fine for unshared objects
			clEnqueueUnmapMemObject(clqueue, unsharedtexture, unsimgptr, 0, nullptr, nullptr);
			std::cout << "OK" << std::endl;
		}
		{ //OpenCL shared texture
			std::cout << "Mapping shared texture with OpenCL: ";
			clEnqueueAcquireGLObjects(clqueue, 1, &sharedtexture, 0, nullptr, nullptr);
			void* shdimgptr = clEnqueueMapImage(clqueue, unsharedtexture, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, origin, region, &pitch, nullptr, 0, nullptr, nullptr, nullptr); //This API call works fine shared objects
			clEnqueueUnmapMemObject(clqueue, unsharedtexture, shdimgptr, 0, nullptr, nullptr);
			clEnqueueReleaseGLObjects(clqueue, 1, &sharedtexture, 0, nullptr, nullptr);
			std::cout << "OK" << std::endl;
		}
		{ //OpenCL unshared buffer
			std::cout << "Mapping unshared buffer with OpenCL: ";
			void* unsbufptr = clEnqueueMapBuffer(clqueue, unsharedbuffer, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, size, 0, nullptr, nullptr, nullptr); //This API call works fine for unshared buffers
			clEnqueueUnmapMemObject(clqueue, unsharedbuffer, unsbufptr, 0, nullptr, nullptr);
			std::cout << "OK" << std::endl;
		}
		{ //OpenCL shared buffer
			std::cout << "Mapping shared buffer with OpenCL (EXPECTING CRASH ON NVIDIA SYSTEMS): " << std::endl;
			clEnqueueAcquireGLObjects(clqueue, 1, &sharedbuffer, 0, nullptr, nullptr);
			//
			//CRITICAL PART BEGINS HERE
			//

			void* shdbufptr = clEnqueueMapBuffer(clqueue, sharedbuffer, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, size, 0, nullptr, nullptr, nullptr);
			//On Nvidia systems when using shared objects, error 0xC0000005 occurs in ntdll.dll: write access violation at position 0xSOMETHING
			//This leaves my application in an unusable state
			//But it works fine everywhere else (tested on ARM, AMD, Intel systems)

			//
			//CRITICAL PART ENDS HERE
			//
			std::cout << "did not fail" << std::endl;
			clEnqueueUnmapMemObject(clqueue, sharedbuffer, shdbufptr, 0, nullptr, nullptr);
			clEnqueueReleaseGLObjects(clqueue, 1, &sharedbuffer, 0, nullptr, nullptr);
			std::cout << "OK" << std::endl;
		}
		//
		//MAIN PART ENDS HERE
		//
	}
	clFinish(clqueue);

	delete[] databuffer;
	clReleaseMemObject(sharedbuffer);
	clReleaseMemObject(unsharedbuffer);
	clReleaseMemObject(sharedtexture);
	clReleaseMemObject(unsharedtexture);

	clReleaseCommandQueue(clqueue);
	clReleaseContext(clcontext);

	glDeleteTextures(1, &gltexture);
	glDeleteBuffers(1, &glbuffer);

	glfwDestroyWindow(glfwwindow);
	glfwTerminate();
	return EXIT_SUCCESS;
}

