use std::fs;
use std::mem::size_of;
use goblin::elf;
use goblin::elf::Elf;
use serde::{Deserialize, Serialize};

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
pub struct Input {
    #[serde(rename = "function_metadata")]
    pub function_metadata: Vec<MetadataRequest>,
}

pub fn run(elf_path: String, input: String) -> Result<(), Box<dyn std::error::Error>> {
    let input = fs::read_to_string(&input)?;
    let input: Input = serde_json::from_str(&input)?;

    let mut elf_raw_bytes = fs::read(&elf_path)?;
    let elf = Elf::parse(&elf_raw_bytes)?;

    let text_section = elf.section_headers.iter().find(|header| {
        if let Some(name) = elf.shdr_strtab.get_at(header.sh_name) {
            return name == ".text";
        }
        false
    }).expect("failed to find .text section of an elf");

    let functions_metadata: Vec<(String, u32, elf::Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);

                for request in &input.function_metadata {
                    if request.function == str_name {
                        return Some((str_name, request.constant, sym));
                    }
                }
            }
            None
        })
        .filter_map(|x| x)
        .collect();
    assert_eq!(functions_metadata.len(), input.function_metadata.len());

    for (str_name, constant, sym) in functions_metadata {
        let func_addr = text_section.sh_offset + (sym.st_value - text_section.sh_addr);
        let func_size = sym.st_size;

        println!("patching function: {}, offset: {} size: {}", str_name, func_addr, func_size);

        let func_instructions = &mut elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        let mut be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
            .collect();

        // define PARITY_INSTRUCTION_INT=66051
        // look for this number and patch it so that it sums to 0
        const PARITY_INT: u32 = 66051;

        assert_eq!(be_instructions.iter().map(|n| {
            if *n == PARITY_INT {
                return 1;
            }
            return 0;
        }).sum::<u32>(), 1);

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
        let mut c = constant as u64;
        let mut parity_c = 0u64;

        instructions_copy.iter_mut().rev().enumerate().for_each(|(i, item)| {
            let tmp = *item;
            *item = (*item).wrapping_mul(c as u32);

            if tmp != PARITY_INT {
                z += *item as u64;
            } else {
                parity_c = c;
                copy_index = i;
            }

            c = c.wrapping_mul(constant as u64);
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
        println!("{:x}", patch_instruction);
        be_instructions[idx] = patch_instruction;

        assert_eq!(hash5(&be_instructions, constant), 0);

        let parity_offset_in_func = idx * size_of::<u32>();
        let patch_instruction_bytes = patch_instruction.to_be_bytes();

        func_instructions[parity_offset_in_func .. parity_offset_in_func + size_of::<u32>()]
            .copy_from_slice(&patch_instruction_bytes);
    }

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