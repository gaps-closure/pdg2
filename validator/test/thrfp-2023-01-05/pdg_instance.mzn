Inst_FunCall_start = 1;
Inst_FunCall_end = 9;
Inst_Ret_start = 10;
Inst_Ret_end = 13;
Inst_Br_start = 0;
Inst_Br_end = -1;
Inst_Other_start = 14;
Inst_Other_end = 38;
Inst_start = 1;
Inst_end = 38;
VarNode_StaticGlobal_start = 0;
VarNode_StaticGlobal_end = -1;
VarNode_StaticModule_start = 0;
VarNode_StaticModule_end = -1;
VarNode_StaticFunction_start = 0;
VarNode_StaticFunction_end = -1;
VarNode_StaticOther_start = 0;
VarNode_StaticOther_end = -1;
VarNode_start = 39;
VarNode_end = -1;
FunctionEntry_start = 39;
FunctionEntry_end = 43;
Param_FormalIn_start = 0;
Param_FormalIn_end = -1;
Param_FormalOut_start = 0;
Param_FormalOut_end = -1;
Param_ActualIn_start = 0;
Param_ActualIn_end = -1;
Param_ActualOut_start = 0;
Param_ActualOut_end = -1;
Param_start = 44;
Param_end = 43;
Annotation_Var_start = 0;
Annotation_Var_end = -1;
Annotation_Global_start = 0;
Annotation_Global_end = -1;
Annotation_Other_start = 0;
Annotation_Other_end = -1;
Annotation_start = 44;
Annotation_end = -1;
PDGNode_start = 1;
PDGNode_end = 43;
ControlDep_CallInv_start = 1;
ControlDep_CallInv_end = 1;
ControlDep_CallRet_start = 2;
ControlDep_CallRet_end = 2;
ControlDep_Entry_start = 3;
ControlDep_Entry_end = 40;
ControlDep_Br_start = 0;
ControlDep_Br_end = -1;
ControlDep_Other_start = 0;
ControlDep_Other_end = -1;
ControlDep_start = 1;
ControlDep_end = 40;
DataDepEdge_DefUse_start = 41;
DataDepEdge_DefUse_end = 62;
DataDepEdge_RAW_start = 63;
DataDepEdge_RAW_end = 64;
DataDepEdge_Ret_start = 65;
DataDepEdge_Ret_end = 65;
DataDepEdge_Alias_start = 0;
DataDepEdge_Alias_end = -1;
DataDepEdge_start = 41;
DataDepEdge_end = 65;
Parameter_In_start = 0;
Parameter_In_end = -1;
Parameter_Out_start = 0;
Parameter_Out_end = -1;
Parameter_Field_start = 0;
Parameter_Field_end = -1;
Parameter_start = 66;
Parameter_end = -1;
Anno_Global_start = 0;
Anno_Global_end = -1;
Anno_Var_start = 0;
Anno_Var_end = -1;
Anno_Other_start = 0;
Anno_Other_end = -1;
Anno_start = 0;
Anno_end = -1;
PDGEdge_start = 1;
PDGEdge_end = 65;
hasFunction = [
39,40,41,41,41,42,43,43,43,39,40,41,42,39,39,39,39,39,41,41,
41,41,41,41,42,42,43,43,43,43,43,43,43,43,43,43,43,43,39,40,
41,42,43
];
hasSource = [
6,12,39,39,39,39,39,39,39,40,40,41,41,41,41,41,41,41,41,41,
41,42,42,42,42,43,43,43,43,43,43,43,43,43,43,43,43,43,43,43,
14,14,16,17,18,19,19,20,21,23,24,3,4,25,27,28,29,30,31,31,
7,37,15,22,12
];
hasDest = [
41,6,15,1,16,17,18,10,14,2,11,19,20,21,22,23,24,3,4,5,
12,25,26,6,13,27,28,29,30,31,32,33,34,35,7,36,37,8,9,38,
16,15,17,18,10,23,20,22,22,24,3,4,5,26,32,33,34,36,37,7,
36,8,16,24,6
];
hasParamIdx = array1d(Param, [
]);
userAnnotatedFunction = array1d(FunctionEntry, [
 false,false,false,false,false
]);
MaxFuncParms = 3;
