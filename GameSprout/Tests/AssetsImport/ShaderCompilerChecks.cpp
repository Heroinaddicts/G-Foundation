#include "AssetTestSupport.h"
#include "IShaderCompiler.h"

int main(int argc,char ** argv) {
    QCoreApplication application(argc,argv);
    try {
        // Load only the compiler module: no AssetsManager, LibraryManager, DB,
        // TaskApi or IDEWindow participates in this test.
        auto * compiler=static_cast<IShaderCompiler *>(LoadModule("ShaderCompiler"));
        Check(GameSprout::GameSproutEditorFinal::Initialize(compiler,nullptr),"Compiler initialize failed");
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create compiler test directory");
        QDir root(temporary.path());Check(root.mkpath("Assets/Includes")&&root.mkpath("Work"),"Cannot create compiler test directories");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Includes/Color values.hlsli"),"float4 Color() {return float4(1,0,0,1);}\n",error),error);
        ShaderCompileRequest request;
        request.SourceRoot=root.filePath("Assets").toStdString();request.SourcePath="Direct.shader";request.WorkDirectory=root.filePath("Work").toStdString();
        request.Source="#include \"Includes/Color values.hlsli\" // comment\nfloat4 main(uint id:SV_VertexID):SV_Position {return Color()+float4(id,0,0,0);}\n";
        request.Targets.push_back({eShaderCodeFormat::DXIL,eShaderPlatform::Direct3D12});
        ShaderCompileResult result;
        Check(compiler->Compile(request,result),"Direct compiler failed: "+result.Error);
        Check(result.Codes.size()==3&&result.Dependencies.size()==1,"Compiler omitted targets/dependencies");
        Check(result.Codes[2].Format==eShaderCodeFormat::DXIL && result.Codes[2].Data.size()>4
            && std::string(reinterpret_cast<const char *>(result.Codes[2].Data.data()),4)=="DXBC","DXIL generation failed");
        Check(!compiler->GetToolchainHash().empty(),"Compiler has no version signature");

        ShaderCompileResult first,second;
        std::thread a([&]{compiler->Compile(request,first);});
        std::thread b([&]{compiler->Compile(request,second);});a.join();b.join();
        Check(first.Success&&second.Success&&first.Codes[0].Data==second.Codes[0].Data,"Parallel compiler calls interfered");
        request.IsCancellationRequested=[] {return true;};
        Check(!compiler->Compile(request,result)&&result.Codes.empty()&&result.Error.find("cancelled")!=std::string::npos,"Cancellation left partial products");
        request.IsCancellationRequested={};
        request.Targets={{eShaderCodeFormat::Metallib,eShaderPlatform::Vulkan}};
        Check(!compiler->Compile(request,result)&&result.Codes.empty(),"Invalid platform/format accepted");
        request.Targets={{eShaderCodeFormat::Metallib,eShaderPlatform::MacOS}};
        const bool metallib=compiler->Compile(request,result);
        if(metallib) Check(result.Codes.size()==1&&std::string(reinterpret_cast<const char *>(result.Codes[0].Data.data()),4)=="MTLB","Invalid Metal library");
        else {
            Check(result.Codes.empty()&&!result.Error.empty(),"Missing Metal toolchain left partial products");
            Check(result.Error.find("Metal Toolchain")!=std::string::npos || result.Error.find("Apple SDK")!=std::string::npos,"Unexpected Metallib failure: "+result.Error);
            std::cout<<"SKIP Metallib success: Metal Toolchain unavailable; PASS failure and empty output\n";
        }
        request.Targets={{eShaderCodeFormat::SPIRV,eShaderPlatform::Vulkan}};
        request.Source="float4 main(uint id:SV_VertexID):SV_Position {return MissingFunction();}";
        Check(!compiler->Compile(request,result)&&result.Codes.empty()&&result.Error.find("Direct.shader")!=std::string::npos,"Diagnostics did not identify original shader");
        GameSprout::GameSproutEditorFinal::Release(compiler,nullptr);
        std::cout<<"PASS standalone SDL/DXC compiler, SPIR-V/MSL/DXIL, parallel calls, cancellation, Metal target and diagnostics\n";
        return 0;
    } catch(const std::exception & error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
