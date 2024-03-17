use std::fs;
use goblin::elf;
use goblin::elf::Elf;
use crate::read_command::{FunctionInfo, Input, Output};

pub fn run(elf_path: String, functions_to_check: String, previous_elf_parser_output: String) -> Result<(), Box<dyn std::error::Error>> {
    let functions_to_check = fs::read_to_string(&functions_to_check)?;
    let previous_elf_parser_output = fs::read_to_string(&previous_elf_parser_output)?;

    let functions_to_check: Input = serde_json::from_str(&functions_to_check)?;
    let previous_elf_parser_output: Output = serde_json::from_str(&previous_elf_parser_output)?;

    let elf_raw_bytes = fs::read(&elf_path)?;
    let elf = Elf::parse(&elf_raw_bytes)?;

    let text_section = elf.section_headers.iter().find(|header| {
        if let Some(name) = elf.shdr_strtab.get_at(header.sh_name) {
            return name == ".text";
        }
        false
    }).expect("failed to find .text section of an elf");

    let functions_metadata: Vec<(String, elf::Sym)> = elf
        .syms
        .iter()
        .map(|sym| {
            if let Some(str_name) = elf.strtab.get_at(sym.st_name) {
                let str_name = String::from(str_name);

                for request in &functions_to_check.function_metadata {
                    if request.function == str_name {
                        return Some((str_name, sym));
                    }
                }
            }
            None
        })
        .filter_map(|x| x)
        .collect();

    assert_eq!(functions_metadata.len(), previous_elf_parser_output.function_metadata.len());

    for (str_name, sym) in functions_metadata {
        let func_addr = text_section.sh_offset + (sym.st_value - text_section.sh_addr);
        let func_size = sym.st_size;

        println!("reading function: {}, offset: {} size: {}", str_name, func_addr, func_size);

        let func_instructions = &elf_raw_bytes[func_addr as usize..(func_addr + func_size) as usize];

        let be_instructions: Vec<u32> = func_instructions
            .chunks(4)
            .map(|chunk| chunk.iter().fold(0, |acc, &b| (acc << 8) | b as u32))
            .collect();

        assert_eq!(find_previous_offset(&previous_elf_parser_output, &str_name).base.offset, sym.st_value as u32);
        assert_eq!(find_previous_offset(&previous_elf_parser_output, &str_name).instruction_count, be_instructions.len());
    }

    Ok(())
}

fn find_previous_offset<'a>(previous_elf_parser_output: &'a Output, name: &str) -> &'a FunctionInfo {
    previous_elf_parser_output.function_metadata.iter().find(|f| {
        return f.base.function == name;
    }).unwrap()
}