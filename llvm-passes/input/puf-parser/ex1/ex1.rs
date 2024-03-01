fn main() {
    println!("{:?}", std::fs::File::open("/dev/null").unwrap());
}