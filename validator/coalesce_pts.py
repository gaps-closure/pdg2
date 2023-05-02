import re
import sys
from typing import Dict, List, Set, Tuple, Callable, TypeVar

A = TypeVar('A')
B = TypeVar('B')
Parsed = Tuple[str, A]
Parser = Callable[[str], Parsed[A]]  

def tag(tag: str) -> Parser[str]:
    def parse(input: str) -> Parsed[str]:
        if input.startswith(tag):
            return input[len(tag):], tag 
        else:
            raise ValueError(f"tag {tag} not matched on {input[:30]}")
            
    return parse

def digit(input: str) -> Parsed[int]:
    return input[1:], int(input[0])

def many(parser: Parser[A]) -> Parser[List[A]]:
    def parse(input: str) -> Parsed[List[A]]:
        xs = []
        try:
            while True:
                input, x = parser(input)
                xs.append(x)
        except:
            return input, xs
    return parse


def some(parser: Parser[A]) -> Parser[List[A]]:
    def parse(input: str) -> Parsed[List[A]]:
        input, x = parser(input)
        input, xs = many(parser)(input)
        return input, [x, *xs]
    return parse

def whitespace(input: str) -> Parsed[str]:
    res = re.match(r'\s', input)
    if res is None:
        raise ValueError(f"could not match whitespace on {input[:30]}")
    return input[1:], input[0]


def alt(parser_a: Parser[A], parser_b: Parser[A]) -> Parser[A]:
    def parse(input: str) -> Parsed[A]:
        try:
            return parser_a(input)
        except:
            return parser_b(input)
    return parse

# alpha numeric dot underscore
def andu(input: str) -> Parsed[str]:
    res = re.match(r'\w|\.|_', input)
    if res is None:
        raise ValueError(f"could not match andu on {input[:30]}")
    return input[1:], input[0]


def sepby(main: Parser[A], sep: Parser[str]) -> Parser[List[A]]:
    def parse_rest(input: str) -> Parsed[A]:
        input, _ = sep(input)
        return main(input) 
    def parse(input: str) -> Parsed[List[A]]:
        xs = []
        input, x = main(input) 
        xs.append(x)
        input, ys = many(parse_rest)(input)
        xs.extend(ys)
        return input, xs
    return parse 

def parse_name(input: str) -> Parsed[str]:
    def parse_global_name(input: str) -> Parsed[str]:
        input, _ = tag("@")(input)
        input, name = some(andu)(input)
        return input, "@" + "".join(name)
    def parse_local_name(input: str) -> Parsed[str]:
        input, gname = some(andu)(input)
        input, _ = tag("::%")(input)
        input, lname = some(andu)(input)
        return input, "".join(gname) + "::%" + "".join(lname)
    return alt(parse_global_name, parse_local_name)(input)

def map(f: Callable[[A], B], parser: Parser[A]) -> Parser[B]:
    def parse(input: str) -> Parsed[B]:
        input, x = parser(input) 
        return input, f(x)
    return parse

def parse_set(input: str) -> Parsed[Set[int]]:
    parse_int = map(lambda xs: int("".join([ str(x) for x in xs ])), some(digit)) # type: ignore
    return map(lambda x: set(x), sepby(parse_int, map(lambda x: "".join(x), some(whitespace))))(input) # type: ignore

def parse_pts(input: str) -> Parsed[Tuple[str, Set[int]]]:
    input, _ = tag('NodeID')(input)
    input, _ = some(whitespace)(input)
    input, _ = some(digit)(input)
    input, _ = some(whitespace)(input)
    input, _ = tag('(val:')(input)
    input, _ = many(whitespace)(input)
    input, name = parse_name(input)
    input, _ = tag(')')(input)
    input, _ = many(whitespace)(input)
    input, _ = tag("PointsTo:")(input)
    input, _ = many(whitespace)(input)
    input, _ = tag("{")(input)
    input, _ = many(whitespace)(input)
    input, set = parse_set(input)
    input, _ = many(whitespace)(input)
    input, _ = tag("}")(input)
    return input, (name, set) 


def main() -> None:
    input = sys.argv[1]
    forward_map: Dict[str, Set[int]] = dict() 
    backward_map: Dict[int, Set[str]] = dict() 
    with open(input) as input_file:
        for line in input_file:
            try:
                input, (name, node_set) = parse_pts(line)
                if name in forward_map:
                    forward_map[name] = forward_map[name].union(node_set)
                else:
                    forward_map[name] = node_set 
                for n in node_set:
                    if n in backward_map:
                        backward_map[n].add(name)
                    else:
                        backward_map[n] = {name}
            except ValueError as e:
                pass

    should_continue = True
     
    # forward map @a -> { 1, 2, 3 }
    # backward map 1 -> { @a @b } 
    while should_continue:
        delete_list: List[int] = [] 
        for (name, nodes) in forward_map.items():
            if len(nodes) > 1:
                new_set: Set[str] = set()
                primary_node = next(iter(nodes))
                for node in nodes: 
                    new_set = new_set.union(backward_map[node])  
                delete_list.extend((node for node in nodes if node != primary_node))
                forward_map[name] = {primary_node}
                backward_map[primary_node] = new_set 

        should_continue = len(delete_list) > 0

        for node in delete_list:
            if node in backward_map:
                del backward_map[node]


    for s in backward_map.values():
        if len(s) > 1:
            print(s)

        # if len(forward_map[node]) > 1:

if __name__ == "__main__":
    main()