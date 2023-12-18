//!
//! This module verifies if given a raw PUF response from the respective
//! IoT device will indeed be a valid PUF response given the generated HDS
//! scheme.
use std::env;
use std::fs::{File, read_to_string};
use std::fmt;
use std::io::Read;
use std::mem::size_of;

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
    // hds_scheme that was generated using puf-hds program.
    pub hds_scheme: String,
    // raw_puf_response acquired from the given IoT device.
    pub raw_puf_response: Vec<u8>,
    // decay_time is the number of seconds the raw_puf_response
    // decayed for until retrieved the random bits.
    pub decay_time: usize,
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
            None => return Err(ArgError::new("2nd argument to the program should be the generated HDS scheme."))?
        };

        let decay_time = match program_args.next() {
            Some(time) => time.parse::<usize>()?,
            None => return Err(ArgError::new("3rd argument to the program should be the decay time of the PUF response(the 1st argument)."))?
        };

        Ok(Config {
            raw_puf_response: resp,
            hds_scheme: scheme,
            decay_time,
        })
    }

    pub fn verify(&self) -> bool {
        for line in self.hds_scheme.lines() {
            let mut ecc: [u32; 3] = [0x0, 0x0, 0x0];

            let pointers = line.split_ascii_whitespace().map(|i| i.parse::<u32>().unwrap()).collect::<Vec<u32>>();

            let target = pointers[1];
            let timing = pointers[0];

            if timing.abs_diff(self.decay_time as u32) > 5 {
                continue;
            }

            for (i, ptr) in pointers.iter().skip(2).enumerate() {
                let block = ptr >> 5;
                let mask = ptr & 0x1f;

                let offset = block as usize * size_of::<u32>();
                let puf_value_at_block = u32::from_be_bytes([
                    self.raw_puf_response[offset],
                    self.raw_puf_response[offset + 1],
                    self.raw_puf_response[offset + 2],
                    self.raw_puf_response[offset + 3]
                ]);

                let bit = puf_value_at_block & (1 << mask);
                if bit != 0x0 {
                    ecc[i / 32] = ecc[i / 32] | (1 << i as u32 % 32u32);
                }
            }

            let ecc_result = (ecc[0] | ecc[1] | ecc[2]) ^
                (ecc[0] & ecc[1] & ecc[2]) ^
                (ecc[0] ^ ecc[1] ^ ecc[2]);

            println!("ecc: {}", ecc_result);
            if target != ecc_result {
                return false;
            }
        }

        println!("ok");
        true
    }
}