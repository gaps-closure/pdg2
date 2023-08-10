#include "SVF-LLVM/LLVMUtil.h"
#include "Graphs/SVFG.h"
#include "WPA/Andersen.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"
#include "WPA/Steensgaard.h"

using namespace std;
using namespace SVF;

struct Args
{
    std::string programName; 
    std::set<std::string> flags; 
    std::vector<std::string> positional; 
    Args(int argc, char** argv)
    {
        programName = argv[0];
        std::vector<std::string> args(argv + 1, argv + argc);
        for(auto arg : args) 
        {
            if(arg[0] == '-')
                flags.insert(arg);
            else
                positional.push_back(arg);
        }
    }
};

enum AnalysisType
{
    FSPTA,
    Ander,
    Steens
};

void usage_exit(Args args)
{
    std::cerr << 
        "Usage: " << args.programName 
                  << " [-fspta,-anders,-steens] <bitcode file> <node csv> <edge csv>" 
                  << "\n";
    exit(1);
}

struct Opts
{
    AnalysisType analysisType = FSPTA;
    std::string modulePath;
    std::string nodePath;
    std::string edgePath;

    Opts(Args args) 
    {
        if(args.flags.find("-fspta") != args.flags.end())
            analysisType = FSPTA;
        else if(args.flags.find("-ander") != args.flags.end())
            analysisType = Ander;
        else if(args.flags.find("-steens") != args.flags.end())
            analysisType = Steens;
        else if(args.flags.size() != 0)
            usage_exit(args);    
         
        if(args.positional.size() != 3) 
            usage_exit(args);    

        modulePath = args.positional[0];
        nodePath = args.positional[1];
        edgePath = args.positional[2];
    }

    BVDataPTAImpl* pointerAnalysis(SVFIR *pag)
    {
        switch (analysisType)
        {
        case FSPTA:
            return FlowSensitive::createFSWPA(pag);
        case Ander:
            return AndersenWaveDiff::createAndersenWaveDiff(pag);
        default:
            return Steensgaard::createSteensgaard(pag);
        }
    }
};


int main(int argc, char ** argv)
{
    Args args(argc, argv);
    Opts opts(args);
    std::vector<std::string> llModules;
    llModules.push_back(opts.modulePath);

    SVFModule* svfModule = LLVMModuleSet::buildSVFModule(llModules);

    auto mset = LLVMModuleSet::getLLVMModuleSet();

    /// Build Program Assignment Graph (SVFIR)
    SVFIRBuilder builder(svfModule);
    SVFIR* pag = builder.build();

    /// Create pointer analysis
    BVDataPTAImpl *wpa = opts.pointerAnalysis(pag);

    // Create instruction index map
    std::map<const llvm::Instruction *, size_t> instIdxMap;
    for(auto fn : *svfModule)
    {
        size_t idx = 0;
        for(auto bb : *fn)
        {
            for(auto inst : *bb)
            {
                auto llinst = llvm::dyn_cast<llvm::Instruction>(mset->getLLVMValue(inst));
                instIdxMap[llinst] = idx;
                idx++;
            }
        }
    }

    std::ofstream nodeDumpFile;
    nodeDumpFile.open(opts.nodePath);
    std::string delim = ",";
    std::string term = "\n";
    for(auto node : *pag)
    {
        auto id = node.first;
        auto var = node.second;
        nodeDumpFile << id << delim;
        nodeDumpFile << (var->isPointer() ? "pointer" : "non-pointer") << delim;
        if(var->hasValue())
        {
            auto llval = mset->getLLVMValue(var->getValue());
            if(auto inst = llvm::dyn_cast<Instruction>(llval))
            {
                nodeDumpFile << inst->getFunction()->getName().str() << delim;
                nodeDumpFile << instIdxMap[inst] << delim;
            } 
            else if(auto arg = llvm::dyn_cast<Argument>(llval))
            {
                nodeDumpFile << arg->getParent()->getName().str() << delim;
                nodeDumpFile << delim;
                nodeDumpFile << arg->getArgNo();
            } 
            else if(auto glob = llvm::dyn_cast<GlobalVariable>(llval))
            {
                nodeDumpFile << glob->getName().str() << delim << delim; 
            } 
            else if(auto fn = llvm::dyn_cast<Function>(llval))
            {
                nodeDumpFile << fn->getName().str() << delim << delim; 
            } else 
            {
                nodeDumpFile << delim << delim;
            }

        }
        nodeDumpFile << term;
    }
    nodeDumpFile.close();

    std::ofstream edgeDumpFile;
    edgeDumpFile.open(opts.edgePath);
    for(auto node : *pag) 
    {
        auto id = node.first;
        for(auto pt : wpa->getPts(id))
        {
            edgeDumpFile << id << delim << pt << term;
        }
    }
    edgeDumpFile.close();

    // clean up memory
    // AndersenWaveDiff::releaseAndersenWaveDiff();
    SVFIR::releaseSVFIR();

    LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");
    SVF::LLVMModuleSet::releaseLLVMModuleSet();

    llvm::llvm_shutdown();
    return 0;
}

