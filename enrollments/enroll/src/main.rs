use std::env;
use std::process;

use enroll::Config;

fn main() {
    let config = Config::new(env::args()).unwrap_or_else(|err| {
        eprintln!("Problem parsing arguments: {}", err);
        process::exit(1);
    });

    if let Err(err) = enroll::generate(&config) {
        eprintln!("Failed to generate hds: {}", err);
        process::exit(1);
    }
}
