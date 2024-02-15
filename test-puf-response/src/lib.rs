//!
//! This module verifies if given a raw PUF response from the respective
//! IoT device will indeed be a valid PUF response given the generated HDS
//! scheme.
use std::env;
use std::fs::{File, read_to_string};
use std::fmt;
use std::io::Read;
use std::mem::size_of;
use serde::{Deserialize, Serialize};

#[derive(Debug)]
struct ArgError {
    details: String,
}

impl ArgError {
    fn new(msg: &str) -> ArgError {
        ArgError { details: msg.to_string() }
    }
}

impl fmt::Display for ArgError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.details)
    }
}

impl std::error::Error for ArgError {
    fn description(&self) -> &str {
        &self.details
    }
}

pub struct Config {
    // enrollments that were generated.
    pub enrollments: String,
    // raw_puf_response acquired from the given IoT device.
    pub raw_puf_response: Vec<u8>,
    // decay_time is the number of seconds the raw_puf_response
    // decayed for until retrieved the random bits.
    pub decay_time: usize,
}

#[derive(Serialize, Deserialize)]
pub struct Enrollment {
    #[serde(rename = "decay_time")]
    decay_time: i32,
    #[serde(rename = "pointers")]
    pointers: Vec<u64>,
    #[serde(rename = "auth_value")]
    auth_value: u32,
    #[serde(rename = "parity")]
    parity: Vec<u16>,
}

#[derive(Serialize, Deserialize)]
pub struct EnrollData {
    enrollments: Vec<Enrollment>,
    requests: Vec<u32>,
}

impl Config {
    pub fn new(args: &mut env::Args) -> Result<Config, Box<dyn std::error::Error>> {
        let mut program_args = args.skip(1).take(3);

        let resp = match program_args.next() {
            Some(resp) => {
                let mut bytes = Vec::new();
                File::open(resp)?.read_to_end(&mut bytes)?;
                bytes
            }
            None => return Err(ArgError::new("1st argument to the program should be the raw PUF response."))?
        };

        let scheme = match program_args.next() {
            Some(scheme) => read_to_string(scheme)?,
            None => return Err(ArgError::new("2nd argument to the program should be the generated enrollments."))?
        };

        let decay_time = match program_args.next() {
            Some(time) => time.parse::<usize>()?,
            None => return Err(ArgError::new("3rd argument to the program should be the decay time of the PUF response(the 1st argument)."))?
        };

        Ok(Config {
            raw_puf_response: resp,
            enrollments: scheme,
            decay_time,
        })
    }

    pub fn verify(&self) -> bool {
        let data: EnrollData = serde_json::from_str(&self.enrollments).expect("failed to parse enrollments");

        for e in data.enrollments {
            if e.decay_time != self.decay_time as i32 {
                continue;
            }

            let mut reconstructed: u32 = 0x0;

            for (i, ptr) in e.pointers.iter().enumerate() {
                // expect 2byte blocks were used.
                let block = ptr >> 4;
                let mask = ptr & 0xf;

                let offset = block as usize * size_of::<u16>();
                let puf_value_at_block = u16::from_be_bytes([
                    self.raw_puf_response[offset],
                    self.raw_puf_response[offset + 1]
                ]);

                let bit = puf_value_at_block & (1 << mask);
                if bit != 0 {
                    reconstructed |= 1 << i as u32;
                }
            }

            // TODO: add ecc using parity values.

            if reconstructed != e.auth_value {
                return false;
            }
        }

        println!("ok");
        true
    }
}