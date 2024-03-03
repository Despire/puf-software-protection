//use std::net::{TcpListener, TcpStream};
//use std::io::{Read, Write};
//
//fn handle_client(mut stream: TcpStream) {
//    let mut buffer = [0; 1024];
//    
//    loop {
//        match stream.read(&mut buffer) {
//            Ok(size) => {
//                if size == 0 {
//                    println!("Client disconnected");
//                    break;
//                }
//                
//                println!("received: {}", String::from_utf8_lossy(&buffer[..]));
//                // Echo back the received data
//                stream.write_all(&buffer[..size]).unwrap();
//            }
//            Err(_) => {
//                println!("Error reading from client");
//                break;
//            }
//        }
//    }
//}
//
//fn main() -> std::io::Result<()> {
//    let listener = TcpListener::bind("127.0.0.1:8080")?;
//    println!("Echo server listening on port 8080");
//
//    for stream in listener.incoming() {
//        match stream {
//            Ok(stream) => {
//                handle_client(stream);
//            }
//            Err(e) => {
//                eprintln!("Error accepting client: {}", e);
//            }
//        }
//    }
//
//    Ok(())
//}


fn main() {
    println!("");
}
