# Accounts report

## A: IRGlobal.Internal.Function B: PDGNode.VarNode.StaticFunction
- `@thread_helper.x`

## A: IRGlobal.Internal.Module B: PDGNode.VarNode.StaticModule
- `@globpm`

## A: IRAlias B: PDGEdge.DataDepEdge.Alias
- `@worldfun --> @thread_helper.x`
- `@thread_helper --> @thread_helper::0`
- `@worldfun --> @globpm`
- `@hellofun --> @worldfun`
- `@worldfun --> @hellofun`
- `@thread_helper --> @globpm`
- `@worldfun --> @hellofun::0`
- `@hellofun --> @thread_helper::2`
- `@worldfun --> @thread_helper::5`
- `@hellofun --> @worldfun::2`
- `@thread_helper --> @thread_helper::8`
- `@worldfun --> @worldfun::2`
- `@thread_helper --> @hellofun`
- `@worldfun --> @worldfun::4`
- `@hellofun --> @thread_helper::5`
- `@thread_helper --> @worldfun::2`
- `@thread_helper --> @hellofun::0`
- `@hellofun --> @thread_helper::0`
- `@hellofun --> @hellofun::0`
- `@worldfun --> @thread_helper::2`
- `@hellofun --> @globpm`
- `@thread_helper --> @thread_helper::5`
- `@thread_helper --> @worldfun`
- `@worldfun --> @worldfun::0`
- `@worldfun --> @thread_helper::8`
- `@hellofun --> @thread_helper::8`
- `@thread_helper --> @thread_helper::2`
- `@worldfun --> @thread_helper::0`

## A: IRParameter B: PDGNode.Param.FormalIn.Root
- `@thread1_fun::%0`
- `@main::%1`
- `@main::%0`
- `@worldfun::%0`
