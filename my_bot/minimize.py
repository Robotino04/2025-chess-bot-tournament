from __future__ import annotations
from ctypes import BigEndianStructure
import enum
from random import betavariate
from typing import Any, Iterable

import clang.cindex
import re
from pcpp.parser import LexToken, Macro
from pcpp.preprocessor import STRING_TYPES
from tqdm import tqdm
import itertools
import tempfile
import subprocess
import os
from pprint import pprint
from collections import defaultdict
from dataclasses import dataclass

from pcpp import Action, OutputDirective, Preprocessor
from io import StringIO

BUILTIN_MACROS = ["__DATE__", "__FILE__", "__PCPP__", "__TIME__"]


class QuietPreprocessor(Preprocessor):
    def __init__(self, lexer=None):
        super().__init__(lexer)
        self.passthru_includes = re.compile(".*", flags=re.DOTALL)
        self.compress = False
        self.line_directive = "//"

    def on_include_not_found(
        self, is_malformed, is_system_include, curdir, includepath
    ):
        if is_malformed:
            return super().on_include_not_found(
                is_malformed, is_system_include, curdir, includepath
            )
        raise OutputDirective(Action.IgnoreAndPassThrough)


class SelectiveExpansionProcessor(QuietPreprocessor):
    def __init__(self, selected_macros: list[str], lexer=None):
        self.selected_macros = selected_macros + BUILTIN_MACROS
        self.token_priority_feed = []
        super().__init__(lexer)

    """
    def expand_macros(self, tokens, expanding_from=[]):
        expanded = []
        for i, token in enumerate(tokens):
            if (
                token.value in self.macros
                and self.macros[token.value].name in self.selected_macros
            ):
                expanded += super().expand_macros(tokens[i:], expanding_from)

            else:
                expanded.append(token)

        return expanded
    """

    def define(self, tokens):
        if isinstance(tokens, STRING_TYPES):
            tokens = self.tokenize(tokens)

        if tokens[0].value not in self.selected_macros:
            # don't define macros
            return
        return super().define(tokens)

    def token(self):
        if len(self.token_priority_feed) != 0:
            return self.token_priority_feed.pop(0)
        return super().token()

    def on_directive_handle(self, directive, toks: list, ifpassthru, precedingtoks):
        if directive.value == "define" and toks[0].value not in self.selected_macros:

            # Find where macro body starts (after macro name and optional args)
            i = 1  # Skip "define" and macro name
            if i < len(toks) and toks[i].value == "(":
                # Skip function-like macro arg list
                paren_depth = 1
                i += 1
                while i < len(toks) and paren_depth > 0:
                    if toks[i].value == "(":
                        paren_depth += 1
                    elif toks[i].value == ")":
                        paren_depth -= 1
                    i += 1

            # Split into macro header and macro body
            space_token = LexToken()
            space_token.value = " "
            space_token.type = self.t_SPACE
            header_tokens = precedingtoks + [directive] + [space_token] + toks[:i]
            body_tokens = toks[i:]

            # Expand selected macros in the body only once
            expanded_body = self.expand_macros(body_tokens)

            newline_token = LexToken()
            newline_token.value = "\n"
            newline_token.type = self.t_NEWLINE

            self.token_priority_feed += header_tokens + expanded_body + [newline_token]

            raise OutputDirective(Action.IgnoreAndRemove)
        return super().on_directive_handle(directive, toks, ifpassthru, precedingtoks)


clang.cindex.Config.set_compatibility_check(False)
index = clang.cindex.Index.create()


def get_tokens(src: str) -> list[clang.cindex.Token | FakeToken]:
    tu = index.parse(
        "file.c",
        unsaved_files=[("file.c", src)],
        args=["-std=c23"],
        options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )

    return [
        t
        for t in tu.get_tokens(extent=tu.cursor.extent)
        if t.kind != clang.cindex.TokenKind.COMMENT
    ]


def count_tokens(src: str):
    # Parse the translation unit
    tokens = get_tokens(src)

    return len(tokens) + 1


def get_macros(src: str) -> list[str]:
    pp = QuietPreprocessor()
    pp.parse(initial_code)
    out = StringIO()
    pp.write(out)

    return sorted([m for m in pp.macros.keys() if not m in BUILTIN_MACROS])


def expand_macros(src: str, names: list[str]) -> str:
    pp = SelectiveExpansionProcessor(names)
    pp.parse(initial_code)
    out = StringIO()
    pp.write(out)
    return out.getvalue()


def powerset(iterable: Iterable):
    "powerset([1,2,3]) --> () (1,) (2,) (3,) (1,2) (1,3) (2,3) (1,2,3)"
    s = list(iterable)
    return itertools.chain.from_iterable(
        itertools.combinations(s, r) for r in range(len(s) + 1)
    )


def clang_tidy(
    source_code: str,
) -> str:
    checks = [
        "-*",
        "readability-else-after-return",
        "readability-redundant-*",
        "readability-simplify-*",
        "readability-container-size-empty",
        "readability-container-contains",
        "readability-qualified-auto",
        "readability-static-accessed-through-instance",
        "readability-convert-member-functions-to-static",
        "readability-delete-null-pointer",
        "readability-duplicate-include",
        "-readability-uppercase-literal-suffix",
    ]

    with tempfile.TemporaryDirectory() as tmpdir:
        source_path = os.path.join(tmpdir, "temp.c")

        with open(source_path, "w") as f:
            f.write(source_code)

        result = subprocess.run(
            [
                "clang-tidy",
                source_path,
                f"-checks={','.join(checks)}",
                "--fix",
                "--quiet",
                "--",
                "-std=c23",
                "-I../src/c/",
            ],
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            print("clang-tidy stderr:", result.stderr)
            print("clang-tidy stdout:", result.stdout)
            raise RuntimeError("clang-tidy failed")

        with open(source_path, "r") as f:
            updated_code = f.read()

        return updated_code


def clang_format(source_code: str) -> str:
    result = subprocess.run(
        ["clang-format", "--style=file:minimal.clang-format"],
        input=source_code,
        capture_output=True,
        text=True,
    )
    return result.stdout


def uncrustify(source_code: str) -> str:
    result = subprocess.run(
        ["uncrustify", "-lc", "-c", "uncrustify.ini"],
        input=source_code,
        capture_output=True,
        text=True,
    )
    return result.stdout


def pipeline(src: str, enabled_macros: list[str]) -> str:
    src = expand_macros(src, enabled_macros)

    src = clang_tidy(src)
    src = uncrustify(src)
    src = clang_format(src)

    return src


@dataclass
class FakeLocation:
    pass


@dataclass
class FakeToken:
    def __init__(self, spelling: str) -> None:
        self.spelling = spelling
        self.location = FakeLocation()

    spelling: str
    location: FakeLocation


@dataclass
class Subdivision:
    tokens: list[clang.cindex.Token | FakeToken]
    start: int
    end: int

    def hashable(self) -> tuple[str, ...]:
        return tuple(t.spelling for t in self.tokens)


def unique_lists_with_counts(
    subdivisions: list[Subdivision],
) -> list[tuple[Subdivision, int]]:
    print("Counting")
    counts: defaultdict[tuple[str, ...], tuple[int, int, int]] = defaultdict(
        lambda: (0, -1, -1)
    )
    unique: list[Subdivision] = []
    seen: set[tuple[str, ...]] = set()

    for subdiv in tqdm(subdivisions):
        oldcount, cstart, cend = counts[subdiv.hashable()]
        if subdiv.start > cend:
            counts[subdiv.hashable()] = (oldcount + 1, subdiv.start, subdiv.end)
        if subdiv.hashable() not in seen:
            seen.add(subdiv.hashable())
            unique.append(subdiv)

    # Convert tuple keys back to lists for the output
    print("Combining")
    result = [(t, counts[t.hashable()][0]) for t in tqdm(unique)]
    return result


def gen_subdivisions(
    tokens: list[clang.cindex.Token | FakeToken],
) -> list[tuple[Subdivision, int]]:
    print("Generating Subdivisions")
    subdivs = [
        Subdivision(tokens[start : end + 1], start, end)
        for start, _ in tqdm(list(enumerate(tokens)))
        for end in range(start, len(tokens))
    ]

    print("Sorting")
    subdivs = sorted(
        subdivs,
        key=lambda subdiv: subdiv.start,
    )
    return unique_lists_with_counts(subdivs)


def line_fallback(
    fb: int,
    loc: clang.cindex.SourceLocation | FakeLocation,
):
    if isinstance(loc, FakeLocation):
        return fb

    return loc.line


def reconstruct_source(tokens: list[clang.cindex.Token | FakeToken]) -> str:
    out = ""
    line = line_fallback(-1, tokens[0].location)
    for token in tokens:
        if line_fallback(line, token.location) != line:
            line = line_fallback(line, token.location)
            out += "\n"
        out += " " + token.spelling
    return out


def generate_one_macro(src: str, name: str):
    tokens = get_tokens(src)
    print(tokens)

    adjusted_subdivs = [
        (subdiv, frequency * (1 - len(subdiv.tokens)) + 2 + 1 + len(subdiv.tokens))
        for subdiv, frequency in gen_subdivisions(tokens)
    ]

    adjusted_subdivs.sort(key=lambda x: x[1], reverse=False)

    pprint(adjusted_subdivs[0])
    best_subdiv = adjusted_subdivs[0][0]

    new_macro = FakeToken(name)
    definition = (
        [
            FakeToken("#"),
            FakeToken("define"),
            new_macro,
            FakeToken("\\"),
        ]
        + best_subdiv.tokens
        + [
            FakeToken("\n"),
        ]
    )

    i = 0
    while i < len(tokens):
        found = True
        for j, _ in enumerate(best_subdiv.tokens):
            if (i + j) >= len(tokens):
                found = False
                break

            if tokens[i + j].spelling != best_subdiv.tokens[j].spelling:
                found = False
                break

        if found:
            tokens[i : i + len(best_subdiv.tokens)] = [new_macro]
        i += 1

    tokens = definition + tokens

    pprint(tokens)

    return reconstruct_source(tokens)


# initial_code = """
#     # define x \
#     int a = 2 + 2 + 2 + 2 ;
#
#
#     # define test
#     x x x x
#    """
# print("Initial tokens:", count_tokens(initial_code))
# print(generate_one_macro(initial_code, "y"))

initial_code = open("example_bot.c", "r").read()
print("Initial tokens:", count_tokens(initial_code))


all_macros = get_macros(initial_code)
print("Optimizing macros:", all_macros)
sorted_macros = sorted(
    [
        (
            enabled_macros,
            count_tokens(pipeline(initial_code, list(enabled_macros))),
        )
        for enabled_macros in tqdm(list(powerset(all_macros)))
    ],
    key=lambda x: x[1],
)
print("Best Macros:", sorted_macros[:10])

expanded_code = pipeline(initial_code, list(sorted_macros[0][0]))
print("Optimized tokens:", count_tokens(expanded_code))

macroed = generate_one_macro(expanded_code, "new_macro")
print("Macroed tokens:", count_tokens(macroed))

open("example_bot_minimized.c", "w").write(macroed)
