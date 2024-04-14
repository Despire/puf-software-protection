use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};

use hex;
use sha3::{Digest, Sha3_256};

use aes::cipher::{generic_array::GenericArray, BlockCipher, BlockDecrypt, BlockEncrypt, KeyInit};
use aes::Aes128;

#[no_mangle]
fn handle_client(mut stream: TcpStream) {
    let mut buffer = [0; 1024];

    loop {
        match stream.read(&mut buffer) {
            Ok(size) => {
                if size == 0 {
                    println!("Client disconnected");
                    break;
                }
                work2(&mut stream, &mut buffer[..]);
            }
            Err(_) => {
                println!("Error reading from client");
                break;
            }
        }
    }
}

fn main() -> std::io::Result<()> {
    std::thread::spawn(work1);

    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Echo server listening on port 8080");

    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                std::thread::spawn(|| handle_client(stream));
            }
            Err(e) => {
                eprintln!("Error accepting client: {}", e);
            }
        }
    }

    Ok(())
}

#[no_mangle]
fn work1() {
    loop {
        std::thread::sleep(std::time::Duration::from_secs(5));
        println!("working...");
    }
}

#[no_mangle]
fn work2(stream: &mut TcpStream, msg: &mut [u8]) {
    let key = GenericArray::from([8u8; 16]);
    let cipher = Aes128::new(&key);

    let mut hasher = Sha3_256::new();

    for chunk in &mut msg.chunks_mut(16) {
        let mut block = GenericArray::from_mut_slice(chunk);
        cipher.encrypt_block(&mut block);
        hasher.update(&block[..]);
    }

    let result = hasher.finalize();
    let hex_result = hex::encode(&result[..]);
    stream.write_all(hex_result.as_bytes()).unwrap();
}
