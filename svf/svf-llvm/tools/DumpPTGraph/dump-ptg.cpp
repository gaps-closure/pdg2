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

std::string nodeKindToString(SVF::SVFVar::PNODEK kind)
{
    switch (kind) {
    case SVF::SVFVar::ValNode:
        return "ValNode";
    case SVF::SVFVar::ObjNode:
        return "ObjNode";
    case SVF::SVFVar::RetNode:
        return "RetNode";
    case SVF::SVFVar::VarargNode:
        return "VarargNode";
    case SVF::SVFVar::GepValNode:
        return "GepValNode";
    case SVF::SVFVar::GepObjNode:
        return "GepObjNode";
    case SVF::SVFVar::FIObjNode:
        return "FIObjNode";
    case SVF::SVFVar::DummyValNode:
        return "DummyValNode";
    case SVF::SVFVar::DummyObjNode:
        return "DummyObjNode";
    default:
        return "Unknown Kind";
    }
}

const llvm::Instruction* getReturn(const llvm::Function* f)
{
    for(const llvm::BasicBlock& bb : *f)
    {
        auto term = bb.getTerminator();
        if(llvm::isa<llvm::ReturnInst>(term))
            return term; 
    }
    return NULL;
}

std::string valueToString(const SVF::Value *v)
{
    std::string s;
    llvm::raw_string_ostream os(s);
    os << *v;
    return s;
}



int main(int argc, char ** argv)
{
    Args args(argc, argv);
    Opts opts(args);
    std::vector<std::string> llModules;
    llModules.push_back(opts.modulePath);

    SVFModule* svfModule = LLVMModuleSet::buildSVFModule(llModules);

    auto mset = LLVMModuleSet::getLLVMModuleSet();

    // Create instruction index map
    std::map<const llvm::Instruction *, size_t> instIdxMap;
    for(auto fn : *svfModule)
    {
        size_t idx = 0;
        auto llvm_fn = llvm::dyn_cast<llvm::Function>(mset->getLLVMValue(fn));
        for(const llvm::BasicBlock& bb : *llvm_fn)
        {
            for(const llvm::Instruction& inst : bb)
            {
                instIdxMap[&inst] = idx;
                idx++;
            }
        }
    }


    /// Build Program Assignment Graph (SVFIR)
    SVFIRBuilder builder(svfModule);
    SVFIR* pag = builder.build();

    /// Create pointer analysis
    BVDataPTAImpl *wpa = opts.pointerAnalysis(pag);

    std::ofstream nodeDumpFile;
    nodeDumpFile.open(opts.nodePath);
    std::string delim = ",";
    std::string term = "\n";
    for(auto node : *pag)
    {
        auto id = node.first;
        auto var = node.second;
        nodeDumpFile << id << delim;
        nodeDumpFile << nodeKindToString((SVF::SVFVar::PNODEK) var->getNodeKind()) << delim;
        nodeDumpFile << (var->isPointer() ? "pointer" : "non-pointer") << delim;
        if(var->hasValue())
        {
            auto llval = mset->getLLVMValue(var->getValue());
            nodeDumpFile << "'" << valueToString(llval) << "'" << delim;
            if(auto inst = llvm::dyn_cast<Instruction>(llval))
            {
                nodeDumpFile << delim;
                nodeDumpFile << inst->getFunction()->getName().str() << delim;
                nodeDumpFile << instIdxMap[inst] << delim;
            } 
            else if(auto arg = llvm::dyn_cast<Argument>(llval))
            {
                nodeDumpFile << delim;
                nodeDumpFile << arg->getParent()->getName().str() << delim;
                nodeDumpFile << delim;
                nodeDumpFile << arg->getArgNo();
            } 
            else if(auto glob_val = llvm::dyn_cast<GlobalValue>(llval))
            {
                if(glob_val->isDeclaration())
                    nodeDumpFile << "declaration" << delim;
                else
                    nodeDumpFile << "definition" << delim;

                if(auto glob = llvm::dyn_cast<GlobalVariable>(llval))
                {
                    nodeDumpFile << glob->getName().str() << delim << delim; 
                } 
                else if(auto fn = llvm::dyn_cast<Function>(llval))
                {
                    if(var->getNodeKind() == SVFVar::RetNode)
                    {
                        auto ret = getReturn(fn);
                        if(ret)
                            nodeDumpFile << fn->getName().str() << delim << instIdxMap[ret] << delim;
                        else
                            nodeDumpFile << fn->getName().str() << delim << delim; 
                    }
                    else 
                        nodeDumpFile << fn->getName().str() << delim << delim; 
                }
            }
            else 
            {
                nodeDumpFile << delim << delim << delim;
            }

        } else
        {
            nodeDumpFile << delim << delim << delim;
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

