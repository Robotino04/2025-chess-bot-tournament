from __future__ import annotations
from ctypes import BigEndianStructure
import enum
from random import betavariate
import sys
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

__original_print = print


def print(*args, **kwargs):
    __original_print(*args, **kwargs, file=sys.stderr)


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
    src = src.replace("\\\n", "")

    return src


initial_code = sys.stdin.read()
token_count = count_tokens(initial_code)
print("Initial tokens:", token_count)


token_count += 1

while token_count != count_tokens(initial_code):
    token_count = count_tokens(initial_code)
    all_macros = get_macros(initial_code)
    print("Optimizing macros:", all_macros)
    sorted_macros = sorted(
        [
            (
                enabled_macros,
                count_tokens(pipeline(initial_code, list(enabled_macros))),
            )
            for enabled_macros in tqdm(list(powerset(all_macros)), file=sys.stderr)
        ],
        key=lambda x: x[1],
    )
    print("Enabling these macros:", sorted_macros[0])

    initial_code = pipeline(initial_code, list(sorted_macros[0][0]))
    print("Current tokens:", count_tokens(initial_code))


__original_print(initial_code)
