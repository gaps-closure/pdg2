#include "DebugInfoUtils.hpp"
#include <queue>
#include <set>

using namespace llvm;

static std::map<std::string, std::string> typeSwitchMap = {
    {"_Bool", "bool"},
    {"char", "u8"},
    {"signed char", "u8"},
    {"unsigned char", "u8"},
    {"short", "u16"},
    {"short int", "u16"},
    {"signed short", "u16"},
    {"unsigned short", "u16"},
    {"signed short int", "u16"},
    {"unsigned short int", "u16"},
    {"int", "u32"},
    {"signed", "u32"},
    {"unsigned int", "u32"},
    {"long", "u32"},
    {"long int", "u32"},
    {"signed long", "u32"},
    {"signed long int", "u32"},
    {"unsigned long", "u32"},
    {"unsigned long int", "u32"},
    {"long long unsigned int", "u64"},
    {"long long", "u64"},
    {"long long int", "u64"},
    {"signed long long", "u64"},
    {"signed long long int", "u64"},
    {"unsigned long long", "u64"},
    {"unsigned long long int", "u64"},
    {"long unsigned int", "u64"},
};

std::string pdg::DIUtils::getArgName(Argument& arg)
{
  Function *F = arg.getParent();
  auto dbgInsts = collectDbgInstInFunc(*F);
  std::vector<DbgInfoIntrinsic *> dbgInstList(dbgInsts.begin(), dbgInsts.end());
  SmallVector<std::pair<unsigned, MDNode *>, 20> func_MDs;
  for (auto dbgInst : dbgInstList)
  {
    DILocalVariable *DLV = nullptr;
    if (auto declareInst = dyn_cast<DbgDeclareInst>(dbgInst))
      DLV = declareInst->getVariable();
    if (auto valueInst = dyn_cast<DbgValueInst>(dbgInst))
      DLV = valueInst->getVariable();
    if (!DLV)
      continue;
    if (DLV->getArg() == arg.getArgNo() + 1 && !DLV->getName().empty() && DLV->getScope()->getSubprogram() == F->getSubprogram())
      return DLV->getName().str();
  }

  return "";
}

DIType *pdg::DIUtils::stripAttributes(DIType *Ty)
{
  DIType* ret = Ty;
  while (ret->getTag() == dwarf::DW_TAG_typedef ||
         ret->getTag() == dwarf::DW_TAG_const_type ||
         ret->getTag() == dwarf::DW_TAG_volatile_type)
  {
    if (auto didt = dyn_cast<DIDerivedType>(ret))
    {
      if (!didt->getBaseType())
        break;
      DIType *baseTy = didt->getBaseType().resolve();
      if (!baseTy)
        break;
      ret = baseTy;
    }
    else 
      break;
  }
  return ret;
}

DIType *pdg::DIUtils::getLowestDIType(DIType *Ty) 
{
  DIType* tmp_di_ty = Ty;
  while (tmp_di_ty->getTag() == dwarf::DW_TAG_pointer_type ||
      tmp_di_ty->getTag() == dwarf::DW_TAG_member ||
      tmp_di_ty->getTag() == dwarf::DW_TAG_typedef ||
      tmp_di_ty->getTag() == dwarf::DW_TAG_const_type)
  {
    if (auto *di_derived_type = dyn_cast<DIDerivedType>(tmp_di_ty))
    {
      DIType *baseTy = di_derived_type->getBaseType().resolve();
      if (baseTy == nullptr)
        return nullptr;
      tmp_di_ty = baseTy;
    }
    else
      break;
  }
  if (tmp_di_ty != nullptr)
    return tmp_di_ty;
  return Ty;
}

std::set<DbgInfoIntrinsic *> pdg::DIUtils::collectDbgInstInFunc(Function &F)
{
  std::set<DbgInfoIntrinsic *> ret;
  for (auto instI = inst_begin(&F); instI != inst_end(&F); ++instI)
  {
    if (DbgInfoIntrinsic *dbi = dyn_cast<DbgInfoIntrinsic>(&*instI))
      ret.insert(dbi);
  }
  return ret;
}

// DIType *pdg::DIUtils::getArgDIType(Argument &arg)
// {
//   Function &F = *arg.getParent();
//   std::set<DbgInfoIntrinsic*> dbgInsts = collectDbgInstInFunc(F);
//   for (auto dbgInst : dbgInsts)
//   {
//     DILocalVariable *DLV = nullptr;
//     if (auto declareInst = dyn_cast<DbgDeclareInst>(dbgInst))
//       DLV = declareInst->getVariable();
//     if (auto valueInst = dyn_cast<DbgValueInst>(dbgInst))
//       DLV = valueInst->getVariable();
//     if (!DLV)
//       continue;
//     if (DLV->getArg() == arg.getArgNo() + 1 && !DLV->getName().empty() && DLV->getScope()->getSubprogram() == F.getSubprogram())
//       return DLV->getType().resolve();
//   }
//   return nullptr;
// }

DIType *pdg::DIUtils::getArgDIType(Argument &arg)
{
  Function &F = *arg.getParent();
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto &MD : MDs)
  {
    MDNode *N = MD.second;
    if (DISubprogram *subprogram = dyn_cast<DISubprogram>(N))
    {
      auto *subRoutine = subprogram->getType();
      const auto &TypeRef = subRoutine->getTypeArray();
      if (F.arg_size() >= TypeRef.size())
        break;
      int metaDataLoc = 0;
      if (arg.getArgNo() != 100)
        metaDataLoc = arg.getArgNo() + 1;
      const auto &ArgTypeRef = TypeRef[metaDataLoc]; // + 1 to skip return type
      DIType *Ty = ArgTypeRef.resolve();
      return Ty;
    }
  }
  return nullptr;
  // throw ArgHasNoDITypeException("Argument doesn't has DIType needed to extract field name...");
}

DIType *pdg::DIUtils::getFuncRetDIType(Function &F)
{
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto &MD : MDs)
  {
    MDNode *N = MD.second;
    if (DISubprogram *subprogram = dyn_cast<DISubprogram>(N))
    {
      auto *subRoutine = subprogram->getType();
      const auto &TypeRef = subRoutine->getTypeArray();
      if (F.arg_size() >= TypeRef.size())
        break;
      const auto &ArgTypeRef = TypeRef[0];
      DIType *Ty = ArgTypeRef.resolve();
      return Ty;
    }
  }
  return nullptr;
  // throw ArgHasNoDITypeException("Argument doesn't has DIType needed to extract field name...");
}

DIType *pdg::DIUtils::getGlobalVarDIType(GlobalVariable &globalVar)
{
  SmallVector<DIGlobalVariableExpression *, 5> GVs;
  globalVar.getDebugInfo(GVs);
  if (GVs.size() == 0)
    return nullptr;

  for (auto GV : GVs)
  {
    DIGlobalVariable* digv = GV->getVariable();
    return digv->getType().resolve();
  }
  return nullptr;
}

DIType *pdg::DIUtils::getBaseDIType(DIType *dt) {
  if (dt == nullptr)
    throw DITypeIsNullPtr("DIType is nullptr, cannot get base type");

  auto type_tag = dt->getTag();
  if (type_tag == dwarf::DW_TAG_pointer_type ||
      type_tag == dwarf::DW_TAG_member ||
      type_tag == dwarf::DW_TAG_typedef ||
      type_tag == dwarf::DW_TAG_const_type ||
      type_tag == dwarf::DW_TAG_volatile_type)
  {
    DIType *baseTy = dyn_cast<DIDerivedType>(dt)->getBaseType().resolve();
    if (!baseTy)
      return nullptr;
    return baseTy;
  }
  return dt;
}

std::string pdg::DIUtils::getDIFieldName(DIType *ty)
{
  if (ty == nullptr)
    return "void";
  switch (ty->getTag())
  {
  case dwarf::DW_TAG_member:
  {
    return ty->getName().str();
  }
  case dwarf::DW_TAG_array_type:
  {
    ty = dyn_cast<DICompositeType>(ty)->getBaseType().resolve();
    return ty->getName().str();
  }
  case dwarf::DW_TAG_pointer_type:
  {
    std::string s = getDIFieldName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    return s;
  }
  case dwarf::DW_TAG_subroutine_type:
    return "";
  case dwarf::DW_TAG_const_type:
  {
    std::string s = getDIFieldName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    return s;
  }
  default:
  {
    if (!ty->getName().str().empty())
      return ty->getName().str();
    return "";
  }
  }
}

std::string pdg::DIUtils::getFuncSigName(DIType *ty, Function *F, std::string funcPtrName, std::string funcName, bool callFromDev)
{
  std::string func_type_str = "";
  if (DISubroutineType *subRoutine = dyn_cast<DISubroutineType>(ty))
  {
    const auto &typeRefArr = subRoutine->getTypeArray();
    // generate name string for return value
    DIType *retType = typeRefArr[0].resolve();
    if (retType == nullptr)
      func_type_str += "void ";
    else
      func_type_str += getDITypeName(retType);

    // generate name string for function pointer 
    func_type_str += " (";
    if (!funcPtrName.empty())
      func_type_str += "*";
    func_type_str += funcPtrName;
    if (!funcName.empty())
      func_type_str = func_type_str + "_" + funcName;
    func_type_str += ")";
    // generate name string for arguments in fucntion pointer signature
    func_type_str += "(";
    for (int i = 1; i < typeRefArr.size(); ++i)
    {
      DIType *d = typeRefArr[i].resolve();
      // retrieve naming info from debugging information for each argument
      std::string argName = getDIFieldName(d);
      if (F != nullptr)
      {
        unsigned argNum = i - 1;
        unsigned ptr = 0;
        for (auto argI = F->arg_begin(); argI != F->arg_end(); ++argI)
        {
          if (ptr == argNum)
          {
            argName = getArgName(*argI);
            break;
          }
          ptr++;
        }
      }

      if (d == nullptr) // void type
        func_type_str += "void ";
      else // normal types
      {
        if (DIDerivedType *dit = dyn_cast<DIDerivedType>(d))
        {
          auto baseType = dit->getBaseType().resolve();
          if (!baseType)
          {
            // if a DIderived type has a null base type, this normally 
            // represent a void pointer
            func_type_str += "void* ";
          }
          else if (baseType->getTag() == dwarf::DW_TAG_structure_type)
          {
            std::string argTyName = getDITypeName(d);
            if (F != nullptr && actualArgHasAllocator(*F, i - 1))
              argTyName = "alloc[callee] " + argTyName;
            if (argTyName.back() == '*')
            {
              argTyName.pop_back();
              argTyName = argTyName + "_" + funcPtrName + "*";
            }
            else
            {
              argTyName = argTyName + "_" + funcPtrName;
            }

            std::string structName = argTyName + " " + argName;
            if (structName != " ")
              func_type_str = func_type_str + "projection " + structName;
          }
          else
            func_type_str = func_type_str + getDITypeName(d) + " " + argName;
        }
        else
          func_type_str = func_type_str + getDITypeName(d);
      }

      if (i < typeRefArr.size() - 1 && !getDITypeName(d).empty())
        func_type_str += ", ";
    }
    func_type_str += ")";
    return func_type_str;
  }
  return "void";
}

std::string pdg::DIUtils::getDITypeName(DIType *ty)
{
  if (ty == nullptr)
    return "void";
  if (!ty->getTag() || ty == NULL) // process function type, which has not tag
    return getFuncSigName(ty);
  try
  {
    switch (ty->getTag())
    {
    case dwarf::DW_TAG_typedef:
      return getDITypeName(getBaseDIType(ty)); // need to know the underlying name
      // return ty->getName(); // directly return typedef name
    case dwarf::DW_TAG_member:
    {
      auto baseTypeName = getDITypeName(getBaseDIType(ty));
      if (baseTypeName == "struct")
        baseTypeName = baseTypeName + " " + ty->getName().str();
      return baseTypeName;
    }
    case dwarf::DW_TAG_array_type:
    {
      if (DIType *arrTy = dyn_cast<DICompositeType>(ty)->getBaseType().resolve())
      {
        auto containedTypeName = getDITypeName(arrTy);
        std::string pointerLevel = "";
        while (containedTypeName.back() == '*')
        {
          containedTypeName.pop_back();
          pointerLevel += "*";
        }

        if (arrTy->getSizeInBits() != 0)
          return "array<" + containedTypeName + ", " +  std::to_string(ty->getSizeInBits() / arrTy->getSizeInBits()) + ">" + pointerLevel;
          // return containedTypeName + "[" + std::to_string(ty->getSizeInBits() / arrTy->getSizeInBits()) + "]";
        else
          return "array<" + containedTypeName + ", " + "var_len" + ">";
      }
    }
    case dwarf::DW_TAG_pointer_type:
    {
      std::string s = getDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
      return s + "*";
    }
    case dwarf::DW_TAG_subroutine_type:
      return getFuncSigName(ty);
    case dwarf::DW_TAG_union_type:
      return "union";
    case dwarf::DW_TAG_structure_type:
    {
      std::string st_name = ty->getName().str();
      if (!st_name.empty())
        return ("struct " + st_name);
      return "struct";
    }
    case dwarf::DW_TAG_const_type:
      return "const " + getDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    case dwarf::DW_TAG_enumeration_type:
    {
      if (!ty->getName().str().empty())
        return "enum " + ty->getName().str();
      return "enum";
    }
    case dwarf::DW_TAG_volatile_type:
      return "volatile " + getDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    default:
    {
      std::string typeName = ty->getName().str();
      if (!typeName.empty())
      {
        if (typeSwitchMap.find(typeName) != typeSwitchMap.end())
          return typeSwitchMap[typeName];
        return typeName;
      }
      return "unknow";
    }
    }
  }
  catch (std::exception &e)
  {
    errs() << e.what();
    exit(0);
  }
}

// return type name without qulifier
std::string pdg::DIUtils::getRawDITypeName(DIType* ty)
{
  if (ty == nullptr)
    return "void";
  if (!ty->getTag() || ty == NULL) // process function type, which has not tag
    return getFuncSigName(ty);
  try
  {
    switch (ty->getTag())
    {
    case dwarf::DW_TAG_typedef:
      return getRawDITypeName(getBaseDIType(ty)); // need to know the underlying name
      // return ty->getName(); // directly return typedef name
    case dwarf::DW_TAG_member:
    {
      auto baseTypeName = getRawDITypeName(getBaseDIType(ty));
      if (baseTypeName == "struct")
        baseTypeName = baseTypeName + " " + ty->getName().str();
      return baseTypeName;
    }
    case dwarf::DW_TAG_array_type:
    {
      if (DIType *arrTy = dyn_cast<DICompositeType>(ty)->getBaseType().resolve())
      {
        auto containedTypeName = getRawDITypeName(arrTy);
        if (arrTy->getSizeInBits() != 0)
          return "array<" + containedTypeName + ", " +  std::to_string(ty->getSizeInBits() / arrTy->getSizeInBits()) + ">";
          // return containedTypeName + "[" + std::to_string(ty->getSizeInBits() / arrTy->getSizeInBits()) + "]";
        else
          return "array<" + containedTypeName + ", " + "var_len" + ">";
      }
    }
    case dwarf::DW_TAG_pointer_type:
      return getRawDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    case dwarf::DW_TAG_subroutine_type:
      return getFuncSigName(ty);
    case dwarf::DW_TAG_union_type:
      return "union";
    case dwarf::DW_TAG_structure_type:
    {
      std::string st_name = ty->getName().str();
      if (!st_name.empty())
        return ("struct " + st_name);
      return "struct";
    }
    case dwarf::DW_TAG_const_type:
      return getRawDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    case dwarf::DW_TAG_enumeration_type:
    {
      if (!ty->getName().str().empty())
        return "enum " + ty->getName().str();
      return "enum";
    }
    case dwarf::DW_TAG_volatile_type:
      return getRawDITypeName(dyn_cast<DIDerivedType>(ty)->getBaseType().resolve());
    default:
    {
      std::string typeName = ty->getName().str();
      if (!typeName.empty())
      {
        if (typeSwitchMap.find(typeName) != typeSwitchMap.end())
          return typeSwitchMap[typeName];
        return typeName;
      }
      return "[unknow]";
    }
    }
  }
  catch (std::exception &e)
  {
    errs() << e.what();
    exit(0);
  }

}

std::string pdg::DIUtils::getArgTypeName(Argument &arg)
{
  return getDITypeName(getArgDIType(arg));
}

void pdg::DIUtils::printStructFieldNames(DINodeArray DINodeArr)
{
  for (auto DINode : DINodeArr)
    errs() << dyn_cast<DIType>(DINode)->getName() << "\n";
}

bool pdg::DIUtils::isPointerType(DIType *dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  if (dt != nullptr)
    return (dt->getTag() == dwarf::DW_TAG_pointer_type);
  return false;
}

bool pdg::DIUtils::isVoidPointer(DIType *dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  if (dt->getTag() == dwarf::DW_TAG_pointer_type) {
    auto baseTy = getLowestDIType(dt);
    if (baseTy == nullptr) 
      return true;
  }
  return false;
}

bool pdg::DIUtils::isStructPointerTy(DIType *dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  dt = stripAttributes(dt);
  if (!dt)
    return false;
  if (dt->getTag() == dwarf::DW_TAG_pointer_type) {
    auto baseTy = getLowestDIType(dt);
    if (baseTy != nullptr) 
      return (baseTy->getTag() == dwarf::DW_TAG_structure_type);
  }
  return false;
}

bool pdg::DIUtils::isUnionPointerTy(DIType *dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  if (dt->getTag() == dwarf::DW_TAG_pointer_type) {
    auto baseTy = getLowestDIType(dt);
    if (baseTy != nullptr) 
      return (baseTy->getTag() == dwarf::DW_TAG_union_type);
  }
  return false;
}

bool pdg::DIUtils::IsPointerToProjectableTy(DIType* dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  if (dt->getTag() == dwarf::DW_TAG_pointer_type)
  {
    auto di_lowest_type = getLowestDIType(dt);
    if (di_lowest_type != nullptr)
      return isProjectableTy(di_lowest_type);
  }
  return false;
}

bool pdg::DIUtils::isStructTy(DIType *dt)
{
  if (dt == nullptr)
    return false;
  if (dt->getTag() == dwarf::DW_TAG_pointer_type)
    return false;
  auto baseTy = getLowestDIType(dt); // strip off tag member type
  if (baseTy != nullptr)
    return (baseTy->getTag() == dwarf::DW_TAG_structure_type);
  return false;
}

bool pdg::DIUtils::isFuncPointerTy(DIType *dt)
{
  if (dt == nullptr)
    return false;
  dt = stripMemberTag(dt);
  if (dt->getTag() == dwarf::DW_TAG_subroutine_type || isa<DISubroutineType>(dt) || isa<DISubprogram>(dt))
    return true;
  auto lowest_di_type = getLowestDIType(dt);
  if (lowest_di_type != nullptr)
    return (lowest_di_type->getTag() == dwarf::DW_TAG_subroutine_type) || isa<DISubroutineType>(lowest_di_type) || isa<DISubprogram>(lowest_di_type);
  return false;
}

DIType *pdg::DIUtils::stripMemberTag(DIType *dt)
{
  if (dt == nullptr)
    return dt;
  if (dt->getTag() == dwarf::DW_TAG_member)
    return getBaseDIType(dt);
  return dt;
}

DIType *pdg::DIUtils::getFuncDIType(Function* func)
{
  auto subProgram = func->getSubprogram();
  return subProgram->getType();
}

std::vector<Function *> pdg::DIUtils::collectIndirectCallCandidatesWithDI(DIType *funcDIType, Module *module, std::map<std::string, std::string> funcptrTargetMap)
{
  std::vector<Function *> indirectCallList;
  for (auto &F : *module)
  {
    std::string funcName = F.getName().str();
    // get Function type
    if (funcName == "main" || !F.getSubprogram() || F.isDeclaration())
      continue;
    // compare the indirect call function type with each function, filter out certian functions that should not be considered as call targets
    if (funcptrTargetMap[DIUtils::getDIFieldName(funcDIType)] == F.getName())
    {
      indirectCallList.push_back(&F);
    }
  }

  return indirectCallList;
}

DIType *pdg::DIUtils::getInstDIType(Instruction* inst, std::vector<DbgInfoIntrinsic *> dbgInstList)
{
 for (auto dbgInst : dbgInstList)
  {
    if (dbgInst->getVariableLocation() != inst)
      continue;
    DILocalVariable *DLV = nullptr;
    if (auto declareInst = dyn_cast<DbgDeclareInst>(dbgInst))
      DLV = declareInst->getVariable();
    if (auto valueInst = dyn_cast<DbgValueInst>(dbgInst))
      DLV = valueInst->getVariable();
    if (!DLV)
      continue;
    return DLV->getType().resolve();
  }
  return nullptr;
}

DbgDeclareInst* pdg::DIUtils::getDbgInstForInst(Instruction* inst, std::set<DbgDeclareInst *> dbgInstList)
{
 for (auto dbi : dbgInstList)
  {
    if (dbi->getVariableLocation() != inst)
      continue;
    if (auto DLV = dyn_cast<DILocalVariable>(dbi->getVariable()))
      return dbi;
  }
  return nullptr;
}

// std::set<std::string> pdg::DIUtils::computeSharedDataType(std::set<Function*> crossDomainFunctions)
// {
//   std::set<std::string> sharedDataTypes;
//   // compute shared type for global variables
//   for (Module::global_iterator globalIt = M.global_begin(); globalIt != M.global_end(); ++globalIt)
//   {
//     if (GlobalVariable *globalVar = dyn_cast<GlobalVariable>(&*globalIt))
//     {
//       // 2. check if a global var is of struct pointer type
//       DIType* globalVarDIType = getGlobalVarDIType(*globalVar);
//       if (!globalVarDIType) 
//         continue;
//       if (isStructPointerTy(globalVarDIType))
//       {
//         sharedDataTypes.insert(getDIFieldName(globalVarDIType));
//       }
//     }
//   }

//   // compute shared data type for functions
//   for (auto F : crossDomainFunctions)
//   {
//     if (F->isDeclaration() || F->empty())
//       continue;
//     for (auto &arg : F->args())
//     {
//       // do not process non-struct ptr type, struct type is coersed
//       DIType *argDIType = DIUtils::getArgDIType(arg);
//       if (!DIUtils::isStructPointerTy(argDIType))
//         continue;
//       // check if shared fields for this struct type is already done
//       std::string argTypeName = DIUtils::getArgTypeName(arg);
//       sharedDataTypes.insert(argTypeName);
//     }
//   }
//   return sharedDataTypes;
// }

std::string pdg::DIUtils::computeFieldID(DIType *structDIType, DIType *fieldDIType)
{
  std::string structTypeName = "";
  std::string childName = "";
  if (structDIType != nullptr)
  {
    structDIType = DIUtils::stripAttributes(structDIType);
    structTypeName = DIUtils::getDITypeName(structDIType);
  }
  if (fieldDIType != nullptr)
  {
    fieldDIType = DIUtils::stripAttributes(fieldDIType);
    childName = DIUtils::getDIFieldName(fieldDIType);
  }
  std::string id =  structTypeName + childName;
  return id;
}

std::string pdg::DIUtils::getInvalidTypeStr(DIType* dt)
{
  std::queue<DIType*> typeQ;
  std::set<DIType*> seenType;
  typeQ.push(dt);
  while (!typeQ.empty())
  {
    DIType* dt = typeQ.front();
    typeQ.pop();
    // check for invalid type
    if (isUnionTy(dt))
      return "union type";

    if (isArrayType(dt))
      return "array type";

    if (seenType.find(dt) != seenType.end())
      continue;
    seenType.insert(dt);
    if (dt->getTag() == dwarf::DW_TAG_structure_type)
    {
      auto DINodeArr = dyn_cast<DICompositeType>(dt)->getElements();
      for (unsigned i = 0; i < DINodeArr.size(); ++i)
      {
        if (DIType *tmpDI = dyn_cast<DIType>(DINodeArr[i]))
          typeQ.push(tmpDI);
      }
    }
    DIType* baseTy = getBaseDIType(dt);
    if (baseTy != nullptr && baseTy != dt)
      typeQ.push(baseTy);
  }
  return "";
}

std::string pdg::DIUtils::ComputePointerLevelStr(DIType* dt)
{
  std::string pointer_level_str = "";
  std::string type_name = getDITypeName(dt);
  auto tmp_ty = dt;
  while (tmp_ty != nullptr)
  {
    if (tmp_ty->getTag() == dwarf::DW_TAG_pointer_type)
    {
      pointer_level_str += "*";
      type_name.pop_back();
    }
    auto di = getBaseDIType(tmp_ty);
    if (di == nullptr || di == tmp_ty)
      break;
    tmp_ty = di;
  }
  return pointer_level_str;
}

bool pdg::DIUtils::isUnionTy(DIType *dt)
{
  if (dt == nullptr)
    return false;
  if (dt->getTag() == dwarf::DW_TAG_pointer_type)
    return false;
  auto baseTy = getLowestDIType(dt); // strip off tag member type
  if (baseTy != nullptr)
    return (baseTy->getTag() == dwarf::DW_TAG_union_type);
  return false;
}

bool pdg::DIUtils::isArrayType(DIType *dt)
{
  dt = stripMemberTag(dt);
  if (dt)
    return (dt->getTag() == dwarf::DW_TAG_array_type);
  return false;
}

bool pdg::DIUtils::actualArgHasAllocator(Function& F, unsigned argIdx)
{
  for (auto user : F.users())
  {
    if (CallInst *ci = dyn_cast<CallInst>(user))
    {
      if (argIdx >= ci->getNumArgOperands())
        return false;
      Value* operand = ci->getOperand(argIdx);
      if (isa<GlobalVariable>(operand)) // the function struct should be assigned from a global var.
        return true;
    }
  }
  return false;
}

unsigned pdg::DIUtils::computeTotalFieldNumberInStructType(DIType* dt)
{
  if (!isStructPointerTy(dt) && !isStructTy(dt))
    return 0;
  std::queue<DIType*> workQ;
  std::set<DIType*> seenTypes;
  workQ.push(dt);
  unsigned fieldNum = 0;
  while (!workQ.empty())
  {
    DIType* curDIType = workQ.front();
    workQ.pop();
    DIType* lowestDIType = getLowestDIType(curDIType);
    if (seenTypes.find(lowestDIType) != seenTypes.end())
      continue;
    seenTypes.insert(lowestDIType);
    auto DINodeArr = dyn_cast<DICompositeType>(lowestDIType)->getElements();
    fieldNum += DINodeArr.size();
    for (unsigned i = 0; i < DINodeArr.size(); ++i)
    {
      DIType *fieldDIType = dyn_cast<DIType>(DINodeArr[i]);
      fieldDIType = getLowestDIType(fieldDIType);
      if (isStructTy(fieldDIType) || isUnionTy(fieldDIType))
        workQ.push(fieldDIType);
    }
  }
  return fieldNum;
}

std::set<DIType *> pdg::DIUtils::collectSharedDITypes(Module &M, std::set<Function *> crossDomainFuncs, int tree_max_height)
{
  std::set<DIType*> sharedDITypes;
  std::set<std::string> seenDITypeName;
  // collect shared type from global variables
  for (Module::global_iterator globalIt = M.global_begin(); globalIt != M.global_end(); ++globalIt)
  {
    if (GlobalVariable *globalVar = dyn_cast<GlobalVariable>(&*globalIt))
    {
      // 2. check if a global var is of struct pointer type
      DIType* globalVarDIType = getGlobalVarDIType(*globalVar);
      if (!globalVarDIType) 
        continue;
      auto containedSharedTypes = computeContainedDerivedTypes(globalVarDIType, tree_max_height);
      for (auto dt : containedSharedTypes)
      {
        auto DITypeName = DIUtils::getDITypeName(dt);
        if (seenDITypeName.find(DITypeName) != seenDITypeName.end())
          continue;
        seenDITypeName.insert(DITypeName);
        if (isStructPointerTy(dt) || isStructTy(dt))
        {
          auto lowestType = DIUtils::getLowestDIType(dt);
          if (lowestType)
            sharedDITypes.insert(lowestType);
        }
      }
      // build object tree for shared struct type
    }
  }
 // collect shared type from interface functions
  for (auto func : crossDomainFuncs)
  {
    if (func->isDeclaration() || func->empty())
      continue;
    for (Argument &arg : func->args())
    {
      auto argDIType = getArgDIType(arg);
      auto containedSharedTypes = computeContainedDerivedTypes(argDIType, tree_max_height);
      for (auto dt : containedSharedTypes)
      {
        //TODO: add function pointer handle
        auto DITypeName = DIUtils::getDITypeName(dt);
        if (seenDITypeName.find(DITypeName) != seenDITypeName.end())
          continue;
        seenDITypeName.insert(DITypeName);

        if (isStructPointerTy(dt) || isStructTy(dt))
        {
          auto lowestType = DIUtils::getLowestDIType(dt);
          if (lowestType)
            sharedDITypes.insert(lowestType);
        }
      }
    }
  }
  return sharedDITypes;
}

std::set<DIType *> pdg::DIUtils::computeContainedDerivedTypes(DIType* dt, int tree_max_height)
{
  std::queue<DIType*> workQ;
  std::set<DIType *> seenTypes;
  if (!isStructPointerTy(dt) && !isStructTy(dt))
    return seenTypes;
  workQ.push(dt);
  int current_tree_height = 0;
  while (current_tree_height < tree_max_height)
  {
    current_tree_height++;
    int work_queue_size = workQ.size();
    while (work_queue_size > 0)
    {
      DIType *curDIType = workQ.front();
      workQ.pop();
      work_queue_size--;
      DIType *lowestDIType = getLowestDIType(curDIType);
      if (seenTypes.find(lowestDIType) != seenTypes.end())
        continue;
      seenTypes.insert(lowestDIType);
      auto DINodeArr = dyn_cast<DICompositeType>(lowestDIType)->getElements();
      for (unsigned i = 0; i < DINodeArr.size(); ++i)
      {
        DIType *fieldDIType = dyn_cast<DIType>(DINodeArr[i]);
        fieldDIType = stripMemberTag(fieldDIType);
        if (isStructTy(fieldDIType) || isStructPointerTy(fieldDIType))
          workQ.push(fieldDIType);
      }
    }
  }
  return seenTypes;
}

bool pdg::DIUtils::isSentinelType(DIType* parent_struct_di_type)
{
  if (parent_struct_di_type == nullptr)
    return false;
  DIType* parent_struct_lowest_di_type = DIUtils::getLowestDIType(parent_struct_di_type);
  if (!isStructTy(parent_struct_lowest_di_type))
    return false;

  auto struct_field_di_arr = dyn_cast<DICompositeType>(parent_struct_lowest_di_type)->getElements();
  for (int i = 0; i < struct_field_di_arr.size(); ++i)
  {
    DIType *field_di_type = dyn_cast<DIType>(struct_field_di_arr[i]);
    DIType *field_lowest_di_type = DIUtils::getLowestDIType(field_di_type);
    if (field_lowest_di_type == parent_struct_lowest_di_type)
      return true;
    // if (DIUtils::isStructTy(field_lowest_di_type))
    //   return isSentinelType(field_lowest_di_type);
  }
  return false;
}

bool pdg::DIUtils::isProjectableTy(DIType *dt)
{
  dt = stripMemberTag(dt);
  if (dt != nullptr)
    return (isStructTy(dt) || isUnionTy(dt));
  return false;
}