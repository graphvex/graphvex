#include "vex.h"
#include "compositor/gpu_scope.h"
#include "vulkan/sampled_image.h"
#include "filter/filter_functions.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

;;DEFINITION
/* Explicit Vulkan scoped groups: upload borrowed straight sRGB source images,
 * isolate groups on the GPU, splat each source texel's weighted premultiplied
 * color into expanded f32 attachments with additive blending, then compose
 * the resulting scope into an sRGB target. Only byte transport crosses the CPU;
 * no CPU filter/color/composition algorithm or fallback exists here. The sampled
 * output keeps the final texture on GPU; readback is an explicit numeric bridge.
 * Both cold forms are synchronous and bounded; a timeout retains its entire job
 * until the fence signals, so neither retry nor destruction frees live work. */
;;OVERVIEW
/* CLASS: GpuScope. STRUCT FIELDS: device (borrowed VkDevice), physical (borrowed
 * physical GPU), queue (borrowed graphics queue), maxPixels (configured work
 * budget), maxExtent (probed image/viewport bound), scatterRender (owned 2-f32
 * attachment pass), groupRender (owned f32 pass), finalRender (owned sRGB
 * pass), scatterDescriptor (owned single-sampler layout), scopeDescriptor
 * (owned four-sampler layout), scatterLayout (owned 24-byte push layout),
 * scopeLayout (owned 36-byte push layout), scatterPipeline/groupPipeline/
 * finalPipeline (owned pipelines), sampler (owned nearest sampler), commands
 * (owned resettable pool), command (borrowed pool buffer), fence (owned),
 * pending (submitted job flag), job (owned dumb slot record).
 * SLOT RECORD Texture: image, memory, view (owned handles), width/height/format.
 * SLOT RECORD Buffer: buffer, memory, size (owned transfer allocation).
 * SLOT RECORD Job: textures[7] (3 inputs/group/accumulation/weight/output),
 * buffers[4] (3 uploads/readback), frames[3] (group/scatter/final), descriptors
 * (owned pool), sets[3] (borrowed pool sets). Counts are the fixed shader DAG,
 * not scene entity limits. Public: _0/_3/chooser/zero, render, pending query,
 * destroy, renderSampled and bounded projections. owner (borrowed Device) is
 * the final field after job; sampled Images outlive the scope, never the Device.
 * Private: render dispatch, Vulkan allocation, render-pass/
 * pipeline setup, descriptor/barrier/record and retirement helpers.
 * Cold rejection reports once, preserves outputs. destroy false on unsignaled
 * fence is silent to preserve bounded teardown; state remains owned for retry. */
enum { PRIOR, DECORATION, FOREGROUND, GROUP, ACCUMULATION, WEIGHT, OUTPUT, TEXTURE_COUNT };
enum { UPLOAD_PRIOR, UPLOAD_DECORATION, UPLOAD_FOREGROUND, READBACK, BUFFER_COUNT };
enum { GROUP_FRAME, SCATTER_FRAME, FINAL_FRAME, FRAME_COUNT };
static const u64 GPU_WAIT_NS = UINT64_C(100000000);
/* Security bound for externally supplied shader blobs, not an entity ceiling. */
static const i64 MAX_SHADER_BYTES = 16L*1024*1024;
typedef struct Texture {
    VkImage image; VkDeviceMemory memory; VkImageView view;
    u32 width, height; VkFormat format;
} Texture;
typedef struct Buffer { VkBuffer buffer; VkDeviceMemory memory; VkDeviceSize size; } Buffer;
typedef struct Job {
    Texture textures[TEXTURE_COUNT]; Buffer buffers[BUFFER_COUNT];
    VkFramebuffer frames[FRAME_COUNT]; VkDescriptorPool descriptors;
    VkDescriptorSet sets[FRAME_COUNT];
} Job;
struct GpuScope {
    VkDevice device; VkPhysicalDevice physical; VkQueue queue;
    u32 maxPixels, maxExtent;
    VkRenderPass scatterRender, groupRender, finalRender;
    VkDescriptorSetLayout scatterDescriptor, scopeDescriptor;
    VkPipelineLayout scatterLayout, scopeLayout;
    VkPipeline scatterPipeline, groupPipeline, finalPipeline;
    VkSampler sampler; VkCommandPool commands; VkCommandBuffer command;
    VkFence fence; bool pending; Job job;
    Device *owner;
};

;;GETTER
static inline VkImage Texture_image(const Texture *self) { return (*self).image; }
;;GETTER
static inline VkImage *Texture_imageMut(Texture *self) { return &(*self).image; }
;;SETTER
static inline fn Texture_setImage(Texture *self, VkImage v) { (*self).image = v; }
;;GETTER
static inline VkDeviceMemory Texture_memory(const Texture *self) { return (*self).memory; }
;;GETTER
static inline VkDeviceMemory *Texture_memoryMut(Texture *self) { return &(*self).memory; }
;;SETTER
static inline fn Texture_setMemory(Texture *self, VkDeviceMemory v) { (*self).memory = v; }
;;GETTER
static inline VkImageView Texture_view(const Texture *self) { return (*self).view; }
;;GETTER
static inline VkImageView *Texture_viewMut(Texture *self) { return &(*self).view; }
;;GETTER
static inline u32 Texture_width(const Texture *self) { return (*self).width; }
;;SETTER
static inline fn Texture_setWidth(Texture *self, u32 v) { (*self).width = v; }
;;GETTER
static inline u32 Texture_height(const Texture *self) { return (*self).height; }
;;SETTER
static inline fn Texture_setHeight(Texture *self, u32 v) { (*self).height = v; }
;;SETTER
static inline fn Texture_setFormat(Texture *self, VkFormat v) { (*self).format = v; }

;;GETTER
static inline VkBuffer Buffer_buffer(const Buffer *self) { return (*self).buffer; }
;;GETTER
static inline VkBuffer *Buffer_bufferMut(Buffer *self) { return &(*self).buffer; }
;;GETTER
static inline VkDeviceMemory Buffer_memory(const Buffer *self) { return (*self).memory; }
;;GETTER
static inline VkDeviceMemory *Buffer_memoryMut(Buffer *self) { return &(*self).memory; }
;;GETTER
static inline VkDeviceSize Buffer_size(const Buffer *self) { return (*self).size; }
;;SETTER
static inline fn Buffer_setSize(Buffer *self, VkDeviceSize v) { (*self).size = v; }

;;GETTER
static inline Texture *Job_texturesMut(Job *self, u32 i) { return &(*self).textures[i]; }
;;GETTER
static inline Buffer *Job_buffersMut(Job *self, u32 i) { return &(*self).buffers[i]; }
;;GETTER
static inline VkFramebuffer Job_frames(const Job *self, u32 i) { return (*self).frames[i]; }
;;GETTER
static inline VkFramebuffer *Job_framesMut(Job *self, u32 i) { return &(*self).frames[i]; }
;;GETTER
static inline VkDescriptorPool Job_descriptors(const Job *self) { return (*self).descriptors; }
;;GETTER
static inline VkDescriptorPool *Job_descriptorsMut(Job *self) { return &(*self).descriptors; }
;;GETTER
static inline VkDescriptorSet Job_sets(const Job *self, u32 i) { return (*self).sets[i]; }
;;GETTER
static inline VkDescriptorSet *Job_setsMut(Job *self) { return (*self).sets; }

;;GETTER
static inline VkDevice GpuScope_device(const GpuScope *self) { return (*self).device; }
;;SETTER
static inline fn GpuScope_setDevice(GpuScope *self, VkDevice v) { (*self).device = v; }
;;GETTER
static inline VkPhysicalDevice GpuScope_physical(const GpuScope *self) { return (*self).physical; }
;;SETTER
static inline fn GpuScope_setPhysical(GpuScope *self, VkPhysicalDevice v) { (*self).physical = v; }
;;GETTER
static inline VkQueue GpuScope_queue(const GpuScope *self) { return (*self).queue; }
;;SETTER
static inline fn GpuScope_setQueue(GpuScope *self, VkQueue v) { (*self).queue = v; }
;;GETTER
static inline u32 GpuScope_maxPixels(const GpuScope *self) { return (*self).maxPixels; }
;;SETTER
static inline fn GpuScope_setMaxPixels(GpuScope *self, u32 v) { (*self).maxPixels = v; }
;;GETTER
static inline u32 GpuScope_maxExtent(const GpuScope *self) { return (*self).maxExtent; }
;;SETTER
static inline fn GpuScope_setMaxExtent(GpuScope *self, u32 v) { (*self).maxExtent = v; }
;;GETTER
static inline VkRenderPass GpuScope_scatterRender(const GpuScope *self) { return (*self).scatterRender; }
;;GETTER
static inline VkRenderPass *GpuScope_scatterRenderMut(GpuScope *self) { return &(*self).scatterRender; }
;;GETTER
static inline VkRenderPass GpuScope_groupRender(const GpuScope *self) { return (*self).groupRender; }
;;GETTER
static inline VkRenderPass *GpuScope_groupRenderMut(GpuScope *self) { return &(*self).groupRender; }
;;GETTER
static inline VkRenderPass GpuScope_finalRender(const GpuScope *self) { return (*self).finalRender; }
;;GETTER
static inline VkRenderPass *GpuScope_finalRenderMut(GpuScope *self) { return &(*self).finalRender; }
;;GETTER
static inline VkDescriptorSetLayout GpuScope_scatterDescriptor(const GpuScope *self) { return (*self).scatterDescriptor; }
;;GETTER
static inline VkDescriptorSetLayout *GpuScope_scatterDescriptorMut(GpuScope *self) { return &(*self).scatterDescriptor; }
;;GETTER
static inline VkDescriptorSetLayout GpuScope_scopeDescriptor(const GpuScope *self) { return (*self).scopeDescriptor; }
;;GETTER
static inline VkDescriptorSetLayout *GpuScope_scopeDescriptorMut(GpuScope *self) { return &(*self).scopeDescriptor; }
;;GETTER
static inline VkPipelineLayout GpuScope_scatterLayout(const GpuScope *self) { return (*self).scatterLayout; }
;;GETTER
static inline VkPipelineLayout *GpuScope_scatterLayoutMut(GpuScope *self) { return &(*self).scatterLayout; }
;;GETTER
static inline VkPipelineLayout GpuScope_scopeLayout(const GpuScope *self) { return (*self).scopeLayout; }
;;GETTER
static inline VkPipelineLayout *GpuScope_scopeLayoutMut(GpuScope *self) { return &(*self).scopeLayout; }
;;GETTER
static inline VkPipeline GpuScope_scatterPipeline(const GpuScope *self) { return (*self).scatterPipeline; }
;;GETTER
static inline VkPipeline *GpuScope_scatterPipelineMut(GpuScope *self) { return &(*self).scatterPipeline; }
;;GETTER
static inline VkPipeline GpuScope_groupPipeline(const GpuScope *self) { return (*self).groupPipeline; }
;;GETTER
static inline VkPipeline *GpuScope_groupPipelineMut(GpuScope *self) { return &(*self).groupPipeline; }
;;GETTER
static inline VkPipeline GpuScope_finalPipeline(const GpuScope *self) { return (*self).finalPipeline; }
;;GETTER
static inline VkPipeline *GpuScope_finalPipelineMut(GpuScope *self) { return &(*self).finalPipeline; }
;;GETTER
static inline VkSampler GpuScope_sampler(const GpuScope *self) { return (*self).sampler; }
;;GETTER
static inline VkSampler *GpuScope_samplerMut(GpuScope *self) { return &(*self).sampler; }
;;GETTER
static inline VkCommandPool GpuScope_commands(const GpuScope *self) { return (*self).commands; }
;;GETTER
static inline VkCommandPool *GpuScope_commandsMut(GpuScope *self) { return &(*self).commands; }
;;GETTER
static inline VkCommandBuffer GpuScope_command(const GpuScope *self) { return (*self).command; }
;;GETTER
static inline VkCommandBuffer *GpuScope_commandMut(GpuScope *self) { return &(*self).command; }
;;GETTER
static inline VkFence GpuScope_fence(const GpuScope *self) { return (*self).fence; }
;;GETTER
static inline VkFence *GpuScope_fenceMut(GpuScope *self) { return &(*self).fence; }
;;GETTER
static inline bool GpuScope_pending(const GpuScope *self) { return (*self).pending; }
;;SETTER
static inline fn GpuScope_setPending(GpuScope *self, bool v) { (*self).pending = v; }
;;GETTER
static inline Job *GpuScope_jobMut(GpuScope *self) { return &(*self).job; }
;;GETTER
static inline Device *GpuScope_owner(const GpuScope *self) { return (*self).owner; }
;;SETTER
static inline fn GpuScope_setOwner(GpuScope *self, Device *v) { (*self).owner = v; }

// Finds a device-local memory type supported by the supplied Vulkan requirements.
static u32 memoryType(GpuScope *self,u32 bits,VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(GpuScope_physical(self),&properties);
    for (u32 i=0;i<properties.memoryTypeCount;++i) {
        if ((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&want)==want)
            return i;
    }
    return UINT32_MAX;
}
// Creates and binds a Vulkan buffer with memory selected for its requested usage.
static bool makeBuffer(GpuScope *self,VkDeviceSize size,VkBufferUsageFlags usage,Buffer *out) {
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    if (vkCreateBuffer(GpuScope_device(self),&info,nullptr,Buffer_bufferMut(out))!=VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(GpuScope_device(self),Buffer_buffer(out),&requirements);
    u32 type=memoryType(self,requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type==UINT32_MAX)
        return false;
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=type};
    Buffer_setSize(out,size);
    return vkAllocateMemory(GpuScope_device(self),&allocate,nullptr,Buffer_memoryMut(out))==VK_SUCCESS &&
        vkBindBufferMemory(GpuScope_device(self),Buffer_buffer(out),Buffer_memory(out),0)==VK_SUCCESS;
}
// Creates a sampled/color-attachment texture and its image view.
static bool makeTexture(GpuScope *self,u32 width,u32 height,VkFormat format,Texture *out) {
    Texture_setWidth(out,width); Texture_setHeight(out,height); Texture_setFormat(out,format);
    VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags=format==VK_FORMAT_R8G8B8A8_SRGB ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},.mipLevels=1,
        .arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    if (vkCreateImage(GpuScope_device(self),&info,nullptr,Texture_imageMut(out))!=VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(GpuScope_device(self),Texture_image(out),&requirements);
    u32 type=memoryType(self,requirements.memoryTypeBits,0);
    if (type==UINT32_MAX)
        return false;
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=type};
    if (vkAllocateMemory(GpuScope_device(self),&allocate,nullptr,Texture_memoryMut(out))!=VK_SUCCESS ||
        vkBindImageMemory(GpuScope_device(self),Texture_image(out),Texture_memory(out),0)!=VK_SUCCESS)
        return false;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=Texture_image(out),.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView(GpuScope_device(self),&view,nullptr,Texture_viewMut(out))==VK_SUCCESS;
}
// Releases per-job textures, descriptors, framebuffers, and command resources.
static fn releaseJob(GpuScope *self) {
    Job *job=GpuScope_jobMut(self);
    VkDevice dev=GpuScope_device(self);
    if (Job_descriptors(job))
        vkDestroyDescriptorPool(dev,Job_descriptors(job),nullptr);
    for (u32 i=0;i<FRAME_COUNT;++i)
        if (Job_frames(job,i))
            vkDestroyFramebuffer(dev,Job_frames(job,i),nullptr);
    for (u32 i=0;i<TEXTURE_COUNT;++i) {
        Texture *t=Job_texturesMut(job,i);
        if (Texture_view(t))
            vkDestroyImageView(dev,Texture_view(t),nullptr);
        if (Texture_image(t))
            vkDestroyImage(dev,Texture_image(t),nullptr);
        if (Texture_memory(t))
            vkFreeMemory(dev,Texture_memory(t),nullptr);
    }
    for (u32 i=0;i<BUFFER_COUNT;++i) {
        Buffer *b=Job_buffersMut(job,i);
        if (Buffer_buffer(b))
            vkDestroyBuffer(dev,Buffer_buffer(b),nullptr);
        if (Buffer_memory(b))
            vkFreeMemory(dev,Buffer_memory(b),nullptr);
    }
    memset(job,0,sizeof *job);
}
// Waits for the submitted job within the bounded fence budget before releasing it.
static bool retire(GpuScope *self) {
    if (GpuScope_pending(self)) {
        if (vkWaitForFences(GpuScope_device(self),1,GpuScope_fenceMut(self),VK_TRUE,GPU_WAIT_NS)!=VK_SUCCESS)
            return false;
        GpuScope_setPending(self,false);
    }
    releaseJob(self);
    return true;
}
// Reports whether this scope currently has a submitted job pending completion.
bool GpuScope_isPending(const GpuScope *self) { return self && GpuScope_pending(self); }
GpuScope *GpuScope_0() { return nullptr; }
GpuScope *GpuScope_zero() { return GpuScope_0(); }
// Destroys scope-owned Vulkan state after retiring pending work; false preserves it on timeout.
bool GpuScope_destroy(GpuScope *self) {
    if (!self)
        return true;
    if (!retire(self))
        return false;
    VkDevice d=GpuScope_device(self);
    if (GpuScope_fence(self))
        vkDestroyFence(d,GpuScope_fence(self),nullptr);
    if (GpuScope_commands(self))
        vkDestroyCommandPool(d,GpuScope_commands(self),nullptr);
    if (GpuScope_sampler(self))
        vkDestroySampler(d,GpuScope_sampler(self),nullptr);
    if (GpuScope_scatterPipeline(self))
        vkDestroyPipeline(d,GpuScope_scatterPipeline(self),nullptr);
    if (GpuScope_groupPipeline(self))
        vkDestroyPipeline(d,GpuScope_groupPipeline(self),nullptr);
    if (GpuScope_finalPipeline(self))
        vkDestroyPipeline(d,GpuScope_finalPipeline(self),nullptr);
    if (GpuScope_scatterLayout(self))
        vkDestroyPipelineLayout(d,GpuScope_scatterLayout(self),nullptr);
    if (GpuScope_scopeLayout(self))
        vkDestroyPipelineLayout(d,GpuScope_scopeLayout(self),nullptr);
    if (GpuScope_scatterDescriptor(self))
        vkDestroyDescriptorSetLayout(d,GpuScope_scatterDescriptor(self),nullptr);
    if (GpuScope_scopeDescriptor(self))
        vkDestroyDescriptorSetLayout(d,GpuScope_scopeDescriptor(self),nullptr);
    if (GpuScope_scatterRender(self))
        vkDestroyRenderPass(d,GpuScope_scatterRender(self),nullptr);
    if (GpuScope_groupRender(self))
        vkDestroyRenderPass(d,GpuScope_groupRender(self),nullptr);
    if (GpuScope_finalRender(self))
        vkDestroyRenderPass(d,GpuScope_finalRender(self),nullptr);
    free(self); return true;
}
// Creates a render pass with the requested color attachment count and format.
static bool renderPass(GpuScope *self,VkFormat format,u32 count,VkRenderPass *out) {
    VkAttachmentDescription attachments[2]={0};
    VkAttachmentReference references[2]={0};
    for (u32 i=0;i<count;++i) {
        attachments[i]=(VkAttachmentDescription){.format=format,.samples=VK_SAMPLE_COUNT_1_BIT,
            .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        references[i]=(VkAttachmentReference){i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=count,.pColorAttachments=references};
    VkSubpassDependency dependency={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
        .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
    VkRenderPassCreateInfo info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=count,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass,
        .dependencyCount=1,.pDependencies=&dependency};
    return vkCreateRenderPass(GpuScope_device(self),&info,nullptr,out)==VK_SUCCESS;
}
// Loads a SPIR-V module from the requested shader directory and name.
static VkShaderModule loadShader(GpuScope *self,const char *directory,const char *name) {
    bytes n=strlen(directory), m=strlen(name);
    if (n>SIZE_MAX-m-6)
        return VK_NULL_HANDLE;
    char *path=malloc(n+m+6);
    if (!path)
        return VK_NULL_HANDLE;
    snprintf(path,n+m+6,"%s/%s.spv",directory,name);
    FILE *file=fopen(path,"rb"); free(path);
    if (!file)
        return VK_NULL_HANDLE;
    if (fseek(file,0,SEEK_END)) { fclose(file); return VK_NULL_HANDLE; }
    i64 length=ftell(file);
    if (length<20 || length%4 || length>MAX_SHADER_BYTES) { fclose(file); return VK_NULL_HANDLE; }
    rewind(file);
    u32 *words=malloc((bytes) length);
    if (!words) { fclose(file); return VK_NULL_HANDLE; }
    bool valid=fread(words,1,(bytes) length,file)==(bytes) length && words[0]==UINT32_C(0x07230203);
    fclose(file);
    VkShaderModule result=VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=(bytes) length,.pCode=words};
    if (valid && vkCreateShaderModule(GpuScope_device(self),&info,nullptr,&result)!=VK_SUCCESS)
        result=VK_NULL_HANDLE;
    free(words); return result;
}
// Builds a fullscreen graphics pipeline from the supplied shader modules and layouts.
static bool pipeline(GpuScope *self,VkShaderModule vertex,VkShaderModule fragment,
    VkRenderPass render,VkPipelineLayout layout,u32 count,bool additive,VkPipeline *out) {
    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend[2]={0};
    for (u32 i=0;i<count;++i)
        blend[i]=(VkPipelineColorBlendAttachmentState){.blendEnable=additive,
            .srcColorBlendFactor=VK_BLEND_FACTOR_ONE,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE,
            .colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.alphaBlendOp=VK_BLEND_OP_ADD,.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=count,.pAttachments=blend};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,
        .pColorBlendState=&cb,.pDynamicState=&dynamic,.layout=layout,.renderPass=render};
    return vkCreateGraphicsPipelines(GpuScope_device(self),VK_NULL_HANDLE,1,&info,nullptr,out)==VK_SUCCESS;
}
GpuScope *GpuScope_3(Device *device,const char *directory,u32 maxPixels) {
    if (!Device_isValid(device) || !directory || !*directory || !maxPixels || maxPixels>INT32_MAX) {
        THROW("GpuScope rejected construction inputs"); return nullptr;
    }
    GpuScope *self=calloc(1,sizeof *self);
    if (!self) { THROW("GpuScope allocation failed"); return nullptr; }
    GpuScope_setOwner(self,device);
    GpuScope_setDevice(self,(VkDevice) Device_native(device));
    GpuScope_setPhysical(self,(VkPhysicalDevice) Device_physical(device));
    GpuScope_setQueue(self,(VkQueue) Device_queue(device));
    GpuScope_setMaxPixels(self,maxPixels);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(GpuScope_physical(self),&properties);
    u32 maxExtent=properties.limits.maxImageDimension2D;
    if (properties.limits.maxFramebufferWidth<maxExtent)
        maxExtent=properties.limits.maxFramebufferWidth;
    if (properties.limits.maxFramebufferHeight<maxExtent)
        maxExtent=properties.limits.maxFramebufferHeight;
    if (properties.limits.maxViewportDimensions[0]<maxExtent)
        maxExtent=properties.limits.maxViewportDimensions[0];
    if (properties.limits.maxViewportDimensions[1]<maxExtent)
        maxExtent=properties.limits.maxViewportDimensions[1];
    if (maxExtent>(u32) INT32_MAX-32)
        maxExtent=(u32) INT32_MAX-32;
    GpuScope_setMaxExtent(self,maxExtent);
    for (u32 i=0;i<2;++i) {
        VkFormat format=i ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R32G32B32A32_SFLOAT;
        VkFormatProperties support;
        vkGetPhysicalDeviceFormatProperties(GpuScope_physical(self),format,&support);
        VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if (!i)
            required|=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
        if ((support.optimalTilingFeatures&required)!=required)
            goto failed;
    }
    if (!renderPass(self,VK_FORMAT_R32G32B32A32_SFLOAT,2,GpuScope_scatterRenderMut(self)) ||
        !renderPass(self,VK_FORMAT_R32G32B32A32_SFLOAT,1,GpuScope_groupRenderMut(self)) ||
        !renderPass(self,VK_FORMAT_R8G8B8A8_SRGB,1,GpuScope_finalRenderMut(self)))
        goto failed;
    VkDescriptorSetLayoutBinding bindings[4]={0};
    for (u32 i=0;i<4;++i)
        bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
    if (vkCreateDescriptorSetLayout(GpuScope_device(self),&ds,nullptr,GpuScope_scopeDescriptorMut(self))!=VK_SUCCESS)
        goto failed;
    bindings[0].stageFlags=VK_SHADER_STAGE_VERTEX_BIT; ds.bindingCount=1;
    if (vkCreateDescriptorSetLayout(GpuScope_device(self),&ds,nullptr,GpuScope_scatterDescriptorMut(self))!=VK_SUCCESS)
        goto failed;
    VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT,.size=36};
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=GpuScope_scopeDescriptorMut(self),.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if (vkCreatePipelineLayout(GpuScope_device(self),&pl,nullptr,GpuScope_scopeLayoutMut(self))!=VK_SUCCESS)
        goto failed;
    push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT; push.size=24; pl.pSetLayouts=GpuScope_scatterDescriptorMut(self);
    if (vkCreatePipelineLayout(GpuScope_device(self),&pl,nullptr,GpuScope_scatterLayoutMut(self))!=VK_SUCCESS)
        goto failed;
    const char *names[]={"scatter_vert","scatter_frag","resolve_vert","scope_frag"};
    VkShaderModule modules[4]={0};
    bool ready=true;
    for (u32 i=0;i<4;++i) {
        modules[i]=loadShader(self,directory,names[i]);
        if (!modules[i])
            ready=false;
    }
    if (ready)
        ready=pipeline(self,modules[0],modules[1],GpuScope_scatterRender(self),GpuScope_scatterLayout(self),2,true,GpuScope_scatterPipelineMut(self)) &&
            pipeline(self,modules[2],modules[3],GpuScope_groupRender(self),GpuScope_scopeLayout(self),1,false,GpuScope_groupPipelineMut(self)) &&
            pipeline(self,modules[2],modules[3],GpuScope_finalRender(self),GpuScope_scopeLayout(self),1,false,GpuScope_finalPipelineMut(self));
    for (u32 i=0;i<4;++i)
        if (modules[i])
            vkDestroyShaderModule(GpuScope_device(self),modules[i],nullptr);
    if (!ready)
        goto failed;
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (vkCreateSampler(GpuScope_device(self),&si,nullptr,GpuScope_samplerMut(self))!=VK_SUCCESS)
        goto failed;
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=Device_queueFamily(device)};
    if (vkCreateCommandPool(GpuScope_device(self),&cp,nullptr,GpuScope_commandsMut(self))!=VK_SUCCESS)
        goto failed;
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=GpuScope_commands(self),.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    if (vkAllocateCommandBuffers(GpuScope_device(self),&ca,GpuScope_commandMut(self))!=VK_SUCCESS)
        goto failed;
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(GpuScope_device(self),&fi,nullptr,GpuScope_fenceMut(self))!=VK_SUCCESS)
        goto failed;
    return self;
failed:
    GpuScope_destroy(self); THROW("GpuScope Vulkan setup unsupported or failed"); return nullptr;
}
// Records a texture layout transition with matching access and stage masks.
static fn barrier(GpuScope *self,Texture *texture,VkImageLayout old,VkImageLayout next,
    VkAccessFlags from,VkAccessFlags to,VkPipelineStageFlags fromStage,VkPipelineStageFlags toStage) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=from,.dstAccessMask=to,
        .oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=Texture_image(texture),.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(GpuScope_command(self),fromStage,toStage,0,0,nullptr,0,nullptr,1,&b);
}
// Uploads one CPU image into its per-job GPU texture and records visibility barriers.
static bool upload(GpuScope *self,const Image *source,u32 index) {
    Job *job=GpuScope_jobMut(self); Texture *t=Job_texturesMut(job,index); Buffer *b=Job_buffersMut(job,index);
    u32 w=Image_width(source),h=Image_height(source);
    if (!makeTexture(self,w,h,VK_FORMAT_R8G8B8A8_SRGB,t) ||
        !makeBuffer(self,(VkDeviceSize) w*h*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,b))
        return false;
    any mappedPixelBytes;
    if (vkMapMemory(GpuScope_device(self),Buffer_memory(b),0,Buffer_size(b),0,&mappedPixelBytes)!=VK_SUCCESS)
        return false;
    for (u32 y=0;y<h;++y)
        memcpy((u8*) mappedPixelBytes+(bytes) y*w*4,Image_pixels(source)+(bytes) y*Image_stride(source),(bytes) w*4);
    vkUnmapMemory(GpuScope_device(self),Buffer_memory(b));
    barrier(self,t,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyBufferToImage(GpuScope_command(self),Buffer_buffer(b),Texture_image(t),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier(self,t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return true;
}
// Creates a framebuffer using the selected contiguous range of job textures.
static bool framebuffer(GpuScope *self,u32 index,VkRenderPass render,u32 first,u32 count) {
    Job *job=GpuScope_jobMut(self); Texture *t=Job_texturesMut(job,first); VkImageView views[2];
    for (u32 i=0;i<count;++i) { Texture *v=Job_texturesMut(job,first+i); views[i]=Texture_view(v); }
    VkFramebufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=render,
        .attachmentCount=count,.pAttachments=views,.width=Texture_width(t),.height=Texture_height(t),.layers=1};
    return vkCreateFramebuffer(GpuScope_device(self),&info,nullptr,Job_framesMut(job,index))==VK_SUCCESS;
}
// Writes one sampled-texture descriptor for the selected set, binding, and job texture.
static fn descriptor(GpuScope *self,u32 set,u32 binding,u32 texture) {
    Job *job=GpuScope_jobMut(self); Texture *t=Job_texturesMut(job,texture);
    VkDescriptorImageInfo image={GpuScope_sampler(self),Texture_view(t),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=Job_sets(job,set),
        .dstBinding=binding,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image};
    vkUpdateDescriptorSets(GpuScope_device(self),1,&write,0,nullptr);
}
// Begins a render pass and binds the selected pipeline, framebuffer, and descriptors.
static fn beginPass(GpuScope *self,u32 frame,VkRenderPass render,VkPipeline pipeline,
    VkPipelineLayout layout,u32 width,u32 height,u32 attachments) {
    Job *job=GpuScope_jobMut(self);
    VkCommandBuffer cmd=GpuScope_command(self);
    VkClearValue clear[2]={0};
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=render,
        .framebuffer=Job_frames(job,frame),.renderArea={{0,0},{width,height}},.clearValueCount=attachments,.pClearValues=clear};
    VkViewport viewport={0,0,(f32) width,(f32) height,0,1}; VkRect2D scissor={{0,0},{width,height}};
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd,0,1,&viewport); vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkDescriptorSet ds=Job_sets(job,frame);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&ds,0,nullptr);
}
// Checks source image readiness and configured extent/pixel limits before recording work.
static bool inputValid(const Image *image,u32 maxPixels,u32 maxExtent) {
    u32 w=Image_width(image),h=Image_height(image);
    return image && w && h && w<=maxExtent && h<=maxExtent && (u64) w*h<=maxPixels &&
        (u64) w*h<=SIZE_MAX/4 && Image_format(image)==IMAGE_FORMAT_RGBA8 && Image_pixels(image) && Image_stride(image)>=(u64) w*4;
}
// Records and submits the selected backdrop/foreground/element scope into an output image.
static bool render(GpuScope *self,u32 scope,const Image *prior,const Image *decoration,
    i32 panelX,i32 panelY,const Image *foreground,i32 foregroundX,i32 foregroundY,u32 radius,bool sampled,Image **out) {
    if (!self || !out || scope>GPU_SCOPE_ELEMENT || radius>FILTER_SCATTER_MAX_RADIUS)
        goto rejected;
    u32 max=GpuScope_maxPixels(self),extent=GpuScope_maxExtent(self);
    if (!inputValid(prior,max,extent) || !inputValid(decoration,max,extent) || !inputValid(foreground,max,extent))
        goto rejected;
    u32 w=Image_width(prior),h=Image_height(prior),pw=Image_width(decoration),ph=Image_height(decoration);
    if (panelX<0 || panelY<0 || (u64) panelX+pw>w || (u64) panelY+ph>h ||
        (i64) foregroundX+Image_width(foreground)>INT32_MAX ||
        (i64) foregroundY+Image_height(foreground)>INT32_MAX ||
        foregroundX<INT32_MIN+(i32) w+32 || foregroundY<INT32_MIN+(i32) h+32)
        goto rejected;
    u32 sw=scope==GPU_SCOPE_FOREGROUND ? Image_width(foreground) : w;
    u32 sh=scope==GPU_SCOPE_FOREGROUND ? Image_height(foreground) : h;
    u64 aw=(u64) sw+2*radius,ah=(u64) sh+2*radius;
    if (aw>extent || ah>extent || aw*ah>max || aw*ah>SIZE_MAX/16)
        goto rejected;
    if (!retire(self))
        goto rejected;
    Job *job=GpuScope_jobMut(self);
    VkCommandBuffer cmd=GpuScope_command(self);
    VkDevice dev=GpuScope_device(self);
    if (vkResetCommandBuffer(cmd,0)!=VK_SUCCESS)
        goto failed;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd,&begin)!=VK_SUCCESS)
        goto failed;
    if (!upload(self,prior,PRIOR) || !upload(self,decoration,DECORATION) || !upload(self,foreground,FOREGROUND) ||
        !makeTexture(self,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,Job_texturesMut(job,GROUP)) ||
        !makeTexture(self,(u32) aw,(u32) ah,VK_FORMAT_R32G32B32A32_SFLOAT,Job_texturesMut(job,ACCUMULATION)) ||
        !makeTexture(self,(u32) aw,(u32) ah,VK_FORMAT_R32G32B32A32_SFLOAT,Job_texturesMut(job,WEIGHT)) ||
        !makeTexture(self,w,h,VK_FORMAT_R8G8B8A8_SRGB,Job_texturesMut(job,OUTPUT)) ||
        (!sampled && !makeBuffer(self,(VkDeviceSize) w*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,Job_buffersMut(job,READBACK))) ||
        !framebuffer(self,GROUP_FRAME,GpuScope_groupRender(self),GROUP,1) ||
        !framebuffer(self,SCATTER_FRAME,GpuScope_scatterRender(self),ACCUMULATION,2) ||
        !framebuffer(self,FINAL_FRAME,GpuScope_finalRender(self),OUTPUT,1))
        goto failed;
    VkDescriptorPoolSize poolSize={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=1,.pPoolSizes=&poolSize};
    if (vkCreateDescriptorPool(dev,&dp,nullptr,Job_descriptorsMut(job))!=VK_SUCCESS)
        goto failed;
    VkDescriptorSetLayout layouts[]={GpuScope_scopeDescriptor(self),GpuScope_scatterDescriptor(self),GpuScope_scopeDescriptor(self)};
    VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=Job_descriptors(job),.descriptorSetCount=3,.pSetLayouts=layouts};
    if (vkAllocateDescriptorSets(dev,&da,Job_setsMut(job))!=VK_SUCCESS)
        goto failed;
    for (u32 i=0;i<4;++i) {
        descriptor(self,GROUP_FRAME,i,i<3 ? i : PRIOR); // phase0 never reads filtered
        descriptor(self,FINAL_FRAME,i,i<3 ? i : ACCUMULATION);
    }
    u32 source=scope==GPU_SCOPE_ELEMENT ? GROUP : scope==GPU_SCOPE_FOREGROUND ? FOREGROUND : PRIOR;
    descriptor(self,SCATTER_FRAME,0,source);
    i32 push[]={(i32) scope,0,panelX,panelY,(i32) pw,(i32) ph,foregroundX,foregroundY,(i32) radius};
    if (scope==GPU_SCOPE_ELEMENT) {
        beginPass(self,GROUP_FRAME,GpuScope_groupRender(self),GpuScope_groupPipeline(self),GpuScope_scopeLayout(self),w,h,1);
        vkCmdPushConstants(cmd,GpuScope_scopeLayout(self),VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,push);
        vkCmdDraw(cmd,3,1,0,0); vkCmdEndRenderPass(cmd);
    }
    beginPass(self,SCATTER_FRAME,GpuScope_scatterRender(self),GpuScope_scatterPipeline(self),GpuScope_scatterLayout(self),(u32) aw,(u32) ah,2);
    i32 scatter[]={(i32) sw,(i32) sh,(i32) aw,(i32) ah,(i32) radius,scope==GPU_SCOPE_ELEMENT ? 0 : 1};
    vkCmdPushConstants(cmd,GpuScope_scatterLayout(self),VK_SHADER_STAGE_VERTEX_BIT,0,sizeof scatter,scatter);
    vkCmdDraw(cmd,6,sw*sh,0,0); vkCmdEndRenderPass(cmd);
    push[1]=1;
    beginPass(self,FINAL_FRAME,GpuScope_finalRender(self),GpuScope_finalPipeline(self),GpuScope_scopeLayout(self),w,h,1);
    vkCmdPushConstants(cmd,GpuScope_scopeLayout(self),VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,push);
    vkCmdDraw(cmd,3,1,0,0); vkCmdEndRenderPass(cmd);
    Texture *target=Job_texturesMut(job,OUTPUT); Buffer *read=Job_buffersMut(job,READBACK);
    if (!sampled) {
        barrier(self,target,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
        vkCmdCopyImageToBuffer(cmd,Texture_image(target),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Buffer_buffer(read),1,&copy);
        VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .buffer=Buffer_buffer(read),.size=Buffer_size(read)};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    }
    if (vkEndCommandBuffer(cmd)!=VK_SUCCESS || vkResetFences(dev,1,GpuScope_fenceMut(self))!=VK_SUCCESS)
        goto failed;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    VkResult submitted=vkQueueSubmit(GpuScope_queue(self),1,&submit,GpuScope_fence(self));
    if (submitted==VK_ERROR_DEVICE_LOST) {
        GpuScope_setPending(self,true); // completion uncertain: retain, never free under GPU
        goto rejected;
    }
    if (submitted!=VK_SUCCESS)
        goto failed;
    GpuScope_setPending(self,true);
    if (vkWaitForFences(dev,1,GpuScope_fenceMut(self),VK_TRUE,GPU_WAIT_NS)!=VK_SUCCESS)
        goto rejected;
    GpuScope_setPending(self,false);
    Image *result=Image_2(w,h);
    if (sampled) {
        if (!result)
            goto failed;
        SampledImage *texture = SampledImage_5(GpuScope_owner(self), (any) Texture_image(target),
            (any) Texture_memory(target), w, h);
        if (!texture) {
            Image_destroy(result);
            goto failed;
        }
        // Adopted storage now belongs to the texture; the temporary job view
        // is released before that texture can be freed.
        Texture_setImage(target, VK_NULL_HANDLE);
        Texture_setMemory(target, VK_NULL_HANDLE);
        bool bound = SampledImage_bindImage(texture, result);
        // releaseJob must destroy its view before a failed bind frees storage.
        releaseJob(self);
        SampledImage_release(texture);
        if (!bound) {
            Image_destroy(result);
            goto rejected;
        }
        *out = result;
        return true;
    }
    if (!result || !Image_ensureShadow(result,w,h)) { Image_destroy(result); goto failed; }
    any mappedPixelBytes;
    if (vkMapMemory(dev,Buffer_memory(read),0,Buffer_size(read),0,&mappedPixelBytes)!=VK_SUCCESS) { Image_destroy(result); goto failed; }
    for (u32 y=0;y<h;++y)
        memcpy(Image_pixels(result)+(bytes) y*Image_stride(result),(u8*) mappedPixelBytes+(bytes) y*w*4,(bytes) w*4);
    vkUnmapMemory(dev,Buffer_memory(read)); releaseJob(self); *out=result; return true;
failed:
    releaseJob(self);
rejected:
    THROW("GpuScope rejected or failed GPU scope render"); return false;
}
// Renders the requested filter scope and synchronously returns its CPU-visible Image output.
bool GpuScope_render(GpuScope *self,u32 scope,const Image *prior,const Image *decoration,
    i32 panelX,i32 panelY,const Image *foreground,i32 foregroundX,i32 foregroundY,u32 radius,Image **out) {
    return render(self,scope,prior,decoration,panelX,panelY,foreground,foregroundX,foregroundY,radius,false,out);
}
// Renders the requested scope and binds the result as a sampled GPU resource without readback.
bool GpuScope_renderSampled(GpuScope *self,u32 scope,const Image *prior,const Image *decoration,
    i32 panelX,i32 panelY,const Image *foreground,i32 foregroundX,i32 foregroundY,u32 radius,Image **out) {
    return render(self,scope,prior,decoration,panelX,panelY,foreground,foregroundX,foregroundY,radius,true,out);
}
// Writes the bounded concise or field-level projection for the scope.
static fn format(const GpuScope *self,bool structure,char *dest,bytes cap,bool *outTruncated) {
    if (!dest || !cap) {
        if (outTruncated)
            *outTruncated=true;
        return;
    }
    i32 n;
    if (!self)
        n=snprintf(dest,cap,"nullptr");
    else if (!structure)
        n=snprintf(dest,cap,"GpuScope(Vulkan scatter, budget=%u,pending=%d)",GpuScope_maxPixels(self),GpuScope_pending(self));
    else n=snprintf(dest,cap,"GpuScope{device=%p,physical=%p,queue=%p,maxPixels=%u,maxExtent=%u,scatterRender=%p,groupRender=%p,finalRender=%p,scatterDescriptor=%p,scopeDescriptor=%p,scatterLayout=%p,scopeLayout=%p,scatterPipeline=%p,groupPipeline=%p,finalPipeline=%p,sampler=%p,commands=%p,command=%p,fence=%p,pending=%d,job=GPU transport records,owner=%p}",
        (any) GpuScope_device(self),(any) GpuScope_physical(self),(any) GpuScope_queue(self),GpuScope_maxPixels(self),GpuScope_maxExtent(self),
        (any) GpuScope_scatterRender(self),(any) GpuScope_groupRender(self),(any) GpuScope_finalRender(self),
        (any) GpuScope_scatterDescriptor(self),(any) GpuScope_scopeDescriptor(self),(any) GpuScope_scatterLayout(self),(any) GpuScope_scopeLayout(self),
        (any) GpuScope_scatterPipeline(self),(any) GpuScope_groupPipeline(self),(any) GpuScope_finalPipeline(self),(any) GpuScope_sampler(self),
        (any) GpuScope_commands(self),(any) GpuScope_command(self),(any) GpuScope_fence(self),GpuScope_pending(self),(any) GpuScope_owner(self));
    if (outTruncated)
        *outTruncated=n<0 || (bytes) n>=cap;
}
// Formats a bounded value summary of the GPU scope.
fn GpuScope_toString(const GpuScope *self,char *dest,bytes cap,bool *outTruncated) { format(self,false,dest,cap,outTruncated); }
// Formats the GPU scope's own fields into caller storage.
fn GpuScope_toStringStruct(const GpuScope *self,char *dest,bytes cap,bool *outTruncated) { format(self,true,dest,cap,outTruncated); }
