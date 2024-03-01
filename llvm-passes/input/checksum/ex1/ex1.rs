#[no_mangle]
fn hash5(mut start: usize, mut size: usize, c: u32) -> u32 {
    let mut h: u32 = 0;

    while size > 0 {
        let ptr = start as *const u8;
        unsafe {
            h = c * (*ptr as u32 + h);
        }
        size -= 1;
        start += 1;
    }

    h
}

fn main() {
    let ptr = hash5 as *const ();
    println!("{}", hash5(ptr as usize, 50, 3));
}