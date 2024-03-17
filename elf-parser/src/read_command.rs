use std::fs;
use std::fs::File;
use std::path::Path;
use goblin::elf;
use goblin::elf::Elf;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct FunctionBase {
    #[serde(rename = "function")]
    pub function: String,
    #[serde(rename = "offset")]
    pub offset: u32,
}

#[derive(Serialize, Deserialize)]
pub struct FunctionInfo {
    #[serde(rename = "base")]
    pub base: FunctionBase,
    #[serde(rename = "constant")]
    pub constant: u32,
    #[serde(rename = "instruction_count")]
    pub instruction_count: usize,
}

#[derive(Serialize, Deserialize)]
pub struct Output {
    // multiplier used in the hash function.
    #[serde(rename = "function_metadata")]
    pub function_metadata: Vec<FunctionInfo>,
}

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

pub fn run(elf_path: String, input_path: String, output_path: String) -> Result<(), Box<dyn std::error::Error>> {
    let input = fs::read_to_string(&input_path)?;
    let mut input: Input = serde_json::from_str(&input)?;

    let elf_raw_bytes = fs::read(&elf_path)?;
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

    // filter our all symbols that are not in the final binary.
    input.function_metadata = input.function_metadata.iter().filter_map(|p| {
        let found = functions_metadata.iter().find(|(name, _, _)| {
            return &p.function == name;
        });
        if found.is_some() {
            return Some(MetadataRequest{
                function: String::from(&p.function),
                constant: p.constant
            });
        }
        None
    }).collect();
    assert_eq!(functions_metadata.len(), input.function_metadata.len());

    serde_json::to_writer_pretty(File::create(&input_path)?, &input)?;

    let mut c = Vec::new();
    for (str_name, constant, sym) in functions_metadata {
        let func_addr = text_section.sh_offset + (sym.st_value - text_section.sh_addr);
        let func_size = sym.st_size;

        println!("reading function: {}, offset: {} size: {}", str_name, func_addr, func_size);

        let func_instructions = &elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        let be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
            .collect();

        c.push(FunctionInfo {
            base: FunctionBase {
                function: str_name,
                offset: sym.st_value as u32,
            },
            constant,
            instruction_count: be_instructions.len(),
        })
    }

    let out_file = File::create(output_path)?;
    let out = Output { function_metadata: c };
    serde_json::to_writer_pretty(out_file, &out)?;
    Ok(())
}