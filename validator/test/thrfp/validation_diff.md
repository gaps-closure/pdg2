# Accounts report

## A: IRGlobal.Internal.Function B: PDGNode.VarNode.StaticFunction
- `@thread_helper.x`

## A: IRParameter B: PDGNode.Param.FormalIn.Root
- `@worldfun::%0`
- `@main::%1`
- `@main::%0`
- `@thread1_fun::%0`

## A: IRGlobal.Internal.Module B: PDGNode.VarNode.StaticModule
- `@globpm`

## A: IRDefUse B: PDGEdge.DataDepEdge.DefUse
- `@globpm --> @main::8`
- `@thread_helper.x --> @thread_helper::7`
- `@globpm --> @thread_helper::2`

## A: IRAlias B: PDGEdge.DataDepEdge.Alias
- `@hellofun --> @thread_helper::8`
- `@thread_helper --> @hellofun::0`
- `@thread_helper --> @thread_helper::8`
- `@hellofun --> @hellofun::0`
- `@worldfun --> @worldfun::0`
- `@hellofun --> @thread_helper::2`
- `@thread_helper --> @globpm`
- `@worldfun --> @thread_helper::0`
- `@worldfun --> @globpm`
- `@worldfun --> @thread_helper.x`
- `@worldfun --> @thread_helper::5`
- `@worldfun --> @worldfun::2`
- `@worldfun --> @hellofun::0`
- `@hellofun --> @thread_helper::5`
- `@thread_helper --> @thread_helper::2`
- `@thread_helper --> @thread_helper::0`
- `@hellofun --> @globpm`
- `@thread_helper --> @hellofun`
- `@thread_helper --> @worldfun::2`
- `@hellofun --> @thread_helper::0`
- `@worldfun --> @thread_helper::8`
- `@thread_helper --> @thread_helper::5`
- `@worldfun --> @hellofun`
- `@worldfun --> @worldfun::4`
- `@thread_helper --> @worldfun`
- `@worldfun --> @thread_helper::2`
- `@hellofun --> @worldfun::2`
- `@hellofun --> @worldfun`
