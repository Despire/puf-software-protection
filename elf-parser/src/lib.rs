//!
//! This module parser an elf binary and calculates checksums for each of
//! the requested functions.
use goblin::elf;
use goblin::elf::Elf;
use serde::{Deserialize, Serialize};
use std::env;
use std::fmt;
use std::fs;

#[derive(Debug)]
struct ArgError {
    details: String,
}

impl ArgError {
    fn new(msg: &str) -> ArgError {
        ArgError {
            details: msg.to_string(),
        }
    }
}

impl fmt::Display for ArgError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.details)
    }
}

impl std::error::Error for ArgError {
    fn description(&self) -> &str {
        &self.details
    }
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
pub struct OffsetRequest {
    #[serde(rename = "function")]
    pub function: String,
}

#[derive(Serialize, Deserialize)]
pub struct Input {
    #[serde(rename = "function_metadata")]
    pub function_metadata: Vec<MetadataRequest>,
}

#[derive(Serialize, Deserialize)]
pub struct Output {
    #[serde(rename = "metadata")]
    pub metadata: Vec<Function>,
    #[serde(rename = "offsets")]
    pub offsets: Vec<FunctionBase>,
}

#[derive(Serialize, Deserialize)]
pub struct FunctionBase {
    #[serde(rename = "function")]
    pub function: String,
    #[serde(rename = "offset")]
    pub offset: u32,
}

#[derive(Serialize, Deserialize)]
pub struct Function {
    #[serde(rename = "base")]
    pub base: FunctionBase,
    #[serde(rename = "hash")]
    pub hash: u32,
    #[serde(rename = "constant")]
    pub constant: u32,
    #[serde(rename = "instruction_count")]
    pub instruction_count: usize,
    #[serde(rename = "instructions")]
    pub instructions: Vec<u32>,
}

pub struct Config {
    pub elf_path: String,
    pub input_path: String,
    pub output_path: String,
}

impl Config {
    pub fn new(args: &mut env::Args) -> Result<Config, Box<dyn std::error::Error>> {
        let mut programs_args = args.skip(1).take(3);

        let input_path = match programs_args.next() {
            Some(path) => path,
            None => return Err(ArgError::new("expected input file path as first argument"))?,
        };

        let output_path = match programs_args.next() {
            Some(path) => path,
            None => {
                return Err(ArgError::new(
                    "expected output file path as second argument",
                ))?
            }
        };

        let elf_path = match programs_args.next() {
            Some(path) => path,
            None => {
                return Err(ArgError::new(
                    "expected path to elf file to parse as third argument",
                ))?
            }
        };

        Ok(Config {
            elf_path,
            input_path,
            output_path,
        })
    }
}

pub fn run(cfg: &Config) -> Result<(), Box<dyn std::error::Error>> {
    let input = fs::read_to_string(&cfg.input_path)?;
    let input: Input = serde_json::from_str(&input)?;

    let file = fs::read(&cfg.elf_path)?;
    let elf = Elf::parse(&file)?;

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
    assert!(functions_metadata.len() == input.function_metadata.len());

    let functions_offset: Vec<(String, elf::Sym)> = elf
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

    let mut out_metadata: Vec<Function> = Vec::new();
    for (str_name, constant, sym) in functions_metadata {
        let func_addr = sym.st_value;
        let func_size = sym.st_size;

        let func_instructions = &file[func_addr as usize..(func_addr + func_size) as usize];

        let be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
            .collect();

        let hash = hash5(&be_instructions, constant);

        out_metadata.push(Function {
            base: FunctionBase {
                function: str_name,
                offset: func_addr as u32,
            },
            hash,
            constant,
            instruction_count: be_instructions.len(),
            instructions: be_instructions,
        });
    }

    let mut out_offsets: Vec<FunctionBase> = Vec::new();
    for (str_name, sym) in functions_offset {
        out_offsets.push(FunctionBase {
            function: str_name,
            offset: sym.st_value as u32,
        });
    }

    let out_file = std::fs::File::create(&cfg.output_path)?;
    let out = Output {
        offsets: out_offsets,
        metadata: out_metadata,
    };
    serde_json::to_writer_pretty(out_file, &out)?;
    Ok(())
}

fn hash5(data: &[u32], c: u32) -> u32 {
    let mut h: u32 = 0;
    for b in data {
        h = c.wrapping_mul((*b as u32).wrapping_add(h));
    }
    h
}
