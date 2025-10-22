use std::{collections::HashMap, hash::Hash, ops::Range};

use clang::{
    Clang, Index, TranslationUnit, Unsaved,
    token::{Token, TokenKind},
};
use itertools::Itertools;

const MACRO_OVERHEAD: i32 = 2 + 1;

#[derive(Debug, Clone, Copy)]
struct PrefetchedToken<'source> {
    spelling: &'source str,
}

impl Hash for PrefetchedToken<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.spelling.hash(state);
    }
}

#[derive(Debug, Clone)]
struct Subdivision<'source, 'parent> {
    tokens: &'parent [PrefetchedToken<'source>],
    start: i32,
    end: i32,
}

impl Hash for Subdivision<'_, '_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        for token in self.tokens {
            token.hash(state);
        }
    }
}

impl PartialEq for Subdivision<'_, '_> {
    fn eq(&self, other: &Self) -> bool {
        self.tokens.len() == other.tokens.len()
            && self
                .tokens
                .iter()
                .zip(other.tokens.iter())
                .all(|(t1, t2)| t1.spelling == t2.spelling)
    }
}
impl Eq for Subdivision<'_, '_> {}

fn unique_lists_with_counts<'source, 'parent, 'div_parent>(
    subdivisions: &'parent [Subdivision<'source, 'div_parent>],
) -> Vec<(&'parent Subdivision<'source, 'div_parent>, i32)> {
    let mut counts =
        HashMap::with_capacity_and_hasher(subdivisions.len(), ahash::RandomState::new());

    for subdiv in subdivisions.iter()
    /*.tqdm()*/
    {
        let (oldcount, oldstart, oldend) = counts.entry(subdiv).or_insert((0, -1, -1));
        if subdiv.start > *oldend {
            *oldcount += 1;
            *oldstart = subdiv.start;
            *oldend = subdiv.end;
        }
    }

    counts
        .into_iter()
        .map(|(subdiv, (score, _start, _end))| (subdiv, score))
        //.tqdm()
        .collect()
}

fn get_spelling_range(token: &Token) -> Range<usize> {
    let range = token.get_range();
    let start = range.get_start().get_file_location().offset as usize;
    let end = range.get_end().get_file_location().offset as usize;

    start..end
}

fn gen_subdivisions<'source, 'parent>(
    tokens: &'parent [PrefetchedToken<'source>],
) -> Vec<Subdivision<'source, 'parent>> {
    (0..tokens.len())
        .collect_vec()
        .into_iter()
        //.tqdm()
        .flat_map(|start| {
            (start + 2..tokens.len())
                .map(|end| (start, end))
                .collect_vec()
        })
        .map(|(start, end)| Subdivision {
            tokens: &tokens[start..=end],
            start: start as i32,
            end: end as i32,
        })
        .filter(|subdiv| {
            if subdiv.tokens.len() < 2 {
                return false;
            }

            !subdiv.tokens.iter().any(|token| {
                token.spelling == "#" || token.spelling == "define" || token.spelling == "include"
            })
        })
        .sorted_by_key(|subdiv| subdiv.start)
        .collect()
}

fn prefetch_tokens<'tu, 'source, 'parent>(
    tokens: &'parent [Token<'tu>],
    source: &'source str,
) -> Vec<PrefetchedToken<'source>> {
    tokens
        .iter()
        .map(|token| PrefetchedToken {
            spelling: &source[get_spelling_range(token)],
        })
        .collect_vec()
}

fn generate_one_macro<'source>(
    tokens: &[PrefetchedToken<'source>],
    name: &'source str,
) -> Vec<PrefetchedToken<'source>> {
    println!("Generating Subdivisions");
    let subdivs = gen_subdivisions(tokens);
    //println!("Counting");
    let subdivs = unique_lists_with_counts(&subdivs);
    //println!("Sorting and Scoring");
    let adjusted_subdivs = subdivs
        .into_iter()
        .map(|(subdiv, frequency)| {
            (
                frequency * (1 - subdiv.tokens.len() as i32)
                    + MACRO_OVERHEAD
                    + subdiv.tokens.len() as i32,
                subdiv,
            )
        })
        .map(|(a, b)| (b, a))
        //.tqdm()
        .sorted_by_key(|(_subdiv, score)| *score)
        .collect_vec();

    let (best_subdiv, score) = adjusted_subdivs[0];
    if score > 0 {
        return tokens.to_vec();
    }
    /*
    println!(
        "Top subdiv: {score} {:?}",
        reconstruct_source(best_subdiv.tokens)
    );
    */

    let mut tokens = tokens.to_vec();

    let new_macro = PrefetchedToken {
        spelling: name,
    };

    let mut i = 0;
    while i < tokens.len() {
        let range = i..((i + best_subdiv.tokens.len()).min(tokens.len()));

        let cmp_subdiv = Subdivision {
            tokens: &tokens[range.clone()],
            start: best_subdiv.start,
            end: best_subdiv.end,
        };

        if *best_subdiv == cmp_subdiv {
            tokens.splice(range, vec![new_macro]);
        }
        i += 1;
    }

    let mut definition = vec![
        PrefetchedToken {
            spelling: "#",
        },
        PrefetchedToken {
            spelling: "define",
        },
        new_macro,
        PrefetchedToken {
            spelling: " ",
        },
    ];
    definition.extend(best_subdiv.tokens);

    definition.extend(tokens);

    definition
}

fn get_tu<'index>(index: &'index Index<'index>, src: &str) -> TranslationUnit<'index> {
    index
        .parser("test.c")
        .unsaved(&[Unsaved::new("test.c", src)])
        .parse()
        .unwrap()
}

fn get_tokens<'tu>(tu: &'tu TranslationUnit<'tu>) -> Vec<Token<'tu>> {
    tu.get_entity()
        .get_range()
        .unwrap()
        .tokenize()
        .into_iter()
        .filter(|token| token.get_kind() != TokenKind::Comment)
        .collect_vec()
}

fn reconstruct_source(tokens: &[PrefetchedToken]) -> String {
    let mut out = "".to_string();

    let mut include_count = 0;

    const INCLUDE_TOKEN_COUNT: u32 = 3; // # + include + "str"

    for token in tokens {
        out.push(' ');
        if token.spelling == "#" || include_count == INCLUDE_TOKEN_COUNT {
            out.push('\n');
            include_count = 0;
        }

        if token.spelling == "#"
            || token.spelling == "include"
            || (token.spelling.starts_with("\"") && token.spelling.ends_with("\""))
        {
            include_count += 1;
        } else {
            include_count = 0;
        }

        out.push_str(token.spelling);
    }

    out
}

fn main() {
    let clang = Clang::new().unwrap();

    let index = Index::new(&clang, false, false);

    let source = std::fs::read_to_string("../example_bot_clean.c").unwrap();

    let mut prev_tokens = 99999999;
    let tu = get_tu(&index, &source);

    let tokens = get_tokens(&tu);
    let mut tokens = prefetch_tokens(&tokens, &source);

    let macro_names = (0..100).map(|i| format!("rust_macro{i}")).collect_vec();

    let mut i = 0;
    loop {
        println!("Current Token Count: {}", tokens.len());
        if tokens.len() >= prev_tokens {
            println!("Tokens increased from {prev_tokens}. Exiting");
            break;
        }
        prev_tokens = tokens.len();

        tokens = generate_one_macro(&tokens, &macro_names[i]);

        i += 1;
    }
    let source = reconstruct_source(&tokens);

    std::fs::write("../example_bot_minimized.c", &source).unwrap();
}
