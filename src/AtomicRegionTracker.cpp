#include "AtomicRegionTracker.hpp"
#include "KSplitStatsCollector.hpp"

using namespace llvm;

void pdg::AtomicRegionTracker::setupLockPairMap()
{
  lock_pairs_map_.insert(std::make_pair("mutex_lock", "mutex_unlock"));
  lock_pairs_map_.insert(std::make_pair("_raw_spin_lock", "_raw_spin_unlock"));
  lock_pairs_map_.insert(std::make_pair("_raw_spin_lock_irq", "_raw_spin_unlock_irq"));
}

std::set<std::pair<Instruction *, Instruction *>> pdg::AtomicRegionTracker::collectCSInFunc(Function &F)
{
  std::set<std::pair<Instruction *, Instruction *>> csInFunc;
  for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I)
  {
    // 1. find all lock instruction that call to acquire a lock
    if (CallInst *CI = dyn_cast<CallInst>(&*I))
    {
      auto calledFunc = CI->getCalledFunction();
      if (calledFunc == nullptr)
        continue;
      std::string lockCall = calledFunc->getName();
      if (lock_pairs_map_.find(lockCall) == lock_pairs_map_.end())
        continue;

      std::set<Instruction *> unlockInsts;
      for (inst_iterator II = I; II != inst_end(F); ++II)
      {
        Instruction *i = &*II;
        if (CallInst *unlockCI = dyn_cast<CallInst>(i))
        {
          if (Function *calledFunc = dyn_cast<Function>(unlockCI->getCalledValue()->stripPointerCasts()))
          {
            if (calledFunc->getName().str() == lock_pairs_map_[lockCall])
              unlockInsts.insert(&*II);
          }
        }
      }

      for (Instruction *unlockCall : unlockInsts)
      {
        if (CallInst *unlockCI = dyn_cast<CallInst>(unlockCall))
        {
          // if (useSameLock(CI, unlockCI))
          csInFunc.insert(std::make_pair(CI, unlockCall));
        }
      }
    }
  }
  return csInFunc;
}

void pdg::AtomicRegionTracker::computeCriticalSectionPairs(Module &M)
{
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  for (auto &F : M)
  {
    if (F.isDeclaration())
      continue;
    auto cs_pairs_in_func = collectCSInFunc(F); // find cs in each defined functions
    // CS.insert(csInFunc.begin(), csInFunc.end());
    for (auto cs_pair : cs_pairs_in_func)
    {
      auto insts_in_cs = collectInstsInCS(cs_pair, F);
      cs_pairs_.insert(std::make_pair(cs_pair, insts_in_cs));
    }
  }
  ksplit_stats_collector.SetNumberOfCriticalSection(cs_pairs_.size());
  // errs() << "number of CS: " << CS.size() << "\n";
}

// void pdg::AtomicRegionTracker::insertAtomicOp(Instruction* inst)
// {
//   atomic_ops_.insert(inst);
// }

bool pdg::AtomicRegionTracker::isAtomicOp(Instruction *inst)
{

  if (CallInst *ci = dyn_cast<CallInst>(inst))
  {
    if (!ci->isInlineAsm())
      return false;
    if (InlineAsm *ia = dyn_cast<InlineAsm>(ci->getCalledValue()))
    {
      auto asmString = ia->getAsmString();
      if (isAtomicAsmString(asmString))
        return true;
    }
  }
  return false;
}

bool pdg::AtomicRegionTracker::isAtomicAsmString(std::string str)
{
  return (str.find("lock") != std::string::npos);
}

std::pair<Instruction *, Instruction *> pdg::AtomicRegionTracker::getCSUseInst(Instruction *inst)
{
  for (auto cs_pair : cs_pairs_)
  {
    auto insts_in_cs = cs_pair.second;
    if (insts_in_cs.find(inst) != insts_in_cs.end())
      return cs_pair.first;
  }
  return std::make_pair(nullptr, nullptr);
}

Instruction * pdg::AtomicRegionTracker::getAtomicOpUseInst(Instruction *inst)
{
  for (auto user : inst->users())
  {
    if (Instruction *i = dyn_cast<Instruction>(user))
    {
      if (atomic_ops_.find(i) != atomic_ops_.end())
        return i;
    }
  }
  return nullptr;
}

std::set<Instruction *> pdg::AtomicRegionTracker::collectInstsInCS(std::pair<Instruction *, Instruction *> lockPair, Function &F)
{
  std::set<Instruction *> instSet;
  auto instI = inst_begin(F);
  while (instI != inst_end(F))
  {
    if (&*instI == lockPair.first)
      break;
    instI++;
  }

  while (&*instI != lockPair.second && instI != inst_end(F))
  {
    instSet.insert(&*instI);
    instI++;
  }

  return instSet;
}

void pdg::AtomicRegionTracker::computeAtomicOperations(Module &M)
{
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  for (auto &F : M)
  {
    if (F.isDeclaration())
      continue;
    for (auto instI = inst_begin(F); instI != inst_end(F); instI++)
    {
      if (isAtomicOp(&*instI))
        atomic_ops_.insert(&*instI);
    }
  }
  ksplit_stats_collector.SetNumberOfAtomicOperation(atomic_ops_.size());
}