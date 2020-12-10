#ifndef ATOMICREGION_TRACKER_H_
#define ATOMICREGION_TRACKER_H_
#include "PDGUtils.hpp"

namespace pdg
{
  class AtomicRegionTracker final
  {
  private:
    std::map<std::string, std::string> lock_pairs_map_;
    std::map<std::pair<llvm::Instruction *, llvm::Instruction *>, std::set<llvm::Instruction *>> cs_pairs_;
    std::set<llvm::Instruction *> atomic_ops_;

  public:
    AtomicRegionTracker() = default;
    AtomicRegionTracker(const AtomicRegionTracker &) = delete;
    AtomicRegionTracker(AtomicRegionTracker &&) = delete;
    AtomicRegionTracker &operator=(const AtomicRegionTracker &) = delete;
    AtomicRegionTracker &operator=(AtomicRegionTracker &&) = delete;
    // ~KSplitStatsCollector();
    static AtomicRegionTracker &getInstance()
    {
      static AtomicRegionTracker atomic_region_tracker{};
      return atomic_region_tracker;
    }
    void setupLockPairMap();
    std::set<std::pair<llvm::Instruction *, llvm::Instruction *>> collectCSInFunc(llvm::Function &F);
    void computeCriticalSections(llvm::Module &M);
    void computeCriticalSectionPairs(llvm::Module &M);
    void computeAtomicOperations(llvm::Module &M);
    bool isAtomicOp(llvm::Instruction *inst);
    bool isAtomicAsmString(std::string str);
    std::pair<llvm::Instruction *, llvm::Instruction *> getCSUseInst(llvm::Instruction *inst);
    llvm::Instruction *getAtomicOpUseInst(llvm::Instruction *inst);
    std::set<llvm::Instruction *> collectInstsInCS(std::pair<llvm::Instruction *, llvm::Instruction *> lockPair, llvm::Function &F);
  };
} // namespace pdg
#endif