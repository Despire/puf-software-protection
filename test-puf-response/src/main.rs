use std::env;
use std::process::exit;

fn main() {
    let cfg = test_puf_response::Config::new(&mut env::args()).unwrap_or_else(|err| {
        eprintln!("failed to parse command line arguments: {}", err);
        exit(1);
    });

    if !cfg.verify() {
        eprintln!("failed to verify the PUF response with the generated HDS scheme.");
        exit(1);
    }
}
