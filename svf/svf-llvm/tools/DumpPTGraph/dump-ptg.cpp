#include "SVF-LLVM/LLVMUtil.h"
#include "Graphs/SVFG.h"
#include "WPA/Andersen.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"

using namespace std;
using namespace SVF;

int main(int argc, char ** argv)
{

    assert(argc == 4);
    std::string llModule(argv[1]);
    std::string nodeDump(argv[2]);
    std::string edgeDump(argv[3]);

    std::vector<std::string> llModules;
    llModules.push_back(llModule);

    SVFModule* svfModule = LLVMModuleSet::buildSVFModule(llModules);

    auto mset = LLVMModuleSet::getLLVMModuleSet();

    /// Build Program Assignment Graph (SVFIR)
    SVFIRBuilder builder(svfModule);
    SVFIR* pag = builder.build();

    /// Create Andersen's pointer analysis
    Andersen* ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
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
    nodeDumpFile.open(nodeDump);
    std::string delim = ",";
    std::string term = "\n";
    for(auto node : *pag)
    {
        auto id = node.first;
        auto var = node.second;
        nodeDumpFile << id << delim;
        if(var->hasValue())
        {
            auto llval = mset->getLLVMValue(var->getValue());
            if(auto inst = llvm::dyn_cast<Instruction>(llval))
            {
                nodeDumpFile << inst->getFunction()->getName().str() << delim;
                nodeDumpFile << instIdxMap[inst] << delim;
                nodeDumpFile << delim;
            } else if(auto arg = llvm::dyn_cast<Argument>(llval))
            {
                nodeDumpFile << arg->getParent()->getName().str() << delim;
                nodeDumpFile << delim;
                nodeDumpFile << arg->getArgNo() << delim;
            } else if(auto glob = llvm::dyn_cast<GlobalVariable>(llval))
            {
                nodeDumpFile << glob->getName().str() << delim << delim; 
            } else if(auto fn = llvm::dyn_cast<Function>(llval))
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
    edgeDumpFile.open(edgeDump);
    for(auto node : *pag) 
    {
        auto id = node.first;
        for(auto pt : ander->getPts(id))
        {
            edgeDumpFile << id << delim << pt << term;
        }
    }
    edgeDumpFile.close();

    // clean up memory
    AndersenWaveDiff::releaseAndersenWaveDiff();
    SVFIR::releaseSVFIR();

    LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");
    SVF::LLVMModuleSet::releaseLLVMModuleSet();

    llvm::llvm_shutdown();
    return 0;
}

