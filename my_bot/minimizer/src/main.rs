use std::{
    collections::{HashMap, HashSet},
    hash::{Hash, Hasher},
    ops::Range,
};

use clang::{
    Clang, Index, TranslationUnit, Unsaved,
    token::{Token, TokenKind},
};
use image::{DynamicImage, GenericImageView, ImageReader, Pixel};
use itertools::Itertools;
use rustc_hash::{FxBuildHasher, FxHasher};

const MACRO_OVERHEAD: i32 = 2 + 1;

#[derive(Clone, Copy, Eq)]
struct PrefetchedToken<'source> {
    spelling: &'source [u8],
    hash: u64,
}

impl<'source> std::fmt::Debug for PrefetchedToken<'source> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PrefetchedToken")
            .field("spelling", &core::str::from_utf8(self.spelling).unwrap())
            .field("hash", &self.hash)
            .finish()
    }
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

    let lines = out
        .lines()
        .filter_map(|l| {
            let trimmed = l.trim();
            if trimmed.is_empty() {
                None
            } else {
                Some(trimmed)
            }
        })
        .collect_vec();

    println!("{lines:#?}");

    let img = ImageReader::open("gear.png")
        .unwrap()
        .decode()
        .unwrap()
        .grayscale();

    let mut row_outputs = Vec::new();

    'too_small: for initial_width in 1.. {
        let char_aspect_ratio = 20.0 / 9.0;
        let initial_height = ((initial_width as f32 * img.height() as f32)
            / (img.width() as f32 * char_aspect_ratio))
            .ceil() as u32;

        let char_width = (img.width() / initial_width).max(1);
        let char_height = (img.height() / initial_height).max(1);

        println!("CW: {char_width}, CH: {char_height}");
        println!("IW: {initial_width}, IH: {initial_height}");

        let mut fake_word_source = ["//"].iter().cycle();

        row_outputs = Vec::new();
        let mut lines = lines.iter();
        while let Some(line) = lines.next() {
            let mut words = line.split_whitespace();

            loop {
                if row_outputs.len() as u32 * char_height >= img.height() {
                    if words.clone().next().is_none() && lines.clone().next().is_none() {
                        break 'too_small;
                    } else {
                        println!("------------------------------");
                        continue 'too_small;
                    }
                }

                let spans = num_spans_in_row(&img, row_outputs.len() as u32 * char_height);
                if spans.is_empty() {
                    row_outputs.push(String::new());
                    continue;
                }

                let mut outputs = Vec::new();

                for &span in &spans {
                    let span_chars = span.length / char_width;
                    let mut chars_used = 0;
                    let mut span_words = Vec::new();

                    if let Some(next_word) = words.next() {
                        chars_used += next_word.len() as u32;
                        span_words.push(next_word.to_string());

                        while let Some(word) = words.clone().next() {
                            let next_len = word.len() as u32;

                            if chars_used + 1 + next_len > span_chars {
                                break;
                            }

                            words.next();
                            span_words.push(word.to_string());
                            chars_used += 1 + next_len;
                        }
                    }

                    if words.clone().next().is_none() {
                        while chars_used < span_chars {
                            let next_word = fake_word_source.next().unwrap();
                            let next_len = next_word.len() as u32;
                            let space_before = if span_words.is_empty() { 0 } else { 1 };

                            if chars_used + space_before + next_len > span_chars {
                                break;
                            }

                            if space_before > 0 {
                                chars_used += 1;
                            }
                            span_words.push(next_word.to_string());
                            chars_used += next_len;
                        }
                    }

                    if span_words.is_empty() {
                        span_words.push(fake_word_source.next().unwrap().to_string());
                    }

                    // 4️⃣ Justify the span
                    let mut span_text = String::new();
                    if span_words.len() == 1 {
                        let word = &span_words[0];
                        span_text = format!("{: ^width$}", word, width = span_chars as usize);
                    } else {
                        let gaps = span_words.len() - 1;
                        let total_space = (span_chars as usize)
                            .saturating_sub(span_words.iter().map(|w| w.len()).sum::<usize>());

                        let base_space = if total_space >= gaps {
                            (total_space - gaps) / gaps
                        } else {
                            0
                        };
                        let mut extra_space = if total_space >= gaps {
                            (total_space - gaps) % gaps
                        } else {
                            0
                        };

                        for (i, word) in span_words.iter().enumerate() {
                            span_text.push_str(word);
                            if i < gaps {
                                let spaces = 1 + base_space + if extra_space > 0 { 1 } else { 0 };
                                extra_space = extra_space.saturating_sub(1);
                                span_text.push_str(&" ".repeat(spaces));
                            }
                        }
                    }

                    outputs.push(span_text);
                }

                // Align spans for this row
                let spans_aligned = outputs
                    .into_iter()
                    .zip(spans.iter().map(|s| Span {
                        length: s.length / char_width,
                        start: s.start / char_width,
                    }))
                    .collect::<Vec<_>>();

                let mut output = String::new();
                for (text, span) in spans_aligned {
                    output
                        .push_str(&" ".repeat((span.start as usize).saturating_sub(output.len())));
                    output.push_str(&text);
                }

                println!("{output}");
                row_outputs.push(output);

                if words.clone().next().is_none() && lines.clone().next().is_some() {
                    break;
                }
            }
        }
        break;
    }

    row_outputs.join("\n")
}

#[derive(Copy, Clone, Debug)]
struct Span {
    pub start: u32,
    pub length: u32,
}

fn num_spans_in_row(img: &DynamicImage, row: u32) -> Vec<Span> {
    let mut spans: Vec<Span> = vec![];
    let mut in_shape = false;
    for x in 0..img.width() {
        if let Some(span) = spans.last_mut()
            && in_shape
        {
            span.length += 1;
        }

        if in_shape && img.get_pixel(x, row).channels()[0] == 0 {
            in_shape = false;
        } else if !in_shape && img.get_pixel(x, row).to_rgb()[0] != 0 {
            in_shape = true;
            spans.push(Span {
                length: 0,
                start: x,
            });
        }
    }

    spans
}

#[derive(Clone, Debug)]
struct ParsedMacro<'s> {
    name: PrefetchedToken<'s>,
    body: Vec<PrefetchedToken<'s>>,
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd, Hash)]
enum Side {
    Front,
    Back,
}

impl Side {
    fn opposite(self) -> Self {
        match self {
            Side::Front => Side::Back,
            Side::Back => Side::Front,
        }
    }
}

fn resolve_token<'s, 'a>(
    token: &'a PrefetchedToken<'s>,
    side: Side,
    macros: &'a [ParsedMacro<'s>],
) -> &'a PrefetchedToken<'s> {
    if let Some(t) = macros.iter().find_map(|m| match side {
        Side::Front if m.name == *token => m
            .body
            .first()
            .map(|token| resolve_token(token, side, macros)),
        Side::Back if m.name == *token => m
            .body
            .last()
            .map(|token| resolve_token(token, side, macros)),
        _ => None,
    }) {
        t
    } else {
        token
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
enum MacroLocation {
    Nested,
    Leaf,
}

/// Find out how deeply nested macros[index] is on the Front or Back side.
/// Add the leaf macro to macros_to_outline
fn locate_deepest_macro<'s>(
    side: Side,
    macros: &[ParsedMacro<'s>],
    index: usize,
    macros_to_outline: &mut HashSet<PrefetchedToken<'s>>,
) -> MacroLocation {
    let token = match side {
        Side::Front => macros[index].body.first().cloned(),
        Side::Back => macros[index].body.last().cloned(),
    };

    let Some(token) = token else {
        return MacroLocation::Nested;
    };

    if let Some((i, _m)) = macros.iter().enumerate().find(|(_, m)| m.name == token) {
        locate_deepest_macro(side, macros, i, macros_to_outline);

        MacroLocation::Nested
    } else {
        macros_to_outline.insert(macros[index].name);
        MacroLocation::Leaf
    }
}

#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
enum IndexType {
    Macro { macro_index: usize, index: usize },
    Tokens(usize),
}

impl IndexType {
    fn resolve<'s, 'b>(
        self,
        macros: &'b [ParsedMacro<'s>],
        tokens: &'b [PrefetchedToken<'s>],
    ) -> &'b PrefetchedToken<'s> {
        match self {
            IndexType::Macro { macro_index, index } => &macros[macro_index].body[index],
            IndexType::Tokens(i) => &tokens[i],
        }
    }
}

fn outline_tokens<'s>(
    tokens: &mut Vec<PrefetchedToken<'s>>,
    macros: &[ParsedMacro<'s>],
    macros_to_outline: &HashSet<PrefetchedToken<'s>>,
    side: Side,
    current_macro: Option<&PrefetchedToken<'s>>, // used to avoid neighbor collisions
) {
    *tokens = tokens
        .iter()
        .enumerate()
        .flat_map(|(i, t)| {
            let neighbour_is_the_macro = current_macro.is_some_and(|m_name| match side {
                Side::Front => tokens.get(i + 1) == Some(m_name),
                Side::Back => tokens.get(i - 1) == Some(m_name),
            });

            if macros_to_outline.contains(t) && !neighbour_is_the_macro {
                match side {
                    Side::Front => vec![*t, *resolve_token(t, side.opposite(), macros)],
                    Side::Back => vec![*resolve_token(t, side.opposite(), macros), *t],
                }
            } else {
                vec![*t]
            }
        })
        .collect();
}

fn absorb_macro_from_side<'s>(
    index: usize,
    macros: &mut Vec<ParsedMacro<'s>>,
    mut tokens: Vec<PrefetchedToken<'s>>,
    side: Side,
) -> Vec<PrefetchedToken<'s>> {
    let m = macros[index].clone();

    // Collect all tokens (top-level + all macro bodies) for consistency check
    let all_tokens = tokens
        .iter()
        .chain(macros.iter().flat_map(|m| m.body.iter()))
        .collect_vec();

    let can_replace = all_tokens
        .iter()
        .enumerate()
        .filter(|(_i, tok)| ***tok == m.name)
        .map(|(i, _tok)| match side {
            Side::Front => all_tokens[i - 1],
            Side::Back => all_tokens[i + 1],
        })
        .all_equal();

    if !can_replace {
        return tokens;
    }

    // Collect all indices for replacement
    let indices: HashSet<IndexType> = tokens
        .iter()
        .enumerate()
        .filter(|(_i, tok)| **tok == m.name)
        .map(|(i, _)| match side {
            Side::Front => IndexType::Tokens(i - 1),
            Side::Back => IndexType::Tokens(i + 1),
        })
        .chain(macros.iter().enumerate().flat_map(|(macro_index, m)| {
            m.body
                .iter()
                .enumerate()
                .filter(|(_i, tok)| **tok == m.name)
                .map(|(i, _)| match side {
                    Side::Front => IndexType::Macro {
                        macro_index,
                        index: i - 1,
                    },
                    Side::Back => IndexType::Macro {
                        macro_index,
                        index: i + 1,
                    },
                })
                .collect_vec()
        }))
        .collect();

    if indices.is_empty() {
        return tokens;
    }

    let the_token = *resolve_token(
        indices.iter().next().unwrap().resolve(macros, &tokens),
        side.opposite(),
        macros,
    );

    println!("Macro {:?}, {side:?}", m.name);
    println!("Replacing {the_token:?}");

    let mut macros_to_outline = HashSet::new();

    // Filter top-level tokens
    tokens = tokens
        .into_iter()
        .enumerate()
        .filter_map(|(i, t)| {
            if indices.contains(&IndexType::Tokens(i)) {
                if let Some((i, _)) = macros.iter().find_position(|m| m.name == t) {
                    locate_deepest_macro(side.opposite(), macros, i, &mut macros_to_outline);
                    Some(t)
                } else {
                    None
                }
            } else {
                Some(t)
            }
        })
        .collect_vec();

    let macros_clone = macros.clone();

    // Filter macro bodies and collect outline info
    for (macro_index, m) in macros.iter_mut().enumerate() {
        m.body = m
            .body
            .iter()
            .enumerate()
            .filter_map(|(i, t)| {
                if indices.contains(&IndexType::Macro {
                    macro_index,
                    index: i,
                }) {
                    if let Some((i, _)) = macros_clone.iter().find_position(|m| m.name == *t) {
                        locate_deepest_macro(
                            side.opposite(),
                            &macros_clone,
                            i,
                            &mut macros_to_outline,
                        );
                        Some(t)
                    } else {
                        None
                    }
                } else {
                    Some(t)
                }
            })
            .cloned()
            .collect_vec();
    }

    println!("Outlining {macros_to_outline:?}");

    // Apply outlining to top-level tokens
    outline_tokens(&mut tokens, macros, &macros_to_outline, side, Some(&m.name));

    // Apply outlining to all macro bodies
    for inner_m in macros.iter_mut() {
        outline_tokens(
            &mut inner_m.body,
            &macros_clone,
            &macros_to_outline,
            side,
            Some(&m.name),
        );
    }

    // Finally, insert the absorbed token into the current macro
    match side {
        Side::Front => {
            macros[index].body.insert(0, the_token);
            for m in macros
                .iter_mut()
                .filter(|m| macros_to_outline.contains(&m.name))
            {
                m.body.pop();
            }
        }
        Side::Back => {
            macros[index].body.push(the_token);
            for m in macros
                .iter_mut()
                .filter(|m| macros_to_outline.contains(&m.name))
            {
                m.body.remove(0);
            }
        }
    }

    tokens
}

fn count_tokens<'s>(macros: &[ParsedMacro<'s>], tokens: &[PrefetchedToken<'s>]) -> usize {
    macros.iter().map(|m| m.body.len() + 3).sum::<usize>() + tokens.len()
}

fn absorb_macros<'s>(
    macros: &mut Vec<ParsedMacro<'s>>,
    mut tokens: Vec<PrefetchedToken<'s>>,
) -> Vec<PrefetchedToken<'s>> {
    let mut still_going = true;
    while still_going {
        still_going = false;

        for side in [Side::Front, Side::Back] {
            for i in 0..macros.len() {
                let mut macros_clone = macros.clone();
                let new_tokens = absorb_macro_from_side(i, &mut macros_clone, tokens.clone(), side);

                macros_clone.retain(|m| !m.body.is_empty());

                if count_tokens(&macros_clone, &new_tokens) < count_tokens(macros, &tokens) {
                    tokens = new_tokens;
                    *macros = macros_clone;
                    still_going = true;

                    {
                        let tokens = macros
                            .iter()
                            .flat_map(|m| {
                                [
                                    vec![
                                        PrefetchedToken::new(b"#"),
                                        PrefetchedToken::new(b"define"),
                                        m.name,
                                    ],
                                    m.body.clone(),
                                ]
                            })
                            .flatten()
                            .chain(tokens.clone())
                            .collect_vec();

                        println!("Absorbed to {}", tokens.len());

                        let source = reconstruct_source(&tokens);

                        std::fs::write("../example_bot_minimized.c", &source).unwrap();

                        println!("\n------------\n{source}\n------------\n");
                        std::io::stdin().read_line(&mut String::new()).unwrap();
                    }

                    break;
                }
            }
        }
    }

    tokens
}

fn main() {
    let clang = Clang::new().unwrap();

    let index = Index::new(&clang, false, false);

    let source = std::fs::read_to_string("../example_bot_clean.c").unwrap();

    let mut prev_tokens = 99999999;
    let tu = get_tu(&index, &source);
    let tokens = get_tokens(&tu);
    let mut tokens = prefetch_tokens(&tokens, source.as_bytes());

    let macro_names = include_str!("macro-names.txt").lines().collect_vec();

    let mut i = 0;
    if true {
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
    }

    let mut tokens_it = tokens.iter().peekable();
    let mut macros = vec![];

    while let (Some(t1), Some(t2)) = (tokens_it.peek().cloned(), tokens_it.clone().nth(1)) {
        if t1.spelling == b"#" && t2.spelling == b"define" {
            tokens_it.next(); // consume '#'
            tokens_it.next(); // consume 'define'

            let name = *tokens_it.next().unwrap();
            let mut m = ParsedMacro { name, body: vec![] };

            while let Some(next) = tokens_it.peek() {
                if next.spelling == b"#" {
                    break;
                }
                m.body.push(*tokens_it.next().unwrap());
            }

            macros.push(m);
        } else {
            break;
        }
    }

    let tokens = tokens_it.cloned().collect_vec();

    // this doesn't actually help
    if false {
        let tokens = absorb_macros(&mut macros, tokens.clone());
    }

    let tokens = macros
        .into_iter()
        .flat_map(|m| {
            [
                vec![
                    PrefetchedToken::new(b"#"),
                    PrefetchedToken::new(b"define"),
                    m.name,
                ],
                m.body,
            ]
        })
        .flatten()
        .chain(tokens)
        .collect_vec();

    println!("Absorbed to {}", tokens.len());

    let source = reconstruct_source(&tokens);

    std::fs::write("../example_bot_minimized.c", &source).unwrap();
}
