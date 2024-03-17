//!
//! This module parser an elf binary and calculates checksums for each of
//! the requested functions.
mod patch_command;
mod read_command;
mod check_command;

use serde::{Deserialize, Serialize};
use std::env;
use std::fmt;

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
pub struct OffsetRequest {
    #[serde(rename = "function")]
    pub function: String,
}

pub fn new(args: &mut env::Args) -> Result<(), Box<dyn std::error::Error>> {
    let mut programs_args = args.skip(1);

    let command = match programs_args.next() {
        Some(command) => command,
        None => return Err(ArgError::new("no command specified, one of <read> <patch>"))?,
    };

    match command.as_str() {
        "read" => {
            // output json from the LLVM pass containing functions info.
            let input_path = match programs_args.next() {
                Some(path) => path,
                None => return Err(ArgError::new("expected input file path as first argument"))?,
            };

            // where the data from the elf will be stored as as json.
            let output_path = match programs_args.next() {
                Some(path) => path,
                None => {
                    return Err(ArgError::new(
                        "expected output file path as second argument",
                    ))?;
                }
            };

            // elf path.
            let elf_path = match programs_args.next() {
                Some(path) => path,
                None => {
                    return Err(ArgError::new(
                        "expected path to elf file to parse as third argument",
                    ))?;
                }
            };

            read_command::run(elf_path, input_path, output_path)
        }
        "patch" => {
            // which functions to patch such that the sums equal to 0.
            let input_path = match programs_args.next() {
                Some(path) => path,
                None => return Err(ArgError::new("expected input file path as first argument"))?,
            };

            // elf path.
            let elf_path = match programs_args.next() {
                Some(path) => path,
                None => {
                    return Err(ArgError::new(
                        "expected path to elf file to parse as third argument",
                    ))?;
                }
            };

            patch_command::run(elf_path, input_path)
        }
        "check" => {
            // json output from the LLVM pass
            let functions_to_check = match programs_args.next() {
                Some(path) => path,
                None => return Err(ArgError::new("expected input file path as first argument"))?,
            };


            // previous output from the <read> command given the json output of the LLVM pass
            let previous_elf_parser_output = match programs_args.next() {
                Some(path) => path,
                None => {
                    return Err(ArgError::new(
                        "expected output file path as second argument",
                    ))?;
                }
            };

            // elf path
            let elf_path = match programs_args.next() {
                Some(path) => path,
                None => {
                    return Err(ArgError::new(
                        "expected path to elf file to parse as third argument",
                    ))?;
                }
            };

            check_command::run(elf_path, functions_to_check, previous_elf_parser_output)
        }
        _ => {
            return Err(ArgError::new("command not recognized only <read>, <patch> is supported"))?;
        }
    }
}