use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};
use hex_literal::hex;
use sha3::{Digest, Sha3_256};

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
                
                println!("received: {}", String::from_utf8_lossy(&buffer[..]));
                // Echo back the sha3 sum of the received data
                let mut hasher = Sha3_256::new();
                hasher.update(&buffer[..size]);
                let result = hasher.finalize();
                stream.write_all(&result[..]).unwrap();
            }
            Err(_) => {
                println!("Error reading from client");
                break;
            }
        }
    }
}

fn main() -> std::io::Result<()> {
    std::thread::spawn(test);

    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Echo server listening on port 8080");

    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                handle_client(stream);
            }
            Err(e) => {
                eprintln!("Error accepting client: {}", e);
            }
        }
    }

    Ok(())
}

#[no_mangle]
fn test() {
    loop {
        std::thread::sleep(std::time::Duration::from_secs(5));
        println!("Doing somethins...");
    }

}
