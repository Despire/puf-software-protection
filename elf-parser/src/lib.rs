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
pub struct Input {
    // path to the elf file to be parsed/
    #[serde(rename = "elf")]
    pub elf: String,
    // multiplier used in the hash function.
    #[serde(rename = "constant")]
    pub constant: u64,
    // function names for which to calculate a hash.
    #[serde(rename = "functions")]
    pub functions: Vec<String>,
}

#[derive(Serialize, Deserialize)]
pub struct Function {
    #[serde(rename = "function")]
    pub function: String,
    #[serde(rename = "hash")]
    pub hash: u64,
    #[serde(rename = "instruction_count")]
    pub instruction_count: usize,
    #[serde(rename = "instructions")]
    pub instructions: Vec<u32>,
}

pub struct Config {
    pub input_path: String,
    pub output_path: String,
}

impl Config {
    pub fn new(args: &mut env::Args) -> Result<Config, Box<dyn std::error::Error>> {
        let mut programs_args = args.skip(1).take(2);

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

        Ok(Config {
            input_path,
            output_path,
        })
    }
}

pub fn run(cfg: &Config) -> Result<(), Box<dyn std::error::Error>> {
    let input = fs::read_to_string(&cfg.input_path)?;
    let input: Input = serde_json::from_str(&input)?;

    let file = fs::read(input.elf)?;
    let elf = Elf::parse(&file)?;

    let functions: Vec<(String, elf::Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);
                if input.functions.contains(&str_name) {
                    return Some((str_name, sym));
                }
            }
            None
        })
        .filter_map(|x| x)
        .collect();

    let mut fncs: Vec<Function> = Vec::new();
    for (str_name, sym) in functions {
        let func_addr = sym.st_value;
        let func_size = sym.st_size;

        let func_instructions = &file[func_addr as usize..(func_addr + func_size) as usize];
        let hash = hash(func_instructions, input.constant);

        let be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| {
                // interpret as little endian.
                chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32)
            })
            .collect();

        fncs.push(Function {
            function: str_name,
            hash,
            instruction_count: be_instructions.len(),
            instructions: be_instructions,
        });
    }

    let out = std::fs::File::create(&cfg.output_path)?;
    serde_json::to_writer_pretty(out, &fncs)?;
    Ok(())
}

fn hash(data: &[u8], multiplier: u64) -> u64 {
    data.iter().map(|&b| multiplier * b as u64).sum()
}
