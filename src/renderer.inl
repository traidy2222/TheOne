    #include "renderer_resources.inl"
    void createRenderTargets(VkFormat colorFormat) {
        VkFormat depthFormat=VK_FORMAT_UNDEFINED;
        for(auto f : {VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM}) {
            VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(physical_,f,&props);
            if(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat=f; break; }
        }
        if(depthFormat==VK_FORMAT_UNDEFINED) throw std::runtime_error("No depth attachment format");
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image.imageType=VK_IMAGE_TYPE_2D; image.format=depthFormat;
        image.extent={extent_.width,extent_.height,1}; image.mipLevels=1; image.arrayLayers=1; image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.tiling=VK_IMAGE_TILING_OPTIMAL; image.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device_,&image,nullptr,&depth_),"create depth image");
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_,depth_,&req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_,&alloc,nullptr,&depthMemory_),"allocate depth memory");
        check(vkBindImageMemory(device_,depth_,depthMemory_,0),"bind depth memory"); depthView_=imageView(depth_,depthFormat,VK_IMAGE_ASPECT_DEPTH_BIT);
        std::array<VkAttachmentDescription,2> attachments{};
        attachments[0].format=colorFormat; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT; attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1]=attachments[0]; attachments[1].format=depthFormat; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&color; sub.pDepthStencilAttachment=&depth;
        VkSubpassDependency dep{}; dep.srcSubpass=VK_SUBPASS_EXTERNAL; dep.dstSubpass=0;
        dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstStageMask=dep.srcStageMask; dep.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; r.attachmentCount=2; r.pAttachments=attachments.data();
        r.subpassCount=1; r.pSubpasses=&sub; r.dependencyCount=1; r.pDependencies=&dep;
        check(vkCreateRenderPass(device_,&r,nullptr,&renderPass_),"create render pass");
        for(auto imageHandle : images_) {
            views_.push_back(imageView(imageHandle,colorFormat,VK_IMAGE_ASPECT_COLOR_BIT));
            std::array<VkImageView,2> targets{views_.back(),depthView_};
            VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; f.renderPass=renderPass_; f.attachmentCount=2; f.pAttachments=targets.data();
            f.width=extent_.width; f.height=extent_.height; f.layers=1;
            VkFramebuffer framebuffer=VK_NULL_HANDLE; check(vkCreateFramebuffer(device_,&f,nullptr,&framebuffer),"create framebuffer"); framebuffers_.push_back(framebuffer);
        }
        pipeline_=createGraphicsPipeline(false,false); blendPipeline_=createGraphicsPipeline(false,true);
        ImGui_ImplVulkan_InitInfo gui{}; gui.ApiVersion=VK_API_VERSION_1_1; gui.Instance=instance_; gui.PhysicalDevice=physical_; gui.Device=device_;
        gui.QueueFamily=family_; gui.Queue=queue_; gui.DescriptorPoolSize=2048; gui.RenderPass=renderPass_; gui.MinImageCount=minImages_; gui.ImageCount=static_cast<uint32_t>(images_.size());
        if(!ImGui_ImplVulkan_Init(&gui)) throw std::runtime_error("Cannot initialize ImGui Vulkan backend"); guiReady_=true;
        if(!ImGui_ImplVulkan_CreateFontsTexture()) throw std::runtime_error("Cannot upload UI fonts");
    }
    void destroyRenderTargets() {
        if(guiReady_) { ImGui_ImplVulkan_Shutdown(); guiReady_=false; }
        if(blendPipeline_) vkDestroyPipeline(device_,blendPipeline_,nullptr); blendPipeline_=VK_NULL_HANDLE;
        if(pipeline_) vkDestroyPipeline(device_,pipeline_,nullptr); pipeline_=VK_NULL_HANDLE;
        for(auto f:framebuffers_) vkDestroyFramebuffer(device_,f,nullptr); framebuffers_.clear();
        if(renderPass_) vkDestroyRenderPass(device_,renderPass_,nullptr); renderPass_=VK_NULL_HANDLE;
        for(auto v:views_) vkDestroyImageView(device_,v,nullptr); views_.clear();
        if(depthView_) vkDestroyImageView(device_,depthView_,nullptr); depthView_=VK_NULL_HANDLE;
        if(depth_) vkDestroyImage(device_,depth_,nullptr); depth_=VK_NULL_HANDLE;
        if(depthMemory_) vkFreeMemory(device_,depthMemory_,nullptr); depthMemory_=VK_NULL_HANDLE;
        if(readback_) vkDestroyBuffer(device_,readback_,nullptr); readback_=VK_NULL_HANDLE;
        if(readbackMemory_) vkFreeMemory(device_,readbackMemory_,nullptr); readbackMemory_=VK_NULL_HANDLE;
    }
    VkPipeline createGraphicsPipeline(bool shadow,bool blend) {
        VkShaderModule vert=shader(shadow?"shadow.vert.spv":"scene.vert.spv"),frag=VK_NULL_HANDLE;
        VkPipeline result=VK_NULL_HANDLE;
        try {
            if(!shadow) frag=shader("scene.frag.spv");
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};
            for(auto& s:stages) { s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; s.pName="main"; }
            stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vert; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag;
            VkVertexInputBindingDescription binding{0,sizeof(one::Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription,4> attrs{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(one::Vertex,position)},
                {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(one::Vertex,normal)},{2,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(one::Vertex,color)},{3,0,VK_FORMAT_R32G32_SFLOAT,offsetof(one::Vertex,uv)}}};
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding;
            vi.vertexAttributeDescriptionCount=shadow?1:4; vi.pVertexAttributeDescriptions=attrs.data();
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1;
            rs.depthBiasEnable=shadow; rs.depthBiasConstantFactor=1.25f; rs.depthBiasSlopeFactor=1.75f;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=!blend; ds.depthCompareOp=VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState color{}; color.colorWriteMask=0xf; color.blendEnable=blend;
            color.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; color.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; color.colorBlendOp=VK_BLEND_OP_ADD;
            color.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; color.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; color.alphaBlendOp=VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount=shadow?0:1; cb.pAttachments=&color;
            std::array<VkDynamicState,2> dynamic{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount=2; dy.pDynamicStates=dynamic.data();
            VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.stageCount=shadow?1:2; p.pStages=stages.data(); p.pVertexInputState=&vi; p.pInputAssemblyState=&ia;
            p.pViewportState=&vp; p.pRasterizationState=&rs; p.pMultisampleState=&ms; p.pDepthStencilState=&ds; p.pColorBlendState=&cb; p.pDynamicState=&dy; p.layout=pipelineLayout_; p.renderPass=shadow?shadowPass_:renderPass_;
            check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&p,nullptr,&result),"create graphics pipeline");
        } catch(...) { if(frag) vkDestroyShaderModule(device_,frag,nullptr); vkDestroyShaderModule(device_,vert,nullptr); throw; }
        if(frag) vkDestroyShaderModule(device_,frag,nullptr); vkDestroyShaderModule(device_,vert,nullptr); return result;
    }
    void recordScene(uint32_t index,const std::vector<one::Draw>& draws,const one::Camera& camera,float time,bool capture,bool shadows,uint64_t selected) {
        VkDeviceSize offset=0;
        if(vertices_) vkCmdBindVertexBuffers(command_,0,1,&vertices_,&offset);
        // Always execute the depth pass so every sampled shadow image has a defined layout.
        VkClearValue depth{}; depth.depthStencil={1,0};
        VkRenderPassBeginInfo shadow{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; shadow.renderPass=shadowPass_; shadow.framebuffer=shadowFramebuffer_; shadow.renderArea={{0,0},{2048,2048}}; shadow.clearValueCount=1; shadow.pClearValues=&depth;
        vkCmdBeginRenderPass(command_,&shadow,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport sv{0,0,2048,2048,0,1}; VkRect2D ss{{0,0},{2048,2048}}; vkCmdSetViewport(command_,0,1,&sv); vkCmdSetScissor(command_,0,1,&ss); vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,shadowPipeline_);
        Frame shadowFrame{lightVP_,glm::vec4(0),glm::vec4(1),glm::vec4(0),glm::vec4(0)};
        vkCmdPushConstants(command_,pipelineLayout_,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Frame),&shadowFrame);
        if(shadows) for(const auto& draw:draws) if(!draw.material || !draw.material->blend) vkCmdDraw(command_,draw.count,1,draw.first,0);
        vkCmdEndRenderPass(command_);
        std::array<VkClearValue,2> clear{}; clear[0].color={{0.035f,0.055f,0.09f,1}}; clear[1].depthStencil={1,0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass=renderPass_; begin.framebuffer=framebuffers_[index]; begin.renderArea={{0,0},extent_}; begin.clearValueCount=2; begin.pClearValues=clear.data();
        vkCmdBeginRenderPass(command_,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0,0,static_cast<float>(extent_.width),static_cast<float>(extent_.height),0,1}; VkRect2D scissor{{0,0},extent_};
        vkCmdSetViewport(command_,0,1,&vp); vkCmdSetScissor(command_,0,1,&scissor);
        for(int blend=0;blend<2;++blend) {
            vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,blend?blendPipeline_:pipeline_);
            for(const auto& draw:draws) {
                one::Material fallback; const auto& m=draw.material?*draw.material:fallback;
                if(m.blend!=(blend!=0)) continue;
                const auto& tex=material(draw.material,draw.image); vkCmdBindDescriptorSets(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout_,0,1,&tex.set,0,nullptr);
                Frame f{camera.projection(static_cast<float>(extent_.width)/extent_.height)*camera.view(),glm::vec4(camera.position,time),m.color,
                    glm::vec4(m.roughness,m.metallic,m.mask?m.cutoff:-1.0f,selected==draw.entity?1.0f:0.0f),glm::vec4(colorFormat_==VK_FORMAT_B8G8R8A8_SRGB || colorFormat_==VK_FORMAT_R8G8B8A8_SRGB?1.0f:0.0f,m.unlit?1.0f:0.0f,m.normalScale,m.occlusionStrength)};
                vkCmdPushConstants(command_,pipelineLayout_,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(f),&f); vkCmdDraw(command_,draw.count,1,draw.first,0);
            }
        }
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),command_); vkCmdEndRenderPass(command_);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=images_[index]; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.newLayout=capture?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.dstAccessMask=capture?VK_ACCESS_TRANSFER_READ_BIT:0;
        vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,capture?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(capture) {
            if(!readback_) createBuffer(static_cast<VkDeviceSize>(extent_.width)*extent_.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,readback_,readbackMemory_);
            VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={extent_.width,extent_.height,1};
            vkCmdCopyImageToBuffer(command_,images_[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback_,1,&copy);
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=0;
            vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }
    }
    void verifyCapture() {
        check(vkWaitForFences(device_,1,&fence_,VK_TRUE,UINT64_MAX),"wait capture");
        void* mapped=nullptr; size_t pixels=static_cast<size_t>(extent_.width)*extent_.height;
        check(vkMapMemory(device_,readbackMemory_,0,pixels*4,0,&mapped),"map readback"); auto* bytes=static_cast<unsigned char*>(mapped);
        size_t varied=0; uint32_t start=extent_.width/3;
        for(uint32_t y=extent_.height/3;y<extent_.height;++y) for(uint32_t x=start;x<extent_.width;++x) {
            size_t i=(static_cast<size_t>(y)*extent_.width+x)*4;
            if(std::abs(int(bytes[i])-int(bytes[(extent_.width-1)*4]))>24 || std::abs(int(bytes[i+1])-int(bytes[(extent_.width-1)*4+1]))>24) ++varied;
        }
        std::ofstream out("capture.ppm",std::ios::binary); out<<"P6\n"<<extent_.width<<" "<<extent_.height<<"\n255\n";
        bool bgra=colorFormat_==VK_FORMAT_B8G8R8A8_UNORM || colorFormat_==VK_FORMAT_B8G8R8A8_SRGB;
        for(size_t i=0;i<pixels;++i) { char rgb[]{static_cast<char>(bytes[i*4+(bgra?2:0)]),static_cast<char>(bytes[i*4+1]),static_cast<char>(bytes[i*4+(bgra?0:2)])}; out.write(rgb,3); }
        vkUnmapMemory(device_,readbackMemory_); if(!out) throw std::runtime_error("Capture write failed");
        if(varied<pixels/10) throw std::runtime_error("GPU readback scene coverage below 10%");
        std::cout<<"GPU readback: "<<varied<<" scene pixels; capture.ppm saved"<<std::endl;
    }
