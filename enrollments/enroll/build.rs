extern crate cc;

fn main() {
    cc::Build::new()
        .file("ecc/reed_solomon.c")
        .compile("rslib.o");
}
