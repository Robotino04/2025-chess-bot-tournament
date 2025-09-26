from __future__ import annotations
from typing import Iterable

import clang.cindex
import re
from pcpp.parser import LexToken, Macro
from pcpp.preprocessor import STRING_TYPES
from tqdm import tqdm
import itertools

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


def count_tokens(src: str):
    # Parse the translation unit
    tu = index.parse(
        "file.c",
        unsaved_files=[("file.c", src)],
        args=["-std=c23"],
        options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )

    tokens = [
        t
        for t in tu.get_tokens(extent=tu.cursor.extent)
        if t.kind != clang.cindex.TokenKind.COMMENT
    ]

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


initial_code = open("example_bot.c", "r").read()
print("Initial tokens:", count_tokens(initial_code))

all_macros = get_macros(initial_code)
print("Optimizing macros:", all_macros)
sorted_macros = sorted(
    [
        (
            enabled_macros,
            count_tokens(expand_macros(initial_code, list(enabled_macros))),
        )
        for enabled_macros in tqdm(list(powerset(all_macros)))
    ],
    key=lambda x: x[1],
)
print("Best Macros:", sorted_macros[:10])

expanded_code = expand_macros(initial_code, list(sorted_macros[0][0]))
print("Optimized tokens:", count_tokens(expanded_code))

open("example_bot_minimized.c", "w").write(expanded_code)
