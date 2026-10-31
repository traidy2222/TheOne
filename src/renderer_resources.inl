    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p{}; vkGetPhysicalDeviceMemoryProperties(physical_,&p);
        for(uint32_t i=0;i<p.memoryTypeCount;++i) if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("No compatible GPU memory type");
    }
    void createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& buffer,VkDeviceMemory& memory) {
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; b.size=size; b.usage=usage; b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_,&b,nullptr,&buffer),"create buffer");
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_,buffer,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=req.size;
        a.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_,&a,nullptr,&memory),"allocate buffer"); check(vkBindBufferMemory(device_,buffer,memory,0),"bind buffer");
    }
    VkImageView imageView(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image=image; v.viewType=VK_IMAGE_VIEW_TYPE_2D; v.format=format;
        v.subresourceRange={aspect,0,1,0,1}; VkImageView result=VK_NULL_HANDLE; check(vkCreateImageView(device_,&v,nullptr,&result),"create image view"); return result;
    }
    void createImage(uint32_t width,uint32_t height,VkFormat format,VkImageUsageFlags usage,VkImage& image,VkDeviceMemory& memory) {
        VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.imageType=VK_IMAGE_TYPE_2D; i.format=format; if(format==VK_FORMAT_R8G8B8A8_UNORM)i.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT; i.extent={width,height,1};
        i.mipLevels=1; i.arrayLayers=1; i.samples=VK_SAMPLE_COUNT_1_BIT; i.tiling=VK_IMAGE_TILING_OPTIMAL; i.usage=usage; i.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device_,&i,nullptr,&image),"create image"); VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_,image,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=req.size; a.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_,&a,nullptr,&memory),"allocate image"); check(vkBindImageMemory(device_,image,memory,0),"bind image");
    }
    VkShaderModule shader(const char* name) {
        auto file=std::filesystem::path(SDL_GetBasePath())/"shaders"/name;
        std::ifstream input(file,std::ios::binary|std::ios::ate); if(!input) throw std::runtime_error("Cannot open shader: "+file.string());
        size_t size=static_cast<size_t>(input.tellg()); if(!size || size%4) throw std::runtime_error("Invalid SPIR-V");
        std::vector<uint32_t> code(size/4); input.seekg(0); input.read(reinterpret_cast<char*>(code.data()),static_cast<std::streamsize>(size));
        if(!input) throw std::runtime_error("Cannot read shader");
        VkShaderModuleCreateInfo s{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; s.codeSize=size; s.pCode=code.data();
        VkShaderModule module=VK_NULL_HANDLE; check(vkCreateShaderModule(device_,&s,nullptr,&module),"create shader"); return module;
    }
    void immediate(const std::function<void(VkCommandBuffer)>& record) {
        VkCommandBuffer c=VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; a.commandPool=pool_; a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device_,&a,&c),"allocate upload commands");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(c,&b),"begin upload"); record(c); check(vkEndCommandBuffer(c),"end upload");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&c;
        check(vkQueueSubmit(queue_,1,&submit,VK_NULL_HANDLE),"submit upload"); check(vkQueueWaitIdle(queue_),"wait upload"); vkFreeCommandBuffers(device_,pool_,1,&c);
    }
    void createSharedResources() {
        std::array<VkDescriptorSetLayoutBinding,8> bindings{};
        bindings[0]={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        bindings[1]={1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        bindings[2]={2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        for(uint32_t i=3;i<=6;++i) bindings[i]={i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        bindings[7]={7,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=8; layout.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&textureLayout_),"create descriptor layout");
        std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6144},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2048}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool.maxSets=1024; pool.poolSizeCount=2; pool.pPoolSizes=sizes.data();
        check(vkCreateDescriptorPool(device_,&pool,nullptr,&texturePool_),"create descriptor pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampler.magFilter=VK_FILTER_LINEAR; sampler.minFilter=VK_FILTER_LINEAR;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT; sampler.maxLod=0;
        check(vkCreateSampler(device_,&sampler,nullptr,&sampler_),"create sampler");
        sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST; sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; sampler.compareEnable=VK_TRUE; sampler.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        check(vkCreateSampler(device_,&sampler,nullptr,&shadowSampler_),"create shadow sampler");
        createBuffer(sizeof(SceneUniform),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,sceneUniform_,sceneUniformMemory_);
        lightVP_=glm::orthoRH_ZO(-23.0f,23.0f,-23.0f,23.0f,1.0f,90.0f)*glm::lookAtRH(glm::vec3(-20,35,18),glm::vec3(0),glm::vec3(0,1,0)); lightVP_[0][1]*=-1; lightVP_[1][1]*=-1; lightVP_[2][1]*=-1; lightVP_[3][1]*=-1;
        createImage(2048,2048,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,shadowImage_,shadowMemory_);
        shadowView_=imageView(shadowImage_,VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_DEPTH_BIT);
        VkAttachmentDescription depth{}; depth.format=VK_FORMAT_D32_SFLOAT; depth.samples=VK_SAMPLE_COUNT_1_BIT; depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; depth.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.pDepthStencilAttachment=&ref;
        std::array<VkSubpassDependency,2> deps{};
        deps[0].srcSubpass=VK_SUBPASS_EXTERNAL; deps[0].dstSubpass=0; deps[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0; deps[1].dstSubpass=VK_SUBPASS_EXTERNAL; deps[1].srcStageMask=VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; r.attachmentCount=1; r.pAttachments=&depth; r.subpassCount=1; r.pSubpasses=&sub; r.dependencyCount=2; r.pDependencies=deps.data();
        check(vkCreateRenderPass(device_,&r,nullptr,&shadowPass_),"create shadow pass");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass=shadowPass_; fb.attachmentCount=1; fb.pAttachments=&shadowView_; fb.width=fb.height=2048; fb.layers=1;
        check(vkCreateFramebuffer(device_,&fb,nullptr,&shadowFramebuffer_),"create shadow framebuffer");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Frame)};
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; l.setLayoutCount=1; l.pSetLayouts=&textureLayout_; l.pushConstantRangeCount=1; l.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device_,&l,nullptr,&pipelineLayout_),"create pipeline layout"); shadowPipeline_=createGraphicsPipeline(true,false);
    }
    const Texture& texture(const one::Image* image) {
        auto it=textures_.find(image); if(it!=textures_.end()) return it->second;
        auto& t=textures_[image]; one::Image white; const auto& src=image?*image:white;
        VkBuffer staging=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        createBuffer(src.rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,staging,memory);
        void* ptr=nullptr; check(vkMapMemory(device_,memory,0,src.rgba.size(),0,&ptr),"map texture staging"); std::memcpy(ptr,src.rgba.data(),src.rgba.size()); vkUnmapMemory(device_,memory);
        createImage(src.width,src.height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,t.image,t.memory);
        t.view=imageView(t.image,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_ASPECT_COLOR_BIT);
        t.linearView=imageView(t.image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT);
        immediate([&](VkCommandBuffer c) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image=t.image; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
            VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={static_cast<uint32_t>(src.width),static_cast<uint32_t>(src.height),1};
            vkCmdCopyBufferToImage(c,staging,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        });
        vkDestroyBuffer(device_,staging,nullptr); vkFreeMemory(device_,memory,nullptr);
        return t;
    }
    const MaterialGPU& material(const one::Material* source,const one::Image* baseImage) {
        auto found=materials_.find(source); if(found!=materials_.end()) return found->second;
        one::Material fallback; const auto& m=source?*source:fallback;
        auto& gpu=materials_[source];
        glm::vec4 emissive(m.emissiveFactor,0); createBuffer(sizeof(emissive),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,gpu.uniform,gpu.memory);
        void* ptr=nullptr; check(vkMapMemory(device_,gpu.memory,0,sizeof(emissive),0,&ptr),"map material");std::memcpy(ptr,&emissive,sizeof(emissive));vkUnmapMemory(device_,gpu.memory);
        VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool=texturePool_; a.descriptorSetCount=1; a.pSetLayouts=&textureLayout_;
        check(vkAllocateDescriptorSets(device_,&a,&gpu.set),"allocate material descriptor");
        std::array<VkDescriptorImageInfo,6> images{{
            {sampler_,texture(baseImage).view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {shadowSampler_,shadowView_,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {sampler_,texture(m.metallicRoughness).linearView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler_,texture(m.normal?m.normal:&flatNormal_).linearView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler_,texture(m.occlusion).linearView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler_,texture(m.emissive).view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        std::array<VkDescriptorBufferInfo,2> buffers{{{sceneUniform_,0,sizeof(SceneUniform)},{gpu.uniform,0,sizeof(emissive)}}};
        std::array<VkWriteDescriptorSet,8> writes{};
        for(uint32_t i=0;i<8;++i) { auto& w=writes[i]; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=gpu.set;w.dstBinding=i;w.descriptorCount=1;
            if(i==1 || i==7) { w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo=&buffers[i==1?0:1]; }
            else { w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[i==0?0:i-1]; }
        }
        vkUpdateDescriptorSets(device_,8,writes.data(),0,nullptr);return gpu;
    }
    void destroyTextures() {
        for(auto& [key,m]:materials_) { (void)key; if(m.set) vkFreeDescriptorSets(device_,texturePool_,1,&m.set);if(m.uniform)vkDestroyBuffer(device_,m.uniform,nullptr);if(m.memory)vkFreeMemory(device_,m.memory,nullptr); } materials_.clear();
        for(auto& [key,t]:textures_) { (void)key; if(t.linearView)vkDestroyImageView(device_,t.linearView,nullptr); if(t.view) vkDestroyImageView(device_,t.view,nullptr); if(t.image) vkDestroyImage(device_,t.image,nullptr); if(t.memory) vkFreeMemory(device_,t.memory,nullptr); }
        textures_.clear();
    }
    void destroySharedResources() {
        destroyTextures();
        if(shadowPipeline_) vkDestroyPipeline(device_,shadowPipeline_,nullptr);
        if(pipelineLayout_) vkDestroyPipelineLayout(device_,pipelineLayout_,nullptr);
        if(shadowFramebuffer_) vkDestroyFramebuffer(device_,shadowFramebuffer_,nullptr);
        if(shadowPass_) vkDestroyRenderPass(device_,shadowPass_,nullptr);
        if(shadowView_) vkDestroyImageView(device_,shadowView_,nullptr);
        if(shadowImage_) vkDestroyImage(device_,shadowImage_,nullptr);
        if(shadowMemory_) vkFreeMemory(device_,shadowMemory_,nullptr);
        if(sceneUniform_) vkDestroyBuffer(device_,sceneUniform_,nullptr);
        if(sceneUniformMemory_) vkFreeMemory(device_,sceneUniformMemory_,nullptr);
        if(sampler_) vkDestroySampler(device_,sampler_,nullptr); if(shadowSampler_) vkDestroySampler(device_,shadowSampler_,nullptr);
        if(texturePool_) vkDestroyDescriptorPool(device_,texturePool_,nullptr); if(textureLayout_) vkDestroyDescriptorSetLayout(device_,textureLayout_,nullptr);
    }
    void updateUniform(bool shadows) {
        SceneUniform u{lightVP_,glm::vec4(glm::normalize(glm::vec3(-20,35,18)),0),glm::vec4(shadows?1.0f:0.0f,0,0,0)};
        void* p=nullptr; check(vkMapMemory(device_,sceneUniformMemory_,0,sizeof(u),0,&p),"map scene uniform"); std::memcpy(p,&u,sizeof(u)); vkUnmapMemory(device_,sceneUniformMemory_);
    }
    void uploadVertices(const std::vector<one::Vertex>& vertices) {
        VkDeviceSize size=vertices.size()*sizeof(one::Vertex); if(size==0) return;
        if(size>vertexCapacity_) {
            if(vertices_) vkDestroyBuffer(device_,vertices_,nullptr); if(vertexMemory_) vkFreeMemory(device_,vertexMemory_,nullptr);
            vertices_=VK_NULL_HANDLE; vertexMemory_=VK_NULL_HANDLE; vertexCapacity_=size*2;
            createBuffer(vertexCapacity_,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices_,vertexMemory_);
        }
        void* ptr=nullptr; check(vkMapMemory(device_,vertexMemory_,0,size,0,&ptr),"map vertices"); std::memcpy(ptr,vertices.data(),static_cast<size_t>(size)); vkUnmapMemory(device_,vertexMemory_);
    }
