/* -----------------------------------------------------
This source code is public domain ( CC0 )
The code is provided as-is without limitations, requirements and responsibilities.
Creators and contributors to this source code are provided as a token of appreciation
and no one associated with this source code can be held responsible for any possible
damages or losses of any kind.

Original file creator:  Niko Kauppi (Code maintenance)
Contributors:
----------------------------------------------------- */
/*
        Copyright 2021 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "Window.h"
#include "Renderer.h"
#include "Shared.h"
#include "RenderPassDeps.h"

#include <assert.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "VulkanTools.h"

Window::Window( Renderer * renderer, uint32_t size_x, uint32_t size_y, std::string name, void * nativeWindow )
{
  
	_renderer			= renderer;
	_surface_size_x		= size_x;
	_surface_size_y		= size_y;
	_window_name		= name;
#if defined(__ANDROID__)  
  	window = (ANativeWindow *)nativeWindow;
#endif
	_InitOSWindow();
	_InitSurface();
	_InitSwapchain();
	_InitSwapchainImages();
	_InitDepthStencilImage();
	_InitRenderPass();
	_InitFramebuffers();
	_InitSynchronizations();
}

Window::~Window()
{
	YABA_QIDLE(YabaQueueWaitIdle( _renderer->GetVulkanQueue() ));
	_DeInitFrameDump();
	_DeInitSynchronizations();
	_DeInitFramebuffers();
	_DeInitRenderPass();
	_DeInitDepthStencilImage();
	_DeInitSwapchainImages();
	_DeInitSwapchain();
	_DeInitSurface();
	_DeInitOSWindow();
}

void Window::resize(int width, int height) {

  LOGI("%s %d %d", "resize called", width,height);
  //if (_surface_size_x == width && _surface_size_y == height) {
  //  return;
  //}
  cleanupSwapChain();
}

void Window::cleanupSwapChain() {

  VkDevice device = _renderer->GetVulkanDevice();
  vkDeviceWaitIdle(device);

  _DeInitFramebuffers();
  _DeInitDepthStencilImage();
  _DeInitSwapchainImages();
  _DeInitSwapchain();
  _DeInitSurface();

  _InitSurface();
  _InitSwapchain();
  _InitSwapchainImages();
  _InitDepthStencilImage();
  _InitFramebuffers();

}

void Window::Close()
{
	_window_should_run		= false;
}

bool Window::Update()
{
	_UpdateOSWindow();
	return _window_should_run;
}

int Window::BeginRender()
{
	ErrorCheck( vkAcquireNextImageKHR(
		_renderer->GetVulkanDevice(),
		_swapchain,
		UINT64_MAX,
		VK_NULL_HANDLE,
		_swapchain_image_available,
		&_active_swapchain_image_id ) );
	ErrorCheck( vkWaitForFences( _renderer->GetVulkanDevice(), 1, &_swapchain_image_available, VK_TRUE, UINT64_MAX ) );
	ErrorCheck( vkResetFences( _renderer->GetVulkanDevice(), 1, &_swapchain_image_available ) );
	{ const long long _qi0 = PerfNowNs(); ErrorCheck( YabaQueueWaitIdle( _renderer->GetVulkanQueue() ) ); PerfAddVk(4, PerfNowNs() - _qi0); }

  return _active_swapchain_image_id;
}

bool Window::_DumpFrame(const std::vector<VkSemaphore> & wait_semaphores)
{
	VkDevice dev = _renderer->GetVulkanDevice();
	const uint32_t w = _surface_size_x, h = _surface_size_y;
	if( _fd_buf == VK_NULL_HANDLE ) {
		_fd_size = (VkDeviceSize)w * h * 4;
		VkBufferCreateInfo bi {};
		bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bi.size = _fd_size;
		bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ErrorCheck( vkCreateBuffer( dev, &bi, nullptr, &_fd_buf ) );
  YABA_VK_NAME(dev, VK_OBJECT_TYPE_BUFFER, _fd_buf, "_fd_buf");
		VkMemoryRequirements mr;
		vkGetBufferMemoryRequirements( dev, _fd_buf, &mr );
		VkMemoryAllocateInfo ai {};
		ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize = mr.size;
		ai.memoryTypeIndex = FindMemoryTypeIndex( &_renderer->GetVulkanPhysicalDeviceMemoryProperties(), &mr,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		ErrorCheck( vkAllocateMemory( dev, &ai, nullptr, &_fd_mem ) );
		ErrorCheck( vkBindBufferMemory( dev, _fd_buf, _fd_mem, 0 ) );
		VkCommandPoolCreateInfo pi {};
		pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pi.queueFamilyIndex = _renderer->GetVulkanGraphicsQueueFamilyIndex();
		ErrorCheck( vkCreateCommandPool( dev, &pi, nullptr, &_fd_pool ) );
		VkSemaphoreCreateInfo si {};
		si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		ErrorCheck( vkCreateSemaphore( dev, &si, nullptr, &_fd_sem ) );
		VkFenceCreateInfo fi {};
		fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		ErrorCheck( vkCreateFence( dev, &fi, nullptr, &_fd_fence ) );
	}
	VkCommandBufferAllocateInfo cai {};
	cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cai.commandPool = _fd_pool;
	cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cai.commandBufferCount = 1;
	VkCommandBuffer cb;
	ErrorCheck( vkAllocateCommandBuffers( dev, &cai, &cb ) );
	VkCommandBufferBeginInfo bgi {};
	bgi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bgi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	ErrorCheck( vkBeginCommandBuffer( cb, &bgi ) );
	VkImage img = _swapchain_images[ _active_swapchain_image_id ];
	VkImageMemoryBarrier b {};
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = img;
	b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	vkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b );
	VkBufferImageCopy rg {};
	rg.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	rg.imageExtent = { w, h, 1 };
	vkCmdCopyImageToBuffer( cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _fd_buf, 1, &rg );
	b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b.dstAccessMask = 0;
	b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b );
	ErrorCheck( vkEndCommandBuffer( cb ) );

	std::vector<VkPipelineStageFlags> stages( wait_semaphores.size(), VK_PIPELINE_STAGE_TRANSFER_BIT );
	VkSubmitInfo sub {};
	sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	sub.waitSemaphoreCount = wait_semaphores.size();
	sub.pWaitSemaphores = wait_semaphores.data();
	sub.pWaitDstStageMask = stages.data();
	sub.commandBufferCount = 1;
	sub.pCommandBuffers = &cb;
	sub.signalSemaphoreCount = 1;
	sub.pSignalSemaphores = &_fd_sem;
	ErrorCheck( YabaQueueSubmit( _renderer->GetVulkanQueue(), 1, &sub, _fd_fence ) );
	VkResult wr = vkWaitForFences( dev, 1, &_fd_fence, VK_TRUE, 5000000000ull );
	vkResetFences( dev, 1, &_fd_fence );
	vkFreeCommandBuffers( dev, _fd_pool, 1, &cb );
	if( wr != VK_SUCCESS ) {
		LOGI("FRAMEDUMP: copy fence wait failed (%d) at frame %llu\n", (int)wr, (unsigned long long)_fd_frame);
		return true; /* semaphore is still signalled by the queue eventually */
	}

	void * p = nullptr;
	if( vkMapMemory( dev, _fd_mem, 0, _fd_size, 0, &p ) == VK_SUCCESS ) {
		const char * dir = getenv("YABA_VK_FRAMEDUMP_DIR");
		char name[512];
		snprintf( name, sizeof(name), "%s/frame-%06llu.ppm", dir ? dir : ".", (unsigned long long)_fd_frame );
		FILE * f = fopen( name, "wb" );
		if( f ) {
			fprintf( f, "P6\n%u %u\n255\n", w, h );
			const bool bgr = ( _surface_format.format == VK_FORMAT_B8G8R8A8_UNORM || _surface_format.format == VK_FORMAT_B8G8R8A8_SRGB );
			const unsigned char * src = (const unsigned char *)p;
			std::vector<unsigned char> row( (size_t)w * 3 );
			uint64_t sum = 0;
			for( uint32_t y = 0; y < h; ++y ) {
				for( uint32_t x = 0; x < w; ++x ) {
					const unsigned char * px = src + ((size_t)y * w + x) * 4;
					row[x*3+0] = bgr ? px[2] : px[0];
					row[x*3+1] = px[1];
					row[x*3+2] = bgr ? px[0] : px[2];
					sum += px[0] + px[1] + px[2];
				}
				fwrite( row.data(), 1, row.size(), f );
			}
			fclose( f );
			/* mean brightness x1000 is a cheap numeric "is it black / did it change" metric */
			LOGI("FRAMEDUMP: wrote %s fmt=%d mean_luma_x1000=%llu\n", name, (int)_surface_format.format,
				(unsigned long long)( sum * 1000 / ((uint64_t)w * h * 3) ));
		} else {
			LOGI("FRAMEDUMP: cannot open %s\n", name);
		}
		vkUnmapMemory( dev, _fd_mem );
	}
	return true;
}

void Window::_DeInitFrameDump()
{
	VkDevice dev = _renderer->GetVulkanDevice();
	if( _fd_fence ) vkDestroyFence( dev, _fd_fence, nullptr );
	if( _fd_sem ) vkDestroySemaphore( dev, _fd_sem, nullptr );
	if( _fd_pool ) vkDestroyCommandPool( dev, _fd_pool, nullptr );
	if( _fd_buf ) vkDestroyBuffer( dev, _fd_buf, nullptr );
	if( _fd_mem ) vkFreeMemory( dev, _fd_mem, nullptr );
	_fd_fence = VK_NULL_HANDLE; _fd_sem = VK_NULL_HANDLE; _fd_pool = VK_NULL_HANDLE;
	_fd_buf = VK_NULL_HANDLE; _fd_mem = VK_NULL_HANDLE;
}

void Window::EndRender( std::vector<VkSemaphore> wait_semaphores )
{
	VkResult present_result = VkResult::VK_RESULT_MAX_ENUM;

	if( _fd_every > 0 && _fd_usage_ok ) {
		if( ( _fd_frame % (uint64_t)_fd_every ) == 0 ) {
			_DumpFrame( wait_semaphores );
			wait_semaphores.assign( 1, _fd_sem ); /* present waits on the copy, which waited on the render */
		}
		++_fd_frame;
	}

	VkPresentInfoKHR present_info {};
	present_info.sType					= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount		= wait_semaphores.size();
	present_info.pWaitSemaphores		= wait_semaphores.data();
	present_info.swapchainCount			= 1;
	present_info.pSwapchains			= &_swapchain;
	present_info.pImageIndices			= &_active_swapchain_image_id;
	present_info.pResults				= &present_result;

	ErrorCheck( YabaQueuePresent( _renderer->GetVulkanQueue(), &present_info ) );
	ErrorCheck( present_result );
}

void Window::RecordPresentTransition( VkCommandBuffer cmd )
{
	/* Single place where the swapchain image goes to PRESENT_SRC_KHR (the render passes leave it in
	   COLOR_ATTACHMENT_OPTIMAL). Must be recorded after the last pass and before vkEndCommandBuffer. */
	VkImageMemoryBarrier b {};
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = _swapchain_images[ _active_swapchain_image_id ];
	b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	b.dstAccessMask = 0;
	b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &b );
}

VkRenderPass Window::GetVulkanRenderPass()
{
	return _render_pass;
}

VkRenderPass Window::GetVulkanKeepRenderPass()
{
  return _render_pass_keep;
}


VkFramebuffer Window::GetVulkanActiveFramebuffer()
{
	return _framebuffers[ _active_swapchain_image_id ];
}

VkExtent2D Window::GetVulkanSurfaceSize()
{
	return { _surface_size_x, _surface_size_y };
}

void Window::_InitSurface()
{
	_InitOSSurface();

	auto gpu = _renderer->GetVulkanPhysicalDevice();

	LOGI("%s %d","vkGetPhysicalDeviceSurfaceSupportKHR in ",_renderer->GetVulkanGraphicsQueueFamilyIndex());
	VkBool32 WSI_supported = false;

	ErrorCheck( vkGetPhysicalDeviceSurfaceSupportKHR( gpu, _renderer->GetVulkanGraphicsQueueFamilyIndex(), _surface, &WSI_supported ) );
	if( !WSI_supported ) {
		assert( 0 && "WSI not supported" );
		std::exit( -1 );
	}

	LOGI("%s","vkGetPhysicalDeviceSurfaceSupportKHR out");

	LOGI("%s","vkGetPhysicalDeviceSurfaceCapabilitiesKHR in");
	ErrorCheck( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( gpu, _surface, &_surface_capabilities ) );
	if( _surface_capabilities.currentExtent.width < UINT32_MAX ) {
		_surface_size_x			= _surface_capabilities.currentExtent.width;
		_surface_size_y			= _surface_capabilities.currentExtent.height;
	}
	LOGI("%s %d,%d","vkGetPhysicalDeviceSurfaceCapabilitiesKHR out",_surface_size_x,_surface_size_y );
	LOGI("SURFACE supportedUsageFlags=0x%x (TRANSFER_SRC=%d TRANSFER_DST=%d) minImages=%u maxImages=%u currentTransform=0x%x\n",
		(unsigned)_surface_capabilities.supportedUsageFlags,
		(_surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)?1:0,
		(_surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)?1:0,
		_surface_capabilities.minImageCount,_surface_capabilities.maxImageCount,(unsigned)_surface_capabilities.currentTransform);

	{
		uint32_t format_count = 0;
		LOGI("%s","vkGetPhysicalDeviceSurfaceFormatsKHR In");
		ErrorCheck( vkGetPhysicalDeviceSurfaceFormatsKHR( gpu, _surface, &format_count, nullptr ) );
		if( format_count == 0 ) {
			assert( 0 && "Surface formats missing." );
			std::exit( -1 );
		}
		LOGI("%s %d","vkGetPhysicalDeviceSurfaceFormatsKHR out",format_count);

		std::vector<VkSurfaceFormatKHR> formats( format_count );

		LOGI("%s","vkGetPhysicalDeviceSurfaceFormatsKHR In");
		ErrorCheck( vkGetPhysicalDeviceSurfaceFormatsKHR( gpu, _surface, &format_count, formats.data() ) );
		for( uint32_t fi = 0; fi < format_count; ++fi )
			LOGI("SURFACE format[%u] = %d colorSpace=%d\n", fi, (int)formats[ fi ].format, (int)formats[ fi ].colorSpace );
		if( formats[ 0 ].format == VK_FORMAT_UNDEFINED ) {
			_surface_format.format		= VK_FORMAT_B8G8R8A8_UNORM;
			_surface_format.colorSpace	= VK_COLORSPACE_SRGB_NONLINEAR_KHR;
		} else {
			_surface_format				= formats[ 0 ];
			/* Prefer a UNORM format: the emulator writes display-referred colours, so an _SRGB attachment would
			   gamma-encode them a second time. First choice RGBA8 UNORM, then BGRA8 UNORM, else whatever came first. */
			const VkFormat prefer[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
			bool picked = false;
			for( VkFormat want : prefer ) {
				for( uint32_t fi = 0; fi < format_count && !picked; ++fi ) {
					if( formats[ fi ].format == want ) { _surface_format = formats[ fi ]; picked = true; }
				}
				if( picked ) break;
			}
		}
		LOGI("SURFACE chosen format = %d colorSpace=%d\n", (int)_surface_format.format, (int)_surface_format.colorSpace );
		LOGI("%s","vkGetPhysicalDeviceSurfaceFormatsKHR OUT");
	}
}

void Window::_DeInitSurface()
{
	vkDestroySurfaceKHR( _renderer->GetVulkanInstance(), _surface, nullptr );
}

void Window::_InitSwapchain()
{
	// This code is old and the fixed one is below
	// if( _swapchain_image_count > _surface_capabilities.maxImageCount ) _swapchain_image_count = _surface_capabilities.maxImageCount;
	// if( _swapchain_image_count < _surface_capabilities.minImageCount + 1 ) _swapchain_image_count = _surface_capabilities.minImageCount + 1;

	// The code above will work just fine in our tutorials and likely on every possible implementation of vulkan as well
	// so this change isn't that important. Just to be absolutely sure we don't go over or below the given limits we should check this a
	// little bit different though. maxImageCount can actually be zero in which case the amount of swapchain images do not have an
	// upper limit other than available memory. It's also possible that the swapchain image amount is locked to a certain
	// value on certain systems. The code below takes into consideration both of these possibilities.
	if( _swapchain_image_count < _surface_capabilities.minImageCount + 1 ) _swapchain_image_count = _surface_capabilities.minImageCount + 1;
	if( _surface_capabilities.maxImageCount > 0 ) {
		if( _swapchain_image_count > _surface_capabilities.maxImageCount ) _swapchain_image_count = _surface_capabilities.maxImageCount;
	}

	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	{
		uint32_t present_mode_count = 0;
		ErrorCheck( vkGetPhysicalDeviceSurfacePresentModesKHR( _renderer->GetVulkanPhysicalDevice(), _surface, &present_mode_count, nullptr ) );
		std::vector<VkPresentModeKHR> present_mode_list( present_mode_count );
		ErrorCheck( vkGetPhysicalDeviceSurfacePresentModesKHR( _renderer->GetVulkanPhysicalDevice(), _surface, &present_mode_count, present_mode_list.data() ) );
		for( auto m : present_mode_list ) {
			if( m == VK_PRESENT_MODE_MAILBOX_KHR ) present_mode = m;
		}
	}

	VkSwapchainCreateInfoKHR swapchain_create_info {};
	swapchain_create_info.sType						= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.surface					= _surface;
	swapchain_create_info.minImageCount				= _swapchain_image_count;
	swapchain_create_info.imageFormat				= _surface_format.format;
	swapchain_create_info.imageColorSpace			= _surface_format.colorSpace;
	swapchain_create_info.imageExtent.width			= _surface_size_x;
	swapchain_create_info.imageExtent.height		= _surface_size_y;
	swapchain_create_info.imageArrayLayers			= 1;
	swapchain_create_info.imageUsage				= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	/* The Resolution 2x/Original path blits the sub render target INTO the swapchain image, which
	   requires TRANSFER_DST on it (VUID-vkCmdBlitImage-dstImage-00224 / ...-oldLayout-01213). */
	if( _surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) {
		swapchain_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		LOGI("SWAPCHAIN: TRANSFER_DST usage enabled\n");
	} else {
		LOGI("SWAPCHAIN: WARNING surface does not offer TRANSFER_DST; the blit path (Resolution 2x/Original) cannot be valid\n");
	}
	{
		const char * fd = getenv("YABA_VK_FRAMEDUMP");
		_fd_every = fd ? atoi(fd) : 0;
		if( _fd_every > 0 ) {
			if( _surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) {
				swapchain_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				_fd_usage_ok = true;
				LOGI("FRAMEDUMP: enabled, every %d frames\n", _fd_every);
			} else {
				LOGI("FRAMEDUMP: requested but surface lacks TRANSFER_SRC; disabled\n");
			}
		}
	}
	swapchain_create_info.imageSharingMode			= VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount		= 0;
	swapchain_create_info.pQueueFamilyIndices		= nullptr;
	swapchain_create_info.preTransform				= VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.compositeAlpha			= VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode				= present_mode;
	swapchain_create_info.clipped					= VK_TRUE;
	swapchain_create_info.oldSwapchain				= VK_NULL_HANDLE;

	ErrorCheck( vkCreateSwapchainKHR( _renderer->GetVulkanDevice(), &swapchain_create_info, nullptr, &_swapchain ) );

	ErrorCheck( vkGetSwapchainImagesKHR( _renderer->GetVulkanDevice(), _swapchain, &_swapchain_image_count, nullptr ) );
}

void Window::_DeInitSwapchain()
{
	vkDestroySwapchainKHR( _renderer->GetVulkanDevice(), _swapchain, nullptr );
}

void Window::_InitSwapchainImages()
{
	_swapchain_images.resize( _swapchain_image_count );
	_swapchain_image_views.resize( _swapchain_image_count );

	ErrorCheck( vkGetSwapchainImagesKHR( _renderer->GetVulkanDevice(), _swapchain, &_swapchain_image_count, _swapchain_images.data() ) );
	for( uint32_t i=0; i < _swapchain_image_count; ++i ) YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_IMAGE, _swapchain_images[ i ], "swapchain_image");

	for( uint32_t i=0; i < _swapchain_image_count; ++i ) {
		VkImageViewCreateInfo image_view_create_info {};
		image_view_create_info.sType				= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.image				= _swapchain_images[ i ];
		image_view_create_info.viewType				= VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.format				= _surface_format.format;
		image_view_create_info.components.r			= VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.g			= VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.b			= VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.a			= VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.subresourceRange.aspectMask			= VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel		= 0;
		image_view_create_info.subresourceRange.levelCount			= 1;
		image_view_create_info.subresourceRange.baseArrayLayer		= 0;
		image_view_create_info.subresourceRange.layerCount			= 1;

		ErrorCheck( vkCreateImageView( _renderer->GetVulkanDevice(), &image_view_create_info, nullptr, &_swapchain_image_views[ i ] ) );
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, _swapchain_image_views[ i ], "_swapchain_image_views[ i ]");
	}
}

void Window::_DeInitSwapchainImages()
{
	for( auto view : _swapchain_image_views ) {
		vkDestroyImageView( _renderer->GetVulkanDevice(), view, nullptr );
	}
}

void Window::_InitDepthStencilImage()
{
	{
		std::vector<VkFormat> try_formats {
			//VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT
			//VK_FORMAT_D16_UNORM_S8_UINT,
			//VK_FORMAT_D32_SFLOAT,
			//VK_FORMAT_D16_UNORM
		};
		for( auto f : try_formats ) {
			VkFormatProperties format_properties {};
			vkGetPhysicalDeviceFormatProperties( _renderer->GetVulkanPhysicalDevice(), f, &format_properties );
			if( format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
				_depth_stencil_format = f;
				break;
			}
		}
		if( _depth_stencil_format == VK_FORMAT_UNDEFINED ) {
			assert( 0 && "Depth stencil format not selected." );
			std::exit( -1 );
		}
    /*
		if( ( _depth_stencil_format == VK_FORMAT_D32_SFLOAT_S8_UINT ) ||
			( _depth_stencil_format == VK_FORMAT_D24_UNORM_S8_UINT ) ||
			( _depth_stencil_format == VK_FORMAT_D16_UNORM_S8_UINT ) ||
			( _depth_stencil_format == VK_FORMAT_S8_UINT ) ) {
      */
    if(_depth_stencil_format == VK_FORMAT_D24_UNORM_S8_UINT){
			_stencil_available				= true;
		}
	}

	VkImageCreateInfo image_create_info {};
	image_create_info.sType					= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.flags					= 0;
	image_create_info.imageType				= VK_IMAGE_TYPE_2D;
	image_create_info.format				= _depth_stencil_format;
	image_create_info.extent.width			= _surface_size_x;
	image_create_info.extent.height			= _surface_size_y;
	image_create_info.extent.depth			= 1;
	image_create_info.mipLevels				= 1;
	image_create_info.arrayLayers			= 1;
	image_create_info.samples				= VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling				= VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage					= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	image_create_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.queueFamilyIndexCount	= VK_QUEUE_FAMILY_IGNORED;
	image_create_info.pQueueFamilyIndices	= nullptr;
	image_create_info.initialLayout			= VK_IMAGE_LAYOUT_UNDEFINED;

	ErrorCheck( vkCreateImage( _renderer->GetVulkanDevice(), &image_create_info, nullptr, &_depth_stencil_image ) );
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_IMAGE, _depth_stencil_image, "_depth_stencil_image");

	VkMemoryRequirements image_memory_requirements {};
	vkGetImageMemoryRequirements( _renderer->GetVulkanDevice(), _depth_stencil_image, &image_memory_requirements );

	uint32_t memory_index					= FindMemoryTypeIndex( &_renderer->GetVulkanPhysicalDeviceMemoryProperties(), &image_memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	VkMemoryAllocateInfo memory_allocate_info {};
	memory_allocate_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize		= image_memory_requirements.size;
	memory_allocate_info.memoryTypeIndex	= memory_index;

	ErrorCheck( vkAllocateMemory( _renderer->GetVulkanDevice(), &memory_allocate_info, nullptr, &_depth_stencil_image_memory ) );
	ErrorCheck( vkBindImageMemory( _renderer->GetVulkanDevice(), _depth_stencil_image, _depth_stencil_image_memory, 0 ) );

	VkImageViewCreateInfo image_view_create_info {};
	image_view_create_info.sType				= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image				= _depth_stencil_image;
	image_view_create_info.viewType				= VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format				= _depth_stencil_format;
	image_view_create_info.components.r			= VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.g			= VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.b			= VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.components.a			= VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create_info.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_DEPTH_BIT | ( _stencil_available ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 );
	image_view_create_info.subresourceRange.baseMipLevel	= 0;
	image_view_create_info.subresourceRange.levelCount		= 1;
	image_view_create_info.subresourceRange.baseArrayLayer	= 0;
	image_view_create_info.subresourceRange.layerCount		= 1;

	ErrorCheck( vkCreateImageView( _renderer->GetVulkanDevice(), &image_view_create_info, nullptr, &_depth_stencil_image_view ) );
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, _depth_stencil_image_view, "_depth_stencil_image_view");
}

void Window::_DeInitDepthStencilImage()
{
	vkDestroyImageView( _renderer->GetVulkanDevice(), _depth_stencil_image_view, nullptr );
	vkFreeMemory( _renderer->GetVulkanDevice(), _depth_stencil_image_memory, nullptr );
	vkDestroyImage( _renderer->GetVulkanDevice(), _depth_stencil_image, nullptr );
}

void Window::_InitRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments {};

  attachments[0].flags = 0;
  attachments[0].format = _surface_format.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  /* Layout state machine for the swapchain image (was PRESENT_SRC at the end of every pass, with the
     "keep" passes starting from UNDEFINED + LOAD, which on a tile-based GPU means "contents garbage").
     main pass : UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL   (CLEAR, so UNDEFINED is legal)
     keep pass : COLOR_ATTACHMENT_OPTIMAL -> same        (LOAD, so the real layout must be declared)
     The one transition to PRESENT_SRC_KHR is recorded at the end of the frame (Window::RecordPresentTransition). */
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


	attachments[ 1 ].flags						= 0;
	attachments[ 1 ].format						= _depth_stencil_format;
	attachments[ 1 ].samples					= VK_SAMPLE_COUNT_1_BIT;
	attachments[ 1 ].loadOp						= VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[ 1 ].storeOp					= VK_ATTACHMENT_STORE_OP_DONT_CARE;
	/* Pipelines drawn into this pass have stencilTestEnable = TRUE (VdpPipeline.cpp), so the stencil must start
	   defined: CLEAR (value 0 comes from the begin info), not DONT_CARE = "garbage" on a tile-based GPU. */
	attachments[ 1 ].stencilLoadOp				= VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[ 1 ].stencilStoreOp				= VK_ATTACHMENT_STORE_OP_STORE;
	attachments[ 1 ].initialLayout				= VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[ 1 ].finalLayout				= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;



	VkAttachmentReference sub_pass_0_depth_stencil_attachment {};
	sub_pass_0_depth_stencil_attachment.attachment	= 1;
	sub_pass_0_depth_stencil_attachment.layout		= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	std::array<VkAttachmentReference, 1> sub_pass_0_color_attachments {};
	sub_pass_0_color_attachments[ 0 ].attachment	= 0;
	sub_pass_0_color_attachments[ 0 ].layout		= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	std::array<VkSubpassDescription, 1> sub_passes {};
	sub_passes[ 0 ].pipelineBindPoint			= VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub_passes[ 0 ].colorAttachmentCount		= sub_pass_0_color_attachments.size();
	sub_passes[ 0 ].pColorAttachments			= sub_pass_0_color_attachments.data();		// layout(location=0) out vec4 FinalColor;
	sub_passes[ 0 ].pDepthStencilAttachment		= &sub_pass_0_depth_stencil_attachment;


	/* Subpass dependencies (R4): shared with the sub render target's pass; see RenderPassDeps.h for why they must match. */
	auto deps = YabaSceneSubpassDependencies();

	VkRenderPassCreateInfo render_pass_create_info {};
	render_pass_create_info.sType				= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount		= attachments.size();
	render_pass_create_info.pAttachments		= attachments.data();
	render_pass_create_info.subpassCount		= sub_passes.size();
	render_pass_create_info.pSubpasses			= sub_passes.data();
	render_pass_create_info.dependencyCount		= deps.size();
	render_pass_create_info.pDependencies		= deps.data();

	ErrorCheck( vkCreateRenderPass( _renderer->GetVulkanDevice(), &render_pass_create_info, nullptr, &_render_pass ) );
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_RENDER_PASS, _render_pass, "_render_pass");

  /* keep pass: resumes drawing on what the previous pass left, so it must declare the layout that
     pass left (R1/R3). Depth/stencil likewise: LOAD with UNDEFINED means "garbage". */
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  ErrorCheck(vkCreateRenderPass(_renderer->GetVulkanDevice(), &render_pass_create_info, nullptr, &_render_pass_keep));
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_RENDER_PASS, _render_pass_keep, "_render_pass_keep");

}

void Window::_DeInitRenderPass()
{
	vkDestroyRenderPass( _renderer->GetVulkanDevice(), _render_pass, nullptr );
  vkDestroyRenderPass(_renderer->GetVulkanDevice(), _render_pass_keep, nullptr);
}

void Window::_InitFramebuffers()
{
	_framebuffers.resize( _swapchain_image_count );
	for( uint32_t i=0; i < _swapchain_image_count; ++i ) {
		std::array<VkImageView, 2> attachments {};
    attachments[0] = _swapchain_image_views[i];
		attachments[ 1 ]	= _depth_stencil_image_view;

		VkFramebufferCreateInfo framebuffer_create_info {};
		framebuffer_create_info.sType			= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass		= _render_pass;
		framebuffer_create_info.attachmentCount	= attachments.size();
		framebuffer_create_info.pAttachments	= attachments.data();
		framebuffer_create_info.width			= _surface_size_x;
		framebuffer_create_info.height			= _surface_size_y;
		framebuffer_create_info.layers			= 1;

		ErrorCheck( vkCreateFramebuffer( _renderer->GetVulkanDevice(), &framebuffer_create_info, nullptr, &_framebuffers[ i ] ) );
  YABA_VK_NAME(_renderer->GetVulkanDevice(), VK_OBJECT_TYPE_FRAMEBUFFER, _framebuffers[ i ], "_framebuffers[ i ]");
	}
}

void Window::_DeInitFramebuffers()
{
	for( auto f : _framebuffers ) {
		vkDestroyFramebuffer( _renderer->GetVulkanDevice(), f, nullptr );
	}
}

void Window::_InitSynchronizations()
{
	VkFenceCreateInfo fence_create_info {};
	fence_create_info.sType			= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	vkCreateFence( _renderer->GetVulkanDevice(), &fence_create_info, nullptr, &_swapchain_image_available );
}

void Window::_DeInitSynchronizations()
{
	vkDestroyFence( _renderer->GetVulkanDevice(), _swapchain_image_available, nullptr );
}
