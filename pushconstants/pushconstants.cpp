

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
//#define USE_GLSL
#define ENABLE_VALIDATION false

// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_UV,
	vkMeshLoader::VERTEX_LAYOUT_COLOR
};

class VulkanExample : public VulkanExampleBase
{
public:
	bool animate = true;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer scene;
	} meshes;

	struct {
		vkTools::UniformData vertexShader;
	} uniformData;
	
	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0, 0.0, -2.0, 1.0);
	} uboVS;

	struct {
		VkPipeline solid;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	// This array holds the light positions
	// and will be updated via a push constant
	std::array<glm::vec4, 6> pushConstants;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		width = 1280;
		height = 720;
		zoom = -30.0;
		zoomSpeed = 2.5f;
		rotationSpeed = 0.5f;
		timerSpeed *= 0.5f;
		rotation = { -32.5, 45.0, 0.0 };
		title = "Vulkan Example - Push constants";

		// Check requested push constant size against hardware limit
		// Specs require 128 bytes, so if the device complies our 
		// push constant buffer should always fit into memory
		VkPhysicalDeviceProperties deviceProps;
		vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
		assert(sizeof(pushConstants) <= deviceProps.limits.maxPushConstantsSize);
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.solid, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.scene);

		vkTools::destroyUniformData(device, &uniformData.vertexShader);
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		VkResult err;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			err = vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo);
			assert(!err);

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::initializers::viewport(
				(float)width,
				(float)height,
				0.0f,
				1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::initializers::rect2D(
				width,
				height,
				0,
				0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			// Update light positions
			// w component = light radius scale
#define r 7.5f
#define sin_t sin(deg_to_rad(timer * 360))
#define cos_t cos(deg_to_rad(timer * 360))
#define y -4.0f
			pushConstants[0] = glm::vec4(r * 1.1 * sin_t, y, r * 1.1 * cos_t, 1.0f);
			pushConstants[1] = glm::vec4(-r * sin_t, y, -r * cos_t, 1.0f);
			pushConstants[2] = glm::vec4(r * 0.85f * sin_t, y, -sin_t * 2.5f, 1.5f);
			pushConstants[3] = glm::vec4(0.0f, y, r * 1.25f * cos_t, 1.5f);
			pushConstants[4] = glm::vec4(r * 2.25f * cos_t, y, 0.0f, 1.25f);
			pushConstants[5] = glm::vec4(r * 2.5f * cos(deg_to_rad(timer * 360)), y, r * 2.5f * sin_t, 1.25f);
#undef r
#undef y
#undef sin_t
#undef cos_t

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);

			// Submit via push constant (rather than a UBO)
			vkCmdPushConstants(
				drawCmdBuffers[i],
				pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,
				VK_FLAGS_NONE,
				sizeof(pushConstants),
				pushConstants.data());

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.scene.vertices.buf, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], meshes.scene.indices.buf, 0, VK_INDEX_TYPE_UINT32);

			vkCmdDrawIndexed(drawCmdBuffers[i], meshes.scene.indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VkImageMemoryBarrier prePresentBarrier = vkTools::prePresentBarrier(swapChain.buffers[i].image);
			vkCmdPipelineBarrier(
				drawCmdBuffers[i],
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_FLAGS_NONE, 
				0, nullptr,
				0, nullptr,
				1, &prePresentBarrier);

			err = vkEndCommandBuffer(drawCmdBuffers[i]);
			assert(!err);
		}
	}

	void draw()
	{
		VkResult err;
		VkSemaphore presentCompleteSemaphore;
		VkSemaphoreCreateInfo presentCompleteSemaphoreCreateInfo =
			vkTools::initializers::semaphoreCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);

		err = vkCreateSemaphore(device, &presentCompleteSemaphoreCreateInfo, nullptr, &presentCompleteSemaphore);
		assert(!err);

		// Get next image in the swap chain (back/front buffer)
		err = swapChain.acquireNextImage(presentCompleteSemaphore, &currentBuffer);
		assert(!err);

		err = vkQueueWaitIdle(queue);
		assert(!err);

		// Gather command buffers to be sumitted to the queue
		std::vector<VkCommandBuffer> submitCmdBuffers = {
			drawCmdBuffers[currentBuffer]
		};

		VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphore;
		submitInfo.commandBufferCount = submitCmdBuffers.size();
		submitInfo.pCommandBuffers = submitCmdBuffers.data();

		// Submit draw command buffer
		err = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
		assert(!err);

		err = swapChain.queuePresent(queue, currentBuffer);
		assert(!err);

		vkDestroySemaphore(device, presentCompleteSemaphore, nullptr);

		submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

		err = vkQueueWaitIdle(queue);
		assert(!err);
	}

	void loadMeshes()
	{
		loadMesh("./../data/models/samplescene.X", &meshes.scene, vertexLayout, 0.35f);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				vkMeshLoader::vertexSize(vertexLayout),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);
		// Location 1 : Normal
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6);
		// Location 3 : Color
		vertices.attributeDescriptions[3] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8);

		vertices.inputState = vkTools::initializers::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo 
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				2);

		VkResult vkRes = vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);
		assert(!vkRes);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				setLayoutBindings.size());

		VkResult err = vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout);
		assert(!err);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		// Define push constant
		// Example uses six light positions as push constants
		// 6 * 4 * 4 = 96 bytes
		// Spec requires a minimum of 128 bytes, bigger values
		// need to be checked against maxPushConstantsSize
		// But even at only 128 bytes, lots of stuff can fit 
		// inside push constants
		VkPushConstantRange pushConstantRange =
			vkTools::initializers::pushConstantRange(
				VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
				sizeof(pushConstants),
				0);

		// Push constant ranges are part of the pipeline layout
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		err = vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);
		assert(!err);
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VkResult vkRes = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
		assert(!vkRes);

		// Binding 0 : Vertex shader uniform buffer
		VkWriteDescriptorSet writeDescriptorSet =
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.vertexShader.descriptor);

		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		// Solid rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

#ifdef USE_GLSL
		shaderStages[0] = loadShaderGLSL("./../data/shaders/pushconstants/lights.vert", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShaderGLSL("./../data/shaders/pushconstants/lights.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
#else
		shaderStages[0] = loadShader("./../data/shaders/pushconstants/lights.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader("./../data/shaders/pushconstants/lights.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
#endif

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VkResult err = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid);
		assert(!err);
	}

	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			sizeof(uboVS),
			&uboVS,
			&uniformData.vertexShader.buffer,
			&uniformData.vertexShader.memory,
			&uniformData.vertexShader.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Vertex shader
		glm::mat4 viewMatrix = glm::mat4();
		uboVS.projection = glm::perspective(deg_to_rad(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 2.0f, zoom));

		float offset = 0.5f;
		int uboIndex = 1;
		uboVS.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
		uboVS.model = glm::rotate(uboVS.model, deg_to_rad(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, deg_to_rad(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, deg_to_rad(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uint8_t *pData;
		VkResult err = vkMapMemory(device, uniformData.vertexShader.memory, 0, sizeof(uboVS), 0, (void **)&pData);
		assert(!err);
		memcpy(pData, &uboVS, sizeof(uboVS));
		vkUnmapMemory(device, uniformData.vertexShader.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(device);
		draw();
		vkDeviceWaitIdle(device);
		if (!paused)
		{
			reBuildCommandBuffers();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

	void toggleAnimation()
	{
		animate = !animate;
	}
};

VulkanExample *vulkanExample;

#ifdef _WIN32

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
		if (uMsg == WM_KEYDOWN)
		{
			switch (wParam)
			{
			case 0x41:
				vulkanExample->toggleAnimation();
				break;
			}
		}
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

#else 

static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
#endif

#ifdef _WIN32
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
#else
int main(const int argc, const char *argv[])
#endif
{
	vulkanExample = new VulkanExample();
#ifdef _WIN32
	vulkanExample->setupWindow(hInstance, WndProc);
#else
	vulkanExample->setupWindow();
#endif
	vulkanExample->initSwapchain();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}