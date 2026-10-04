VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR* p){
    argent::PresentAnalysis analysis(argent::presentation::gameplayInput.load());
    auto s=deviceOf(key(q));if(!s)return VK_ERROR_DEVICE_LOST;
    bool consumed=false;argent::Source source;State::Queue queue{};bool known=false;
    if(s->game&&p->swapchainCount==1){
        {std::lock_guard<std::recursive_mutex> l(stateMutex);auto si=s->sources.find(p->pSwapchains[0]);auto qi=s->queues.find(q);
        if(si!=s->sources.end()&&qi!=s->queues.end()){source=si->second;queue=qi->second;known=true;}}
        const bool sfsVrPresent=s->sfs&&argent::sfs::vrEnabled();
        const bool sourcePresent=argent::sfs::sourceSwapchain(s->device,p->pSwapchains[0]);
        if(known&&sfsVrPresent){
          argent::sfs::StereoFrame pair;bool sourceComplete=false;
          static std::atomic<uint64_t> sfsPairMiss{},sfsPresentMiss{};
          const bool pairReady=p->pImageIndices[0]<source.images.size()&&argent::sfs::pair(s->device,source.images[p->pImageIndices[0]],source.extent,source.format,pair);
          if(pairReady){
            argent::StereoMirror finalMirror;
            auto mirror=s->mirrors.find(p->pSwapchains[0]);
            if(sourcePresent&&mirror!=s->mirrors.end()&&mirror->second->needsFrame())
              finalMirror=[&](VkImage eye,VkExtent2D extent,VkImageLayout layout){
                try{static argent::FrameTiming::Totals t;argent::FrameTiming timing("desktopMirror",t);
                  mirror->second->present(*s,eye,q,argent::DesktopMirrorPacing::Clock::now(),extent,layout,true);
                }catch(const std::exception& e){try{argent::log(e.what());}catch(...){}}catch(...){}
              };
            const auto handoff=argent::presentStereoFrame(*s,pair,p->waitSemaphoreCount,p->pWaitSemaphores,finalMirror);
            consumed=handoff.waitsConsumed;sourceComplete=handoff.sourceComplete;
            if(!consumed){auto n=++sfsPresentMiss;if(n<=8||(argent::extendedLogging()&&n%120==0))try{argent::log("SFS_PRESENT_NOT_CONSUMED count="+std::to_string(n)+" image="+std::to_string(p->pImageIndices[0])+" sourcePresent="+std::to_string(sourcePresent));}catch(...){}}
          }else{
            try{argent::cancelStereoFrame();}catch(...){}
            auto n=++sfsPairMiss;if(n<=8||(argent::extendedLogging()&&n%120==0))try{argent::log("SFS_PRESENT_NO_PAIR count="+std::to_string(n)+" image="+std::to_string(p->pImageIndices[0])+" images="+std::to_string(source.images.size())+" extent="+std::to_string(source.extent.width)+"x"+std::to_string(source.extent.height));}catch(...){}
          }
          if(consumed)try{argent::readbackStereoIfRequested(*s,source.images[p->pImageIndices[0]],source.extent,source.format);}catch(...){}
          if(sourcePresent){
            VkResult r;{static argent::FrameTiming::Totals t;argent::FrameTiming timing("sourceRetire",t);r=argent::sfs::presentSource(s->device,q,*p,consumed,sourceComplete);}
            argent::sfs::copyCompleted(s->device);try{argent::trace::present();}catch(...){}auto n=++presents;
            if(n==1||(argent::extendedLogging()&&n%120==0))try{argent::log("SFS_GAME_PRESENT count="+std::to_string(n)+" result="+std::to_string(r)+" XRcopied="+std::to_string(consumed));}catch(...){}return r;
          }
          argent::sfs::copyCompleted(s->device);
        }
        if(!sfsVrPresent&&known&&source.transferable&&s->graphicsQueue)consumed=argent::presentQuad(*s,q,queue.family,queue.index,source,p->pImageIndices[0],*p);
    }
    auto ready=*p;if(consumed){ready.waitSemaphoreCount=0;ready.pWaitSemaphores=nullptr;}
    VkResult r;{std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);r=s->proc<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(q,&ready);}
    if(s->game){try{argent::trace::present();}catch(...){}auto n=++presents;if(n==1||n%600==0)try{argent::log("GAME_PRESENT count="+std::to_string(n)+" result="+std::to_string(r)+" XRcopied="+std::to_string(consumed)+" known="+std::to_string(known)+" graphics="+std::to_string(queue.graphics)+" transferable="+std::to_string(source.transferable)+" family="+std::to_string(queue.family)+" index="+std::to_string(queue.index)+" swapchains="+std::to_string(p->swapchainCount));}catch(...){}}
    return r;
}
