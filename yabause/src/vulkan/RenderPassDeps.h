#pragma once
/* The three subpass dependencies shared by every colour + depth/stencil "scene" render pass: the window's main
   pass, its keep pass and the sub render target's pass.

   They must be IDENTICAL in all three: the graphics pipelines are built against one of these passes and drawn
   inside the others, and render-pass compatibility requires the dependency arrays to match
   (VUID-vkCmdDrawIndexed-renderPass-02684 reported "dependencyCount ... 3 != 2").

   [0] external -> 0 : orders this pass's loads/clears/layout transition against how the image was last touched
                       (sampled, rendered, or transferred). loadOp LOAD reads the colour attachment and CLEAR/LOAD touch
                       depth/stencil at EARLY_FRAGMENT_TESTS, so those accesses must be in the destination mask.
   [1] 0 -> external : makes this pass's writes visible to whatever follows (sampling, transfer, the next pass).
   [2] 0 -> 0        : self dependency, allows vkCmdPipelineBarrier inside the pass (the pipelines use input/GENERAL). */
#include <vulkan/vulkan.h>
#include <array>

static inline std::array<VkSubpassDependency, 3> YabaSceneSubpassDependencies()
{
  const VkPipelineStageFlags att = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  const VkAccessFlags attAcc = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  std::array<VkSubpassDependency, 3> d = {};

  d[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  d[0].dstSubpass = 0;
  d[0].srcStageMask = att | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  d[0].dstStageMask = att;
  d[0].srcAccessMask = attAcc | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  d[0].dstAccessMask = attAcc;
  d[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  d[1].srcSubpass = 0;
  d[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  d[1].srcStageMask = att;
  d[1].dstStageMask = att | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  d[1].srcAccessMask = attAcc;
  d[1].dstAccessMask = attAcc | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  d[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  d[2].srcSubpass = 0;
  d[2].dstSubpass = 0;
  d[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  d[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  d[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  d[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  d[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  return d;
}
