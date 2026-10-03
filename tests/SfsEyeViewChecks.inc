for(const bool dlss:{false,true})for(const auto failure:{VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_SUCCESS})for(int failedEye=0;failedEye<2;++failedEye){
 eyeViewImage=images[0];eyeViewFailureLayer=-1;eyeViewCreates={};eyeViewDestroys={};checkedEyeViews={};
 VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=images[0];info.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
 info.format=chainInfo.imageFormat;info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
 VkImageView original{};ok(vkCreateImageView(device,&info,nullptr,&original));
 argent::dlss::Resource input{};input.view=original;input.image=images[0];input.format=chainInfo.imageFormat;input.range=info.subresourceRange;input.width=input.height=4;
 std::array<const argent::dlss::Resource*,30> inputs{};for(unsigned i=0;i<4;++i)inputs[i]=&input;
 std::array<argent::dlss::EyeParameters,2> outputs{};argent::sfs::FramePose pose{};
 std::array<VkImageView,2> selected{};
 const auto resolveEyes=[&]{
  if(dlss){
   if(!argent::sfs::dlssEyeResources(command,inputs,outputs,pose))return false;
   for(unsigned eye=0;eye<2;++eye)selected[eye]=outputs[eye].resources[0].view;
  }else for(unsigned eye=0;eye<2;++eye)if(!argent::sfs::eyeAttachmentView(device,original,eye,selected[eye]))return false;
  return true;
 };
 eyeViewFailureLayer=failedEye;eyeViewFailureResult=failure;
 check(!resolveEyes(),"Cached eye views accepted failed or null native creation");
 const auto retainedLeft=checkedEyeViews[0];eyeViewFailureLayer=-1;
 check(resolveEyes(),"Cached eye views could not retry rejected native creation");
 check(selected[0]&&selected[1]&&selected[0]!=selected[1]&&selected[0]!=poisonedEyeView&&selected[1]!=poisonedEyeView,"Cached eye views reused poisoned or aliasing output");
 check(eyeViewCreates[0]==(failedEye==0?2u:1u)&&eyeViewCreates[1]==(failedEye==1?2u:1u),"Eye view retry recreated a valid sibling or skipped failed creation");
 check(failedEye==0||selected[0]==retainedLeft,"Right view rejection lost the cached left view");
 const auto attempts=eyeViewCreates;
 check(resolveEyes()&&eyeViewCreates==attempts,"Warm eye views repeated native creation");
 for(unsigned eye=0;eye<2;++eye){VkImageView attachment{};check(argent::sfs::eyeAttachmentView(device,original,eye,attachment)&&attachment==selected[eye],"Attachment consumer did not share the cached eye views");}
 check(argent::sfs::dlssEyeResources(command,inputs,outputs,pose)&&outputs[0].resources[0].view==selected[0]&&outputs[1].resources[0].view==selected[1]&&eyeViewCreates==attempts,"DLSS consumer did not share the cached eye views");
 vkDestroyImageView(device,original,nullptr);
 check(eyeViewDestroys==std::array<unsigned,2>{1,1},"Cached eye view destruction was not exactly once");
 eyeViewImage=VK_NULL_HANDLE;
}
std::cout<<"Cached eye views: 12 failed/null creation, retry, shared reuse and destruction scenarios passed\n";
