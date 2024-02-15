use serde::{Serialize, Deserialize};
use crate::{Config, reed_solomon};

use rand::distributions::{Distribution, Uniform};
use rand::seq::SliceRandom;
use rand_chacha::ChaCha8Rng;
use rand::SeedableRng;

pub type DRAMCells = Vec<Vec<u64>>;

#[derive(Serialize, Deserialize)]
pub struct Enrollment {
    #[serde(rename = "decay_time")]
    decay_time: i32,
    #[serde(rename = "pointers")]
    pointers: Vec<u64>,
    #[serde(rename = "auth_value")]
    auth_value: u64,
    #[serde(rename = "parity")]
    parity: Vec<u16>,
}

pub fn prepare(cfg: &Config, dram_cells: Vec<(DRAMCells, DRAMCells, DRAMCells)>) -> Result<Vec<Enrollment>, Box<dyn std::error::Error>> {
    let custom_seed = [0u8; 32];
    let mut rng = ChaCha8Rng::from_seed(custom_seed);
    let u64_range = Uniform::new_inclusive(0, u64::MAX);

    dram_cells
        .iter()
        .enumerate()
        .map(|(i, cells)| {
            let decay_time = cfg.decay_config.get_measurement(i);
            let stable_1_cells = &cells.0;
            let stable_0_cells = &cells.2;

            let mut dram_1_pointers = stable_1_cells.iter().flatten().map(|v| *v).collect::<Vec<u64>>();
            dram_1_pointers.shuffle(&mut rng);

            let mut dram_0_pointers = stable_0_cells.iter().flatten().map(|v| *v).collect::<Vec<u64>>();
            dram_0_pointers.shuffle(&mut rng);

            let mut pointers = Vec::new();

            let auth_value = u64_range.sample(&mut rng);
            for shift in 0..64 {
                let bit = 1 << shift;
                if bit & auth_value != 0x0 {
                    pointers.push(dram_1_pointers.pop().unwrap());
                } else {
                    pointers.push(dram_0_pointers.pop().unwrap());
                }
            }

            let mut parity = vec![0u16; (64. * (cfg.enrollment.parity_percentage as f64 / 100.)) as usize];
            unsafe {
                let control_ptr = reed_solomon::init(&parity);
                if control_ptr.is_null() {
                    return Err("failed to initialize reed solomon encoder")?;
                }

                let mut data = auth_value.to_be_bytes();
                if reed_solomon::encode(control_ptr, &mut data, &mut parity) != 0 {
                    return Err("failed to encode data using reed solomon")?;
                }

                reed_solomon::free(control_ptr);
            }

            Ok(Enrollment {
                pointers,
                parity,
                auth_value,
                decay_time,
            })
        })
        .collect()
}