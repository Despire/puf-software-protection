use std::env;
use std::process::exit;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cfg = elf_parser::Config::new(&mut env::args()).unwrap_or_else(|err| {
        eprintln!("failed to parse command line arguments: {}", err);
        exit(1);
    });

    elf_parser::run(&cfg)
}
