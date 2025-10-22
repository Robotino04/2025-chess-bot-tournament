use std::{
    collections::HashMap,
    hash::{Hash, Hasher},
    ops::Range,
};

use clang::{
    Clang, Index, TranslationUnit, Unsaved,
    token::{Token, TokenKind},
};
use itertools::Itertools;
use rustc_hash::{FxBuildHasher, FxHasher};

const MACRO_OVERHEAD: i32 = 2 + 1;

#[derive(Debug, Clone, Copy, Eq)]
struct PrefetchedToken<'source> {
    spelling: &'source [u8],
    hash: u64,
}

impl<'source> PartialEq for PrefetchedToken<'source> {
    fn eq(&self, other: &Self) -> bool {
        self.hash == other.hash && self.spelling == other.spelling
    }
}

impl<'source> PrefetchedToken<'source> {
    fn new(spelling: &'source [u8]) -> Self {
        let mut hasher = FxHasher::with_seed(0);
        spelling.hash(&mut hasher);
        let hash = hasher.finish();
        Self { spelling, hash }
    }
}

impl<'source> Hash for PrefetchedToken<'source> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.hash.hash(state);
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
        self.tokens.hash(state);
    }
}

impl PartialEq for Subdivision<'_, '_> {
    fn eq(&self, other: &Self) -> bool {
        self.tokens == other.tokens
    }
}
impl Eq for Subdivision<'_, '_> {}

/// requires subdivisions to be sorted by .start
fn unique_lists_with_counts<'source, 'parent>(
    subdivisions: Vec<Subdivision<'source, 'parent>>,
) -> Vec<(Subdivision<'source, 'parent>, i32)> {
    let mut counts = HashMap::with_capacity_and_hasher(subdivisions.len(), FxBuildHasher);

    for subdiv in subdivisions.into_iter() {
        let start = subdiv.start;
        let end = subdiv.end;
        let (oldcount, oldend) = counts.entry(subdiv).or_insert((0, -1));
        if start > *oldend {
            *oldcount += 1;
            *oldend = end;
        }
    }

    counts
        .into_iter()
        .map(|(subdiv, (score, _end))| (subdiv, score))
        .collect()
}

fn get_spelling_range(token: &Token) -> Range<usize> {
    let range = token.get_range();
    let start = range.get_start().get_file_location().offset as usize;
    let end = range.get_end().get_file_location().offset as usize;

    start..end
}

/// generated a list of all subdivisions sorted by .start
fn gen_subdivisions<'source, 'parent>(
    tokens: &'parent [PrefetchedToken<'source>],
) -> Vec<Subdivision<'source, 'parent>> {
    (0..tokens.len())
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
            subdiv
                .tokens
                .iter()
                .all(|token| !matches!(token.spelling, b"#" | b"define" | b"include"))
        })
        .collect()
}

fn prefetch_tokens<'tu, 'source, 'parent>(
    tokens: &'parent [Token<'tu>],
    source: &'source [u8],
) -> Vec<PrefetchedToken<'source>> {
    tokens
        .iter()
        .map(|token| {
            let spelling = &source[get_spelling_range(token)];

            PrefetchedToken::new(spelling)
        })
        .collect_vec()
}

fn generate_one_macro<'source>(
    tokens: &[PrefetchedToken<'source>],
    name: &'source str,
) -> Vec<PrefetchedToken<'source>> {
    println!("Generating Subdivisions");
    let subdivs = gen_subdivisions(tokens);
    let subdivs = unique_lists_with_counts(subdivs);
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
        .sorted_unstable_by_key(|(_subdiv, score)| *score)
        .collect_vec();

    let (best_subdiv, score) = adjusted_subdivs.into_iter().next().unwrap();
    if score > 0 {
        return tokens.to_vec();
    }

    let mut tokens = tokens.to_vec();

    let new_macro = PrefetchedToken::new(name.as_bytes());

    let mut i = 0;
    while i < tokens.len() {
        let range = i..((i + best_subdiv.tokens.len()).min(tokens.len()));

        let cmp_subdiv = Subdivision {
            tokens: &tokens[range.clone()],
            start: best_subdiv.start,
            end: best_subdiv.end,
        };

        if best_subdiv == cmp_subdiv {
            tokens.splice(range, vec![new_macro]);
        }
        i += 1;
    }

    let mut definition = vec![
        PrefetchedToken::new(b"#"),
        PrefetchedToken::new(b"define"),
        new_macro,
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
        if token.spelling == b"#" || include_count == INCLUDE_TOKEN_COUNT {
            out.push('\n');
            include_count = 0;
        }

        if token.spelling == b"#"
            || token.spelling == b"include"
            || (token.spelling.starts_with(b"\"") && token.spelling.ends_with(b"\""))
        {
            include_count += 1;
        } else {
            include_count = 0;
        }

        out.push_str(core::str::from_utf8(token.spelling).unwrap());
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
    let mut tokens = prefetch_tokens(&tokens, source.as_bytes());

    let macro_names = (0..100).map(|i| format!("rust_macro{i}")).collect_vec();
    let macro_names = macro_names.iter().map(|i| i.as_str()).collect_vec();

    let mut i = 0;
    loop {
        println!("Current Token Count: {}", tokens.len());
        if tokens.len() >= prev_tokens {
            println!("Tokens increased from {prev_tokens}. Exiting");
            break;
        }
        prev_tokens = tokens.len();

        tokens = generate_one_macro(&tokens, macro_names[i]);

        i += 1;
    }
    let source = reconstruct_source(&tokens);

    std::fs::write("../example_bot_minimized.c", &source).unwrap();
}
