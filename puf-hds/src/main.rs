use std::env;
use std::process;

use puf_hds;
use puf_hds::Config;

fn main() {
    let config = Config::new(env::args()).unwrap_or_else(|err| {
        eprintln!("Problem parsing arguments: {}", err);
        process::exit(1);
    });

    if let Err(err) = puf_hds::generate(&config) {
        eprintln!("Failed to generate hds: {}", err);
        process::exit(1);
    }
}
