# PDG Validator

This directory will contain tools and tests to systematically validate that the
program dependency information -- contained in the LLVM IR, CLE-JSON, and
annotated source code -- relevant to CLOSURE program analysis are correctly
extracted by the libpdg opt pass and exported to the MZN model.

The objective is to account for and reconcile all instructions in the LLVM IR,
with the PDG nodes/edges, and other information exported to MZN for input to
the constraint solver. 

## Known Issues/Incompleteness of Phase 2 PDG builder and MZN exporter

1. Phase 2 PDG had a bug which failed to process callsites with variadic functions or struct literal functions, and also ignored all subsequent callsites in that function

2. Calls through a function pointer are ignored by the Phase 2 PDG resulting in an incomplete PDG (significant issue for programs that use threads, callbacks, etc.)

3. Phase 2 PDG builds parameter trees to a depth of 5 leading to incomplete PDG in some cases

```
Issue description:

I looked through the pdg code and found that the parameter trees are depth
limited. In Tree.hh you can see that the default value is 5. So the number of
formal in nodes will depend on this depth parameter.

We do have one constraint which uses parameter edges which include parameter
field edges:

constraint :: "XDCParmAllowed"
     forall (e in Parameter) (xdedge[e] -> allowOrRedact(cdfForRemoteLevel[esTaint[e], hasLabelLevel[edTaint[e]]]));

I'm worried that changing the depth limit might change the result of the
analysis. We should check/argue that a parameter field edge can't be a cross
domain edge, and argue that the parameter_field edges aren't relevant to our
analysis. Then, it might be nice to set this max_depth_tree to 0 so we cut down
on the size of the program graph. If parameter_fields are relevant to our
analysis, then we need to address this depth limit some other way.

Discussion:

Inter-enclave: currently CLOSURE only supports cross-domain functions with
fixed number of arguments that are scalars or fixed-size arrays of primitives,
so parameter tree depth of 5 will not be exceeded.

Intra-enclave: this could be an issue in label coercion, need to think about
whether a level-6 parameter tree node could cause a label violation or is it
captured by constraints on other data dependencies. In any case, what you point
out is an issue with the PDG missing edges, and not an issue with the
constraint model itself.

So at a minimum the PDG builder should warn that the parameter tree is
shallower than the maximum depth of a function that is actually called in the
program and take the depth as an argument; the developer could choose to
re-build the PDG to a greater depth. This of course, pre-supposes that the call
graph is complete (we fixed some bugs in this regard, but still do not handle
function pointers).

```

