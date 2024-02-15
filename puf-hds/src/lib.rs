mod reed_solomon;
mod enroll;

use enroll::DRAMCells;

use std::{env, fs, io, u64};
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;
use crate::enroll::Enrollment;

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub enum Sizes { Byte, KB, MB }

impl Sizes {
    fn as_usize(&self) -> usize {
        match self {
            Sizes::Byte => 1,
            Sizes::KB => 1 << 10,
            Sizes::MB => 1 << 20,
        }
    }
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub enum WordSize { BW16, BW32 } // BW => Bus Width

impl WordSize {
    fn as_word_in_bytes(&self) -> usize {
        match self {
            WordSize::BW16 => 2,
            WordSize::BW32 => 4,
        }
    }
    fn as_word_in_bits(&self) -> usize { self.as_word_in_bytes() * 8 }

    fn max(&self) -> usize {
        match self {
            WordSize::BW16 => u16::MAX as usize,
            WordSize::BW32 => u32::MAX as usize
        }
    }

    fn from_be_bytes(&self, buff: &[u8]) -> usize {
        match self {
            WordSize::BW16 => {
                u16::from_be_bytes(buff.try_into().expect("Incorrect buffer size for u16")) as usize
            }
            WordSize::BW32 => {
                u32::from_be_bytes(buff.try_into().expect("Incorrect buffer size for u32")) as usize
            }
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
    pub size: usize,
    /// Unit of the size.
    /// (i.e if size = 10 and unit = MB, then PUF is considered to be 10MB)
    pub unit: Sizes,
    /// Page size in words of the DRAM component.
    /// (i.e. the BeagleBone Black rev 3C has 1024-word page)
    pub page_size_words: usize,
    /// bus width in bits of the SDRAM.
    /// (i.e. on the BeagleBone Black rev 3C its 16 bits)
    pub bus_width: WordSize,
}

impl PUFConfig {
    fn size_in_bytes(&self) -> usize { self.unit.as_usize() * self.size }
    fn blocks(&self) -> usize { self.size_in_bytes() / self.bus_width.as_word_in_bytes() }
    fn row_width(&self) -> usize { self.page_size_words * self.bus_width.as_word_in_bytes() }
    fn num_of_rows(&self) -> usize { self.size_in_bytes() / self.row_width() }
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct EnrollmentConfig {
    /// Name of the output file.
    pub name: String,
    /// Recover % of the original message
    pub parity_percentage: usize,
    /// This is a request from the user that can be later used when patching the binary.
    /// It indicates which timeouts in which order should be executed in the control flow
    /// of the patched binary.
    pub timeout_requests: Vec<u32>,
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct Config {
    /// Name of the board.
    pub name: String,
    /// Path to the measurements.
    /// It is expected that all the measurements are present in this specified path.
    pub path: String,
    /// Common prefix to the binary files in the specified path.
    /// i.e if the path is ./sets and the file in it have names BBB_10s.bin BBB_20s.bin
    /// the common prefix would be 'BBB'.
    pub common_prefix: String,
    /// Config for the enrollment helper structure.
    pub enrollment: EnrollmentConfig,
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

#[derive(serde::Serialize, serde::Deserialize, Debug)]
struct EnrollData {
    requests: Vec<u32>,
    enrollments: Vec<Enrollment>,
}

pub fn generate(cfg: &Config) -> Result<(), Box<dyn std::error::Error>> {
    let cells = extract_cells(cfg)?;

    for (i, measurement) in cells.iter().enumerate() {
        let stable_cells_count = measurement.0.iter().map(|row| row.len()).sum::<usize>();
        let unstable_cells_count = measurement.1.iter().map(|row| row.len()).sum::<usize>();
        let stable_0_cells_count = measurement.2.iter().map(|row| row.len()).sum::<usize>();
        println!("Stats related to \"Decay Timeout: {}s\"", cfg.decay_config.get_measurement(i));
        println!("\t stable_cells(common to all measurements, not present in previous decay timeout): {}", stable_cells_count);
        println!("\t unstable_cells(across all measurements, not present in previous decay timeout): {}", unstable_cells_count);
        println!("\t stable_0_cells(common to all measurements): {}", stable_0_cells_count);

        let enough_entropy = (stable_cells_count > 32) && (stable_0_cells_count > 32);
        println!("\t able to generate 32bit random value using DRAM cells: {}", if enough_entropy { "yes" } else { "no " });
        if !enough_entropy {
            return Err(format!("measurement {} has not enough stable cells to encode a 32bit value", cfg.decay_config.get_measurement(i)))?;
        }
    }

    let data = EnrollData {
        enrollments: enroll::prepare(&cfg, cells)?,
        requests: cfg.enrollment.timeout_requests.clone(),
    };

    let output_file = File::create(&cfg.enrollment.name)?;
    serde_json::to_writer_pretty(output_file, &data)?;

    Ok(())
}

fn measurements(cfg: &Config) -> Result<Vec<(Vec<File>, Option<Vec<File>>)>, Box<dyn std::error::Error>> {
    (0..cfg.decay_config.num_of_measurements as usize)
        .map(|measurement| {
            let timeout = cfg.decay_config.get_measurement(measurement);

            let current_measurements = collect_measurements(cfg, timeout)?;
            let previous_measurements = if measurement > 0 {
                collect_measurements(cfg, timeout - cfg.decay_config.incremental_step)?
            } else {
                Vec::new()
            };

            Ok((current_measurements, if previous_measurements.is_empty() { None } else { Some(previous_measurements) }))
        })
        .collect()
}

fn collect_next_word(measurements: Option<&mut [File]>, bus_width: &WordSize) -> Result<Vec<usize>, Box<dyn std::error::Error>> {
    match measurements {
        Some(files) => files
            .iter_mut()
            .map(|m| {
                let mut buff = vec![0u8; bus_width.as_word_in_bytes()];
                if m.read(&mut buff)? == 0 {
                    panic!("input json config misconfigured: over-reading file, Make sure the puf_config has valid parameters");
                }
                Ok(bus_width.from_be_bytes(&buff))
            }).collect(),
        None => Ok(Vec::new()),
    }
}

fn extract_cells(cfg: &Config) -> Result<Vec<(DRAMCells, DRAMCells, DRAMCells)>, Box<dyn std::error::Error>> {
    measurements(cfg)?
        .iter_mut()
        .map(|pair| {
            let mut stable_1_cells = vec![Vec::<u32>::new(); cfg.puf_config.num_of_rows()];
            let mut stable_0_cells = vec![Vec::<u32>::new(); cfg.puf_config.num_of_rows()];
            let mut unstable_cells = vec![Vec::<u32>::new(); cfg.puf_config.num_of_rows()];

            // Read all blocks from all of the files.
            for block in 0..cfg.puf_config.blocks() {
                let mut current_common_flips = cfg.puf_config.bus_width.max();
                let mut current_common_non_flips = cfg.puf_config.bus_width.max();
                let mut current_all_flips = 0x0;
                for word in collect_next_word(Some(&mut pair.0), &cfg.puf_config.bus_width)? {
                    current_common_flips &= word;
                    current_common_non_flips &= !word;
                    current_all_flips |= word;
                }

                let mut previous_all_flips = 0x0;
                for word in collect_next_word(pair.1.as_deref_mut(), &cfg.puf_config.bus_width)? {
                    previous_all_flips |= word;
                }

                let current_flips = current_common_flips & (!previous_all_flips);
                if current_flips != 0x0 {
                    (0..cfg.puf_config.bus_width.as_word_in_bits())
                        .filter(|shift| current_flips & (1 << shift) != 0x0)
                        .for_each(|shift| {
                            let ptr = (block as u32) << (cfg.puf_config.bus_width.as_word_in_bits().trailing_zeros()) | shift as u32;
                            let row_index = block / cfg.puf_config.page_size_words;
                            stable_1_cells[row_index].push(ptr);
                        });
                }

                if current_common_non_flips != 0x0 {
                    (0..cfg.puf_config.bus_width.as_word_in_bits())
                        .filter(|shift| current_common_non_flips & (1 << shift) != 0x0)
                        .for_each(|shift| {
                            let ptr = (block as u32) << (cfg.puf_config.bus_width.as_word_in_bits().trailing_zeros()) | shift as u32;
                            let row_index = block / cfg.puf_config.page_size_words;
                            stable_0_cells[row_index].push(ptr);
                        });
                }

                let unstable_flips = current_flips ^ (current_all_flips & (!previous_all_flips));
                if unstable_flips != 0x0 {
                    (0..cfg.puf_config.bus_width.as_word_in_bits())
                        .filter(|shift| unstable_flips & (1 << shift) != 0x0)
                        .for_each(|shift| {
                            let ptr = (block as u32) << (cfg.puf_config.bus_width.as_word_in_bits().trailing_zeros()) | shift as u32;
                            let row_index = block / cfg.puf_config.page_size_words;
                            unstable_cells[row_index].push(ptr);
                        })
                }
            }
            Ok((stable_1_cells, unstable_cells, stable_0_cells))
        }).collect()
}

fn collect_measurements(cfg: &Config, timeout: i32) -> Result<Vec<File>, Box<dyn std::error::Error>> {
    let mut current_measurements = Vec::new();

    for iter in 1..=cfg.decay_config.replication {
        let path = PathBuf::from(&cfg.path).join(format!("{}_{}_{}sec", cfg.common_prefix, iter, timeout));
        current_measurements.push(File::open(path)?);
    }

    Ok(current_measurements)
}