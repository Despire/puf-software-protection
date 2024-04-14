use goblin::elf;
use goblin::elf::Elf;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::fs::File;

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

pub fn run(elf_path: String, input_path: String) -> Result<(), Box<dyn std::error::Error>> {
    let input = fs::read_to_string(&input_path)?;
    let mut input: Input = serde_json::from_str(&input)?;

    let elf_raw_bytes = fs::read(&elf_path)?;
    let elf = Elf::parse(&elf_raw_bytes)?;

    let mut processed: HashSet<String> = HashSet::new();
    let functions_to_patch: Vec<(String, u32, elf::Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);
                if processed.contains(&str_name) {
                    return None;
                }

                for request in &input.function_metadata {
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

    // filter out all symbols that are not in the final binary.
    input.function_metadata = input
        .function_metadata
        .iter()
        .filter_map(|p| {
            let found = functions_to_patch.iter().find(|(name, _, _)| {
                return &p.function == name;
            });
            if found.is_some() {
                return Some(MetadataRequest {
                    function: String::from(&p.function),
                    constant: p.constant,
                });
            }
            None
        })
        .collect();

    assert_eq!(functions_to_patch.len(), input.function_metadata.len());
    serde_json::to_writer_pretty(File::create(&input_path)?, &input)?;
    Ok(())
}
