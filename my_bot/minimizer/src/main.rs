use std::{
    cell::RefCell,
    collections::{HashMap, HashSet},
    hash::Hash,
};

use clang::{
    Clang, Index, TranslationUnit, Unsaved,
    token::{Token, TokenKind},
};
use itertools::{Either, Itertools};
use tqdm::Iter;

#[derive(Debug, Clone)]
struct Subdivision<'a> {
    tokens: Vec<Token<'a>>,
    start: i32,
    end: i32,
    hashed_tokens: RefCell<Option<String>>,
}

impl Subdivision<'_> {
    fn hashable_tokens(&self) -> String {
        if let Some(hashed_tokens) = self.hashed_tokens.borrow().clone() {
            return hashed_tokens;
        }
        *self.hashed_tokens.borrow_mut() =
            Some(self.tokens.iter().map(|t| t.get_spelling()).join(" "));

        self.hashed_tokens.borrow().as_ref().cloned().unwrap()
    }
}

impl Hash for Subdivision<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.hashable_tokens().hash(state);
        //self.start.hash(state);
        //self.end.hash(state);
    }
}

impl PartialEq for Subdivision<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.hashable_tokens() == other.hashable_tokens()
    }
}
impl Eq for Subdivision<'_> {}

fn unique_lists_with_counts(subdivisions: Vec<Subdivision>) -> Vec<(Subdivision, i32)> {
    println!("Counting");
    let mut counts = HashMap::with_capacity(subdivisions.len());
    let mut seen = HashSet::with_capacity(subdivisions.len());

    for subdiv in subdivisions.into_iter().tqdm() {
        let (oldcount, oldstart, oldend) = counts.entry(subdiv.clone()).or_insert((0, -1, -1));
        if subdiv.start > *oldend {
            *oldcount += 1;
            *oldstart = subdiv.start;
            *oldend = subdiv.end;
        }
        seen.insert(subdiv);
    }

    println!("Combining");
    seen.into_iter()
        .map(|subdiv| (counts[&subdiv].0, subdiv))
        .map(|(a, b)| (b, a))
        .tqdm()
        .collect()
}

fn gen_subdivisions<'a>(tokens: &[Token<'a>]) -> Vec<(Subdivision<'a>, i32)> {
    println!("Generating Subdivisions");
    let subdivs = (0..tokens.len())
        .collect_vec()
        .into_iter()
        .tqdm()
        .flat_map(|start| (start..tokens.len()).map(|end| (start, end)).collect_vec())
        .map(|(start, end)| Subdivision {
            tokens: tokens[start..=end].to_vec(),
            start: start as i32,
            end: end as i32,
            hashed_tokens: RefCell::new(None),
        })
        .filter(|subdiv| {
            let s = reconstruct_source(
                subdiv
                    .tokens
                    .clone()
                    .into_iter()
                    .map(Either::Right)
                    .collect_vec()
                    .as_slice(),
            );
            !(s.contains("#") || s.contains("define") | s.contains("include"))
        })
        .sorted_by_key(|subdiv| subdiv.start)
        .collect();
    unique_lists_with_counts(subdivs)
}
fn either_spelling(token: &Either<String, Token<'_>>) -> String {
    match token {
        Either::Left(x) => x.clone(),
        Either::Right(t) => t.get_spelling(),
    }
}

fn generate_one_macro(tokens: Vec<Token>, name: String) -> Vec<Either<String, Token>> {
    let subdivs = gen_subdivisions(&tokens);
    println!("Sorting and Scoring");
    let adjusted_subdivs = subdivs
        .into_iter()
        .map(|(subdiv, frequency)| {
            (
                frequency * (1 - subdiv.tokens.len() as i32) + 2 + 1 + subdiv.tokens.len() as i32,
                subdiv,
            )
        })
        .map(|(a, b)| (b, a))
        .tqdm()
        .sorted_by_key(|(_subdiv, score)| *score)
        .collect_vec();

    println!("Top subdiv:");
    for (subdiv, score) in adjusted_subdivs.iter().take(1) {
        println!(
            "==== Score: {score}\n{:?}",
            reconstruct_source(
                &subdiv
                    .tokens
                    .clone()
                    .into_iter()
                    .map(Either::Right)
                    .collect_vec()
            )
        );
    }
    println!("====");

    let best_subdiv = &adjusted_subdivs[0].0;

    let new_macro = Either::Left(name);
    let mut definition = vec![
        Either::Left("#".to_string()),
        Either::Left("define".to_string()),
        new_macro.clone(),
        Either::Left("\\".to_string()),
    ];
    definition.extend(best_subdiv.tokens.clone().into_iter().map(Either::Right));
    definition.push(Either::Left("\n".to_string()));
    definition = vec![Either::Left(
        reconstruct_source(&definition).replace("\n", " ") + "\n",
    )];

    let mut tokens = tokens.into_iter().map(Either::Right).collect_vec();

    let mut i = 0;
    while i < tokens.len() {
        let mut found = true;
        for j in 0..best_subdiv.tokens.len() {
            if (i + j) >= tokens.len() {
                found = false;
                break;
            }

            if either_spelling(&tokens[i + j]) != best_subdiv.tokens[j].get_spelling() {
                found = false;
                break;
            }
        }
        if found {
            tokens.splice(i..(i + best_subdiv.tokens.len()), vec![new_macro.clone()]);
        }
        i += 1;
    }
    tokens.splice(0..0, definition);

    tokens
}

fn get_tu<'a>(index: &'a Index<'a>, src: &str) -> TranslationUnit<'a> {
    let mut parser = index.parser("test.c");
    parser.unsaved(&[Unsaved::new("test.c", src)]);
    parser.parse().unwrap()
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

fn reconstruct_source(tokens: &[Either<String, Token>]) -> String {
    let mut out = vec!["".to_string()];
    let mut line = match tokens[0] {
        Either::Left(_) => -1,
        Either::Right(t) => t.get_location().get_spelling_location().line as i32,
    };
    let mut col = match tokens[0] {
        Either::Left(_) => -1,
        Either::Right(t) => t.get_location().get_spelling_location().column as i32,
    };

    for token in tokens {
        let mut last_line = out.last_mut().unwrap();

        match token {
            Either::Left(s) => {
                last_line.push(' ');
                last_line.push_str(s);
            }
            Either::Right(t) => {
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
                last_line.push_str(&t.get_spelling());
            }
        }
    }

    out.into_iter()
        .map(|line| line.trim().to_string())
        .join("\n")
        .replace("\\\n", "")
        .lines()
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

    let index = Index::new(&clang, false, true);

    let mut source = std::fs::read_to_string("../example_bot_minimized.c").unwrap();
    let mut prev_source = "".to_string();

    let mut prev_tokens = 99999999;

    loop {
        let tu = get_tu(&index, &source);

        let tokens = get_tokens(&tu);
        println!("Current Token Count: {}", tokens.len());
        if tokens.len() > prev_tokens {
            println!("Tokens increased from {prev_tokens}. Exiting");
            break;
        }
        prev_tokens = tokens.len();

        let mut i = 0;
        while source.contains(&format!("rust_macro{i}")) {
            i += 1;
        }

        let macroed_tokens = generate_one_macro(tokens, format!("rust_macro{i}"));

        prev_source = source;
        source = reconstruct_source(&macroed_tokens);

        std::fs::write("../example_bot_minimized.c", &source).unwrap();
    }

    std::fs::write("../example_bot_minimized.c", &prev_source).unwrap();
}
