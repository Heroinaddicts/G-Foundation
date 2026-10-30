#include "ShaderCompileJob.h"
#include <algorithm>

bool ValidateShaderReflection(ShaderCompileJob & job,const Json::Value & reflection) {
    const QString & stage=job.Stage;
    if(!reflection.isObject() || !reflection["entryPoints"].isArray() || reflection["entryPoints"].size()!=1
        || reflection["entryPoints"][0]["mode"].asString()!=(stage=="vertex"?"vert":stage=="fragment"?"frag":"comp")) return job.Fail("Shader stage/entry reflection mismatch");
    for(const char * unsupported:{"push_constants","subpass_inputs","acceleration_structures","textures"})
        if(!reflection[unsupported].empty()) return job.Fail(std::string("Unsupported SDL shader resource: ")+unsupported);
    const bool compute=stage=="compute";
    const uint32_t resourceSet=stage=="fragment"?2:0,uniformSet=compute?2:resourceSet+1;
    std::map<std::string,std::vector<uint32_t>> bindingGroups;
    for(const char * category:{"separate_images","separate_samplers","images","ssbos","ubos"}) {
        if(!reflection[category].isNull() && !reflection[category].isArray()) return job.Fail("Invalid shader resource reflection");
        for(const auto & resource:reflection[category]) {
            if(!resource["set"].isUInt() || !resource["binding"].isUInt() || !resource["array"].empty()) return job.Fail("Shader resources need explicit scalar bindings; descriptor arrays are not supported");
            const uint32_t set=resource["set"].asUInt();std::string group=category;
            if(group=="ubos") {if(set!=uniformSet) return job.Fail("Uniform buffer register space does not follow SDL conventions");}
            else if(group=="separate_images" || group=="separate_samplers") {if(set!=resourceSet) return job.Fail("Texture/sampler register space does not follow SDL conventions");}
            else {
                if(set!=resourceSet && !(compute && set==1)) return job.Fail("Storage register space does not follow SDL conventions");
                if((!compute || set==0) && !resource.get("readonly",false).asBool()) return job.Fail("Read-write storage requires a compute shader and space1");
                group+=compute && set==1?"RW":"RO";
            }
            bindingGroups[group].push_back(resource["binding"].asUInt());
        }
    }
    const auto dense=[&](const std::string & group,uint32_t start,uint32_t maximum) {
        auto & values=bindingGroups[group];std::sort(values.begin(),values.end());
        if(values.size()>maximum) return false;
        for(size_t i=0;i<values.size();++i) if(values[i]!=start+i) return false;
        return true;
    };
    const uint32_t images=uint32_t(bindingGroups["separate_images"].size()),samplers=uint32_t(bindingGroups["separate_samplers"].size());
    if(samplers>images || images-samplers+bindingGroups["imagesRO"].size()>8
        || !dense("separate_images",0,24) || !dense("separate_samplers",0,16) || !dense("ubos",0,4)
        || !dense("imagesRO",images,8) || !dense("imagesRW",0,8)
        || !dense("ssbosRO",images+uint32_t(bindingGroups["imagesRO"].size()),8)
        || !dense("ssbosRW",uint32_t(bindingGroups["imagesRW"].size()),8)) return job.Fail("SDL shader binding slots must be consecutive from zero and within resource limits");
    return true;
}
