#!/usr/bin/env python3

import re, sys, string

rx = re.compile(r"""^
        \s*
        probe
        \s+
        (?P<probename>\w+)
        \(
        (?P<args>([^)]|\n|\r\n)*)
        \)\s*;
        """, re.X | re.M)

blanks = re.compile("(\s|\r|\n)+")
txt = "".join(sys.stdin.readlines())
for m in rx.finditer(txt):
    arg_str = blanks.sub(" ", m.group("args"))
    probename = re.sub("__", "_", m.group("probename"))
    print("TRACEPOINT_EVENT(\n\terlang,\n\t", probename, ",\n\tTP_ARGS(\n\t\t", sep="", end="")
    args = [] 
    fields = []
    for arg in arg_str.split(","):
        arg_tokens = re.split("(\W)", arg.strip())
        type_str = " ".join(arg_tokens[:-1]).strip()
        name_str = arg_tokens[-1]
        new_arg = type_str + ", " + name_str + "_arg"
        args.append(new_arg)
        if re.search("int", type_str):
            field = "ctf_integer(%s, %s, %s_arg)" % (type_str, name_str, name_str)
        elif re.search("char", type_str):
            field = "ctf_string(%s, %s_arg)" % (name_str, name_str)
        else:
            raise Exception("Bad type", (probename, arg_str, type_str))
        fields.append(field)

    print(",\n\t\t".join(args[:10]), sep="", end="")
    print("\n\t),\n\tTP_FIELDS(\n\t\t", sep="", end="")
    print("\n\t\t".join(fields[:10]), sep="", end="")
    print("\n\t)\n)\n\n", sep="", end="")
