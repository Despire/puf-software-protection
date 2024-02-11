mod reed_solomon;

use std::{env, fs, io};
use std::collections::HashSet;
use std::fs::File;
use std::io::{Read, Write};
use std::path::PathBuf;

use rand::distributions::{Distribution, Uniform};
use rand::prelude::IteratorRandom;
use rand::seq::SliceRandom;
use rand_seeder::Seeder;
use rand_pcg::Pcg64;

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub enum Sizes { Byte, KB, MB }

impl Sizes {
    fn as_i32(&self) -> i32 {
        match self {
            Sizes::Byte => 1,
            Sizes::KB => 1 << 10,
            Sizes::MB => 1 << 20,
        }
    }
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct DecayConfig {
    /// Lowest timeout of the datasets.
    /// (i.e. 10s, 20s, 30s, lowest would be 10)
    pub decay_time_start: i32,
    /// Incremental step between consecutive measurements.
    /// (i.e. 10s, 20s, 30s, has an incremental step of 10s)
    pub incremental_step: i32,
    /// Number of measurements (i.e. 10s, 20s, 30s equals 3 measurements)
    pub num_of_measurements: i32,
    /// Number of measurements taken for the same timeout.
    /// (i.e 10s, 20s, 30s, 10s, 20s, 30s, would be 2)
    pub replication: i32,
}

impl DecayConfig {
    fn get_measurement(&self, i: usize) -> i32 {
        self.decay_time_start + (i * self.incremental_step as usize) as i32
    }
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct PUFConfig {
    /// Size of the PUF in the measurements.
    pub size: i32,
    /// Unit of the size.
    /// (i.e if size = 10 and unit = MB, then PUF is considered to be 10MB)
    pub unit: Sizes,
    /// Page size in bytes of the DRAM component.
    /// (i.e. on the BeagleBone Black rev 3C its 512 bytes)
    pub page_size_bytes: i32,
    /// Word size in bytes of the CPU.
    /// (i.e. on the BeagleBone Black rev 3C its 4 bytes)
    pub word_size_bytes: usize,
}

impl PUFConfig {
    fn size_in_bytes(&self) -> i32 { self.unit.as_i32() * self.size }
    fn blocks(&self) -> u32 { self.size_in_bytes() as u32 / self.word_size_bytes as u32 }
    fn num_of_rows(&self) -> u32 { self.size_in_bytes() as u32 / (self.page_size_bytes as usize * self.word_size_bytes) as u32 }
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct HDSConfig {
    /// Name of the output file.
    pub name: String,
    /// Recover % of the original message
    pub parity_percentage: i32,
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct Config {
    /// Name of the board.
    pub name: String,
    /// Path to the measurements.
    /// It is expected that all of the measurements are present in this specified path.
    pub path: String,
    /// Common prefix to the binary files in the specified path.
    /// i.e if the path is ./sets and the file in it have names BBB_10s.bin BBB_20s.bin
    /// the common prefix would be 'BBB'.
    pub common_prefix: String,
    /// Config for the generated HDS.
    pub hds_config: HDSConfig,
    /// Config for the PUF used in the measurements.
    pub puf_config: PUFConfig,
    /// Config for performed decay timeout measurements.
    pub decay_config: DecayConfig,
}

impl Config {
    pub fn new(mut args: env::Args) -> Result<Config, Box<dyn std::error::Error>> {
        let input = args.nth(1).unwrap_or_else(|| {
            println!("no path to config was supplied reading from stdin");
            String::new()
        });

        if !input.is_empty() {
            let cfg = fs::read_to_string(input)?;
            return Ok(serde_json::from_str(&cfg)?);
        }

        let mut input = String::new();
        io::stdin().read_to_string(&mut input)?;

        match serde_json::from_str::<Config>(&input) {
            Ok(cfg) => Ok(cfg),
            Err(err) => {
                if err.is_syntax() {
                    Err("input is not a valid JSON")?
                } else {
                    Err(err)?
                }
            }
        }
    }
}

pub fn generate(cfg: &Config) -> Result<(), Box<dyn std::error::Error>> {
    let stable_cells = extract_stable_cells(cfg)?;
    // let (hds, gen_values) = build_hds(cfg, &stable_cells)?;

    // let mut output_file = File::create(&cfg.hds_config.name)?;
    // for measurement in 0..(cfg.decay_config.num_of_measurements - 1) as usize {
    //     write_hds(&mut output_file, cfg, measurement, &gen_values, &hds)?;
    // }

    Ok(())
}

// fn write_hds(
//     output_file: &mut File,
//     cfg: &Config,
//     measurement: usize,
//     gen_values: &[i32],
//     hds: &[Vec<u64>],
// ) -> Result<(), Box<dyn std::error::Error>> {
//     let timeout = cfg.decay_config.get_measurement(measurement) + cfg.decay_config.incremental_step / 2;
//     write!(output_file, "{} {} ", timeout.to_string(), if measurement >= gen_values.len() { 0 } else { gen_values[measurement] })?;
//     for i in 0..(32 * cfg.hds_config.encoding_repetition - 1) as usize {
//         write!(output_file, "{} ", hds[measurement][i])?;
//     }
//     write!(output_file, "{}\n", hds[measurement][(cfg.hds_config.encoding_repetition - 1) as usize])?;
//     Ok(())
// }

// fn build_hds(cfg: &Config, stable_cells: &Vec<Vec<Vec<u64>>>) -> Result<(Vec<Vec<u64>>, Vec<i32>), Box<dyn std::error::Error>> {
//     let mut rng: Pcg64 = Seeder::from("test").make_rng();
//     let u32_range = Uniform::from(0..i32::MAX);
//
//     let mut helper_data_system = vec![
//         vec![0u64; cfg.hds_config.encoding_repetition as usize * 32];
//         cfg.decay_config.num_of_measurements as usize];
//
//     let mut rows: HashSet<u32> = (0..cfg.puf_config.num_of_rows()).collect();
//     let mut generated_values = Vec::new();
//
//     for measurement in 0..(cfg.decay_config.num_of_measurements - 2) as usize {
//         generated_values.push(u32_range.sample(&mut rng));
//
//         for rep in 0..cfg.hds_config.encoding_repetition as usize {
//             for shift in 0..32 {
//                 let mask: u32 = 1 << shift;
//
//                 if *generated_values.last().unwrap() as u32 & mask != 0x0 {
//                     let mut row: u32;
//                     loop {
//                         row = *rows.iter().choose(&mut rng).unwrap();
//                         if !stable_cells[measurement][row as usize].is_empty() {
//                             break;
//                         }
//                     }
//                     rows.remove(&row);
//                     helper_data_system[measurement][shift + rep * 32] = *stable_cells[measurement][row as usize].choose(&mut rng).unwrap();
//                 } else {
//                     let mut row: u32;
//                     loop {
//                         row = *rows.iter().choose(&mut rng).unwrap();
//                         if !stable_cells[measurement + 2][row as usize].is_empty() {
//                             break;
//                         }
//                     }
//                     rows.remove(&row);
//                     helper_data_system[measurement][shift + rep * 32] = *stable_cells[measurement + 2][row as usize].choose(&mut rng).unwrap();
//                 }
//             }
//         }
//     }
//
//     Ok((helper_data_system, generated_values))
// }

fn zip_measurements(cfg: &Config) -> Result<Vec<(Vec<File>, Option<Vec<File>>)>, Box<dyn std::error::Error>> {
    (0..cfg.decay_config.num_of_measurements as usize).map(|measurement| {
        let timeout = cfg.decay_config.get_measurement(measurement);

        let current_measurements = collect_measurements(cfg, timeout)?;
        let previous_measurements = if measurement > 0 {
            collect_measurements(cfg, timeout - cfg.decay_config.incremental_step)?
        } else {
            Vec::new()
        };

        Ok((current_measurements, if previous_measurements.is_empty() { None } else { Some(previous_measurements) }))
    }).collect()
}

fn collect_next_word(measurements: Option<&mut [File]>, word_size: usize) -> io::Result<Vec<u32>> {
    match measurements {
        Some(files) => files.iter_mut().map(|m| {
            let mut buff = Vec::with_capacity(word_size);
            m.take(word_size as u64).read_to_end(&mut buff)?;
            u32::from_be_bytes(buff.as_slice().try_into().unwrap())
        }).collect(),
        None => Ok(Vec::new()),
    }
}

fn extract_stable_cells_2(cfg: &Config) -> Result<Vec<Vec<Vec<u64>>>, Box<dyn std::error::Error>> {
    let mut stable_cells = vec![
        vec![Vec::<u64>::new(); cfg.puf_config.num_of_rows() as usize];
        cfg.decay_config.num_of_measurements as usize];

    for mut measurement_pair in zip_measurements(cfg)? {
        // Read all blocks from all of the files.
        for block in 0..cfg.puf_config.blocks() {
            let mut current_common_flips: u32 = 0xFFFFFFFF;
            for word in collect_next_word(Some(&mut measurement_pair.0), cfg.puf_config.word_size_bytes) {
                // this will take the original mask 0xFFFFFFFF and AND it with first measurement
                // then the resulting bits are AND with the second measurements and this will
                // give us stable bit flips found in both measurements.
                current_common_flips = current_common_flips & word;
            }

            let mut previous_all_flips: u32 = 0x0;
            for word in collect_next_word(measurement_pair.1.as_deref_mut(), cfg.puf_config.word_size_bytes) {
                // this will take the bit flips from the first measurements and then
                // from the second measurements and we end with the bit flips from all
                // previous measurements. (Note we don't care about the common bit flips from
                // the previous measurements.)
                previous_all_flips = previous_all_flips | word;
            }

            // flips only in current decay run.
            let current_flips = current_common_flips & (!previous_all_flips);
            if current_flips != 0x0 {
                panic!("implement me! (refactoring below)")
            }
        }
    }

    Ok(Vec::new())
}

fn extract_stable_cells(cfg: &Config) -> Result<Vec<Vec<Vec<u64>>>, Box<dyn std::error::Error>> {
    let mut stable_cells = vec![
        vec![Vec::<u64>::new(); cfg.puf_config.num_of_rows() as usize];
        cfg.decay_config.num_of_measurements as usize];

    for measurement in 0..cfg.decay_config.num_of_measurements as usize {
        let timeout = cfg.decay_config.get_measurement(measurement);
        let mut current_measurements = collect_measurements(cfg, timeout)?;

        let previous_measurements = if measurement > 0 {
            let previous_timeout = timeout - cfg.decay_config.incremental_step;
            collect_measurements(cfg, previous_timeout)?
        } else {
            Vec::new()
        };

        for block in 0..cfg.puf_config.blocks() {
            let mut current_common_bit_flips = 0xFFFFFFFF;
            for (i, m) in current_measurements.iter_mut().enumerate() {
                let mut buff = Vec::with_capacity(cfg.puf_config.word_size_bytes);
                m.take(cfg.puf_config.word_size_bytes as u64).read_to_end(&mut buff)?;
                let word = u32::from_be_bytes(buff.as_slice().try_into().unwrap());

                // this will take the original mask 0xFFFFFFFF and AND it with first measurement
                // then the resulting bits are AND with the second measurements and this will
                // give us stable bit flips found in both measurements.
                current_common_bit_flips = current_common_bit_flips & word;
            }

            let mut previous_bit_flips = 0x0;
            for m in &previous_measurements {
                let mut buff = Vec::with_capacity(cfg.puf_config.word_size_bytes);
                m.take(cfg.puf_config.word_size_bytes as u64).read_to_end(&mut buff)?;
                let word = u32::from_be_bytes(buff.as_slice().try_into().unwrap());

                // this will take the bit flips from the first measurements and then
                // from the second measurements and we end with the bit flips from all
                // previous measurements. (Note we don't care about the common bit flips from
                // the previous measurements.)
                previous_bit_flips = previous_bit_flips | word;
            }

            // Only bit flips for the current measurement that are not present in the
            // previous measurements.
            let only_current_bit_flips = current_common_bit_flips & (!previous_bit_flips);
            if only_current_bit_flips != 0x0 {
                for shift in 0..32 {
                    let mask = 1 << shift;
                    if only_current_bit_flips & mask != 0x0 {
                        // pointer to the block from the puf that has bit flips.
                        let pointer = (block as u64) << 5 | shift as u64;
                        // first remove the mask so we only end up with the block index.
                        // to get the row index just device by the page_size.
                        // since the puf size is a multiple of the page_size the division
                        // will be an integer.
                        // Why the page_size ? Imagine you have a PUF of size 2MB.
                        // 524_288 blocks. You can interpret these blocks as a matrix with
                        // rows and columns. Remember that PUF is a multiple of page_size
                        // So there are page_size number of elements in a single row.
                        // thus the division of page_size will give us the index of the row.
                        let row_index = (pointer >> 5) >> cfg.puf_config.page_size_bytes.trailing_zeros();

                        stable_cells[measurement][row_index as usize].push(pointer);
                    }
                }
            }
        }
    }

    Ok(stable_cells)
}

fn collect_measurements(cfg: &Config, timeout: i32) -> Result<Vec<File>, Box<dyn std::error::Error>> {
    let mut current_measurements = Vec::new();

    for iter in 1..=cfg.decay_config.replication {
        let path = PathBuf::from(&cfg.path).join(format!("{}_{}_{}sec", cfg.common_prefix, iter, timeout));
        current_measurements.push(File::open(path)?);
    }

    Ok(current_measurements)
}
