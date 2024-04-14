use goblin::elf::{Elf, SectionHeader, Sym};
use rand::prelude::IteratorRandom;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::mem::size_of;

// In a function, look for this number and patch it so that it sums to 0
const PARITY_INT: u32 = 66051;

// Marker for the start followed by count, constant of a checksum block to path
const START: u32 = 3789400763;
const COUNT: u32 = 3806177979;
const CONST: u32 = 3822955195;

// Marker for replacement of reference values.
const REPLACEMENT: u32 = 3839732411;

#[derive(Serialize, Deserialize)]
pub struct MetadataRequest {
    // multiplier used in the hash function.
    #[serde(rename = "constant")]
    pub constant: u32,
    // function names for which to calculate a hash.
    #[serde(rename = "function")]
    pub function: String,
}

#[derive(Serialize, Deserialize)]
pub struct FunctionsToPatch {
    #[serde(rename = "function_metadata")]
    pub function_metadata: Vec<MetadataRequest>,
}

#[derive(Serialize, Deserialize)]
pub struct Replacement {
    // puf response used for the function to patch
    #[serde(rename = "puf_response")]
    pub puf_response: u32,

    // which function to patch
    #[serde(rename = "function")]
    pub function: String,

    // which function offset to subtract from the PUF response.
    #[serde(rename = "take_offset_from_function")]
    pub take_offset_from_function: String,
}

#[derive(Serialize, Deserialize)]
pub struct ReplacementRequest {
    #[serde(rename = "replacements")]
    pub replacements: Vec<Replacement>,
}

pub fn run(
    elf_path: String,
    functions_to_patch: String,
    replacements_in_functions: String,
) -> Result<(), Box<dyn std::error::Error>> {
    let functions_to_patch = fs::read_to_string(&functions_to_patch)?;
    let functions_to_patch: FunctionsToPatch = serde_json::from_str(&functions_to_patch)?;

    let replacements_in_functions = fs::read_to_string(&replacements_in_functions)?;
    let replacements_in_functions: ReplacementRequest =
        serde_json::from_str(&replacements_in_functions)?;

    let mut elf_raw_bytes = fs::read(&elf_path)?;
    let elf = Elf::parse(&elf_raw_bytes)?;

    let text_section = elf
        .section_headers
        .iter()
        .find(|header| {
            if let Some(name) = elf.shdr_strtab.get_at(header.sh_name) {
                return name == ".text";
            }
            false
        })
        .expect("failed to find .text section of an elf");

    let mut processed: HashSet<String> = HashSet::new();
    let functions_metadata: Vec<(String, u32, Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);
                if processed.contains(&str_name) {
                    return None;
                }

                for request in &functions_to_patch.function_metadata {
                    if request.function == str_name {
                        processed.insert(str_name.clone());
                        return Some((str_name, request.constant, sym));
                    }
                }
            }
            None
        })
        .filter_map(|x| x)
        .collect();
    assert_eq!(
        functions_metadata.len(),
        functions_to_patch.function_metadata.len()
    );

    let all_functions: Vec<(String, Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);
                return Some((str_name, sym));
            }
            None
        })
        .filter_map(|x| x)
        .collect();

    patch_replacements(
        &all_functions,
        &text_section,
        &replacements_in_functions,
        &mut elf_raw_bytes,
    );

    patch_checksum(
        &all_functions,
        &functions_metadata,
        &text_section,
        &mut elf_raw_bytes,
    );

    patch_parity(&functions_metadata, &text_section, &mut elf_raw_bytes);

    fs::write(elf_path, elf_raw_bytes)?;
    Ok(())
}

pub fn hash5(data: &[u32], c: u32) -> u32 {
    let mut h: u32 = 0;
    for b in data {
        h = c.wrapping_mul((*b).wrapping_add(h));
    }
    h
}

fn eegcd(mut a: u64, x: &mut u64, y: &mut u64) {
    let mut b = u32::MAX as u64 + 1;
    let mut x0: u64 = 1;
    let mut x1: u64 = 0;
    let mut y0: u64 = 0;
    let mut y1: u64 = 1;

    while b != 0 {
        let q: u64 = a / b;
        let temp: u64 = b;
        b = a % b;
        a = temp;

        let temp_x: u64 = x0.wrapping_sub(q.wrapping_mul(x1));
        let temp_y: u64 = y0.wrapping_sub(q.wrapping_mul(y1));

        x0 = x1;
        y0 = y1;

        x1 = temp_x;
        y1 = temp_y;
    }

    assert_eq!(a, 1);

    *x = x0;
    *y = y0;
}

fn patch_replacements(
    all_functions: &[(String, Sym)],
    text_section: &SectionHeader,
    replacements_in_functions: &ReplacementRequest,
    elf_raw_bytes: &mut [u8],
) {
    let functions_lookup: HashMap<String, Sym> = all_functions
        .iter()
        .map(|(name, sym)| {
            return Some((name.clone(), sym.clone()));
        })
        .filter_map(|x| x)
        .collect();

    let functions_for_replacement: Vec<(String, Sym, &Replacement)> = all_functions
        .iter()
        .map(|(name, sym)| {
            for req in &replacements_in_functions.replacements {
                if req.function.as_str() == name {
                    return Some((name.clone(), sym.clone(), req));
                }
            }
            None
        })
        .filter_map(|x| x)
        .collect();

    for f in &functions_for_replacement {
        assert!(functions_lookup.contains_key(&f.2.take_offset_from_function));
        let function_for_offset = functions_lookup
            .get(&f.2.take_offset_from_function)
            .unwrap();

        let func_addr = text_section.sh_offset + (f.1.st_value - text_section.sh_addr);
        let func_size = f.1.st_size;

        let function_for_offset_addr = function_for_offset.st_value;
        let reference_value = (function_for_offset_addr as u32).wrapping_sub(f.2.puf_response);

        let be_instructions =
            func_be_instructions(elf_raw_bytes, text_section, f.1.st_value, f.1.st_size);

        let marker = be_instructions.iter().position(|v| *v == REPLACEMENT);

        if marker.is_none() {
            continue;
        }

        assert_eq!(be_instructions[marker.unwrap()], REPLACEMENT);

        println!(
            "replacing in function {} with reference value {} from function {}",
            &f.0, reference_value, &f.2.take_offset_from_function
        );

        let start_addr_idx = marker.unwrap() * size_of::<u32>();

        let func_instructions =
            &mut elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        func_instructions[start_addr_idx..start_addr_idx + size_of::<u32>()]
            .copy_from_slice(reference_value.to_le_bytes().as_ref());
    }
}

fn patch_parity(
    functions_metadata: &[(String, u32, Sym)],
    text_section: &SectionHeader,
    elf_raw_bytes: &mut [u8],
) {
    for (str_name, constant, sym) in functions_metadata {
        let func_addr = text_section.sh_offset + (sym.st_value - text_section.sh_addr);
        let func_size = sym.st_size;

        println!(
            "patching function parity: {}, offset: {} size: {}",
            str_name, func_addr, func_size
        );

        let func_instructions =
            &mut elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        let mut be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
            .collect();

        assert_eq!(
            be_instructions
                .iter()
                .map(|n| {
                    if *n == PARITY_INT {
                        return 1;
                    }
                    return 0;
                })
                .sum::<u32>(),
            1
        );

        let mut idx = 0x0;
        for i in 0..be_instructions.len() {
            if be_instructions[i] == PARITY_INT {
                idx = i;
                break;
            }
        }
        let mut instructions_copy = be_instructions.clone();
        let mut copy_index = 0x0;
        let mut z = 0u64;
        let mut c = *constant as u64;
        let mut parity_c = 0u64;

        instructions_copy
            .iter_mut()
            .rev()
            .enumerate()
            .for_each(|(i, item)| {
                let tmp = *item;
                *item = (*item).wrapping_mul(c as u32);

                if tmp != PARITY_INT {
                    z += *item as u64;
                } else {
                    parity_c = c;
                    copy_index = i;
                }

                c = c.wrapping_mul(*constant as u64);
            });

        let mut x = 0u64;
        let mut y = 0u64;
        eegcd(parity_c, &mut x, &mut y);
        x = x.wrapping_mul((u32::MAX as u64 + 1).wrapping_sub(z));
        x = x % (u32::MAX as u64 + 1);

        let reverse_index = (instructions_copy.len() - 1) - copy_index;
        instructions_copy[reverse_index] = (x as u32).wrapping_mul(parity_c as u32);

        let mut sum: u32 = 0;
        instructions_copy.iter().for_each(|n| {
            sum = sum.wrapping_add(*n);
        });
        assert_eq!(0, 0u32);

        let patch_instruction = x as u32;
        be_instructions[idx] = patch_instruction;

        assert_eq!(hash5(&be_instructions, *constant), 0);

        let parity_offset_in_func = idx * size_of::<u32>();
        let patch_instruction_bytes = patch_instruction.to_be_bytes();

        func_instructions[parity_offset_in_func..parity_offset_in_func + size_of::<u32>()]
            .copy_from_slice(&patch_instruction_bytes);
    }
}

fn patch_checksum(
    all_functions: &[(String, Sym)],
    functions_metadata: &[(String, u32, Sym)],
    text_section: &SectionHeader,
    elf_raw_bytes: &mut [u8],
) {
    let text_section_start = text_section.sh_addr;
    let text_section_end = text_section.sh_addr + text_section.sh_size;
    assert!(!functions_metadata.is_empty());

    let mut to_be_processed_functions = Vec::from(functions_metadata);
    let mut processed_functions = Vec::new();
    for (str_name, sym) in all_functions {
        if sym.st_size == 0 || sym.st_value < text_section_start || sym.st_value >= text_section_end
        {
            continue;
        }

        let func_addr = text_section.sh_offset + (sym.st_value - text_section.sh_addr);
        let func_size = sym.st_size;

        let be_instructions =
            func_be_instructions(elf_raw_bytes, text_section, sym.st_value, sym.st_size);
        let marker = be_instructions.iter().position(|v| *v == START);

        if marker.is_none() {
            continue;
        }

        assert_eq!(be_instructions[marker.unwrap()], START);
        assert_eq!(be_instructions[marker.unwrap() + 1], COUNT);
        assert_eq!(be_instructions[marker.unwrap() + 2], CONST);

        println!(
            "patching function checksum: {}, offset: {} size: {}",
            str_name, func_addr, func_size
        );

        let mut rng = rand::thread_rng();

        if to_be_processed_functions.is_empty() {
            to_be_processed_functions = Vec::from(functions_metadata);
        }

        processed_functions.push(
            to_be_processed_functions.remove(
                to_be_processed_functions
                    .iter()
                    .enumerate()
                    .choose(&mut rng)
                    .map(|(i, _)| i)
                    .unwrap(),
            ),
        );

        let (target_name, target_constant, target_sym) = processed_functions.last().unwrap();

        let rng_func_start: u32 = target_sym.st_value as u32;
        let rng_func_size: u32 = func_be_instructions(
            elf_raw_bytes,
            text_section,
            target_sym.st_value,
            target_sym.st_size,
        )
        .len() as u32;

        let tmp_func_start: u32 = rng_func_start & 0xffff0000 | rng_func_size & 0x0000ffff;
        let tmp_func_size: u32 = rng_func_size & 0xffff0000 | rng_func_start & 0x0000ffff;

        println!(
            "\ttarget_function: {} {:x} {} index: {} encoded_start: {:x} encoded_size: {:x}",
            target_name,
            rng_func_start,
            rng_func_size,
            marker.unwrap(),
            tmp_func_start,
            tmp_func_size
        );

        let start_addr_idx = marker.unwrap() * size_of::<u32>();
        let instruction_count_idx = (marker.unwrap() + 1) * size_of::<u32>();
        let constant_idx = (marker.unwrap() + 2) * size_of::<u32>();

        let func_instructions =
            &mut elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        func_instructions[start_addr_idx..start_addr_idx + size_of::<u32>()]
            .copy_from_slice(tmp_func_start.to_le_bytes().as_ref());

        func_instructions[instruction_count_idx..instruction_count_idx + size_of::<u32>()]
            .copy_from_slice(tmp_func_size.to_le_bytes().as_ref());

        func_instructions[constant_idx..constant_idx + size_of::<u32>()]
            .copy_from_slice(target_constant.to_le_bytes().as_ref());
    }
}

fn func_be_instructions(
    elf_raw_bytes: &mut [u8],
    text_section: &SectionHeader,
    start: u64,
    size: u64,
) -> Vec<u32> {
    let func_addr = text_section.sh_offset + (start - text_section.sh_addr);
    let func_size = size;

    let func_instructions = &elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

    func_instructions
        .chunks(4)
        .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
        .collect::<Vec<u32>>()
}
