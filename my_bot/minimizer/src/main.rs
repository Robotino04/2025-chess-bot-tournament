use std::{collections::HashMap, hash::Hash, ops::Range};

use clang::{
    Clang, Index, TranslationUnit, Unsaved,
    token::{Token, TokenKind},
};
use itertools::Itertools;

const MACRO_OVERHEAD: i32 = 2 + 1;

#[derive(Debug, Clone, Copy)]
struct PrefetchedToken<'a, 'b> {
    token: Option<&'b Token<'a>>,
    spelling: &'b str,
}

impl Hash for PrefetchedToken<'_, '_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.spelling.hash(state);
    }
}

#[derive(Debug, Clone)]
struct Subdivision<'a, 'b> {
    tokens: &'b [PrefetchedToken<'a, 'b>],
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

fn unique_lists_with_counts<'a, 'b, 'c>(
    subdivisions: &'c [Subdivision<'a, 'b>],
) -> Vec<(&'c Subdivision<'a, 'b>, i32)> {
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

fn gen_subdivisions<'a, 'b>(
    tokens: &'b [PrefetchedToken<'a, 'b>],
    source: &'b str,
) -> Vec<Subdivision<'a, 'b>> {
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

            let start = subdiv
                .tokens
                .first()
                .unwrap()
                .token
                .unwrap()
                .get_range()
                .get_start()
                .get_file_location()
                .offset as usize;
            let end = subdiv
                .tokens
                .last()
                .unwrap()
                .token
                .unwrap()
                .get_range()
                .get_end()
                .get_file_location()
                .offset as usize;

            let spelling = &source[start..end];

            !(spelling.contains('#') || spelling.contains("define") || spelling.contains("include"))
        })
        .sorted_by_key(|subdiv| subdiv.start)
        .collect()
}

fn prefetch_tokens<'a, 'b>(
    tokens: &'b [Token<'a>],
    source: &'b str,
) -> Vec<PrefetchedToken<'a, 'b>> {
    tokens
        .iter()
        .map(|token| PrefetchedToken {
            token: Some(token),
            spelling: &source[get_spelling_range(token)],
        })
        .collect_vec()
}

fn generate_one_macro<'a, 'b>(
    tokens: &'b [PrefetchedToken<'a, 'b>],
    name: &'b str,
    source: &'a str,
) -> (String, Vec<PrefetchedToken<'a, 'b>>) {
    println!("Generating Subdivisions");
    let subdivs = gen_subdivisions(tokens, source);
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
        return ("".to_string(), tokens.to_vec());
    }
    /*
    println!(
        "Top subdiv: {score} {:?}",
        reconstruct_source(best_subdiv.tokens)
    );
    */

    let mut tokens = tokens.to_vec();

    let new_macro = PrefetchedToken {
        token: None,
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
            token: None,
            spelling: "#",
        },
        PrefetchedToken {
            token: None,
            spelling: "define",
        },
        new_macro,
        PrefetchedToken {
            token: None,
            spelling: "\\",
        },
    ];
    definition.append(&mut best_subdiv.tokens.to_vec());

    let definition_str = reconstruct_source(definition.as_slice()).replace("\n", " ") + "\n";

    (definition_str, tokens)
}

fn get_tu<'a>(index: &'a Index<'a>, src: &str) -> TranslationUnit<'a> {
    index
        .parser("test.c")
        .unsaved(&[Unsaved::new("test.c", src)])
        .parse()
        .unwrap()
}

fn get_tokens<'a>(tu: &'a TranslationUnit<'a>) -> Vec<Token<'a>> {
    tu.get_entity()
        .get_range()
        .unwrap()
        .tokenize()
        .into_iter()
        .filter(|token| token.get_kind() != TokenKind::Comment)
        .collect_vec()
}

fn reconstruct_source(tokens: &[PrefetchedToken]) -> String {
    let mut out = vec!["".to_string()];
    let mut line = match tokens[0].token {
        None => -1,
        Some(t) => t.get_location().get_spelling_location().line as i32,
    };
    let mut col = match tokens[0].token {
        None => -1,
        Some(t) => t.get_location().get_spelling_location().column as i32,
    };

    for token in tokens {
        let mut last_line = out.last_mut().unwrap();

        match token.token {
            None => {
                last_line.push(' ');
                last_line.push_str(token.spelling);
            }
            Some(t) => {
                let tstart = t.get_location().get_spelling_location();
                let tend = t.get_range().get_end().get_spelling_location();
                if line != tstart.line as i32 {
                    line = tstart.line as i32;
                    col = 0;
                    out.push("".to_string());
                    last_line = out.last_mut().unwrap();
                }
                if col != tstart.column as i32 {
                    col = tend.column as i32;
                    last_line.push(' ');
                }
                last_line.push_str(token.spelling);
            }
        }
    }

    out.into_iter()
        .map(|line| line.trim().trim_end_matches("\\").trim_end().to_string())
        .map(|line| {
            if line.starts_with("#") {
                line.replace("\n", "")
            } else {
                line.to_string()
            }
        })
        .map(|line| line.trim().to_string())
        .join("\n")
}

fn main() {
    let clang = Clang::new().unwrap();

    let index = Index::new(&clang, false, false);

    let mut source = std::fs::read_to_string("../example_bot_clean.c").unwrap();
    let mut prev_source = "".to_string();

    let mut prev_tokens = 99999999;

    loop {
        let tu = get_tu(&index, &source);

        let tokens = get_tokens(&tu);
        println!("Current Token Count: {}", tokens.len());
        if tokens.len() >= prev_tokens {
            println!("Tokens increased from {prev_tokens}. Exiting");
            break;
        }
        prev_tokens = tokens.len();

        let mut i = 0;
        while source.contains(&format!("rust_macro{i}")) {
            i += 1;
        }

        let tokens = prefetch_tokens(&tokens, &source);

        let macro_name = format!("rust_macro{i}");
        let (macro_str, macroed_tokens) = generate_one_macro(&tokens, &macro_name, &source);

        let new_source = macro_str + &reconstruct_source(&macroed_tokens);
        prev_source = source;
        source = new_source;

        std::fs::write("../example_bot_minimized.c", &source).unwrap();
    }

    std::fs::write("../example_bot_minimized.c", &prev_source).unwrap();
}
