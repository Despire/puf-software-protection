use libc::c_int;
use std::ffi::c_void;

#[repr(C)]
#[derive(Debug)]
pub struct ListHead {
    next: *mut c_void,
    prev: *mut c_void,
}

#[repr(C)]
#[derive(Debug)]
/// C memory layout compatible Reed Solomon control structure
pub struct C_RsControl {
    /// @mm: Bits per symbol
    mm: c_int,
    /// @nn: Symbols per block (= (1<<mm)-1)
    nn: c_int,
    /// @alpha_to: log lookup table
    alpha_to: *mut u16,
    /// @index_of: Antilog lookup table
    index_of: *mut u16,
    /// @genpoly: Generator polynomial
    genpoly: *mut u16,
    /// @nroots: Number of generator roots = number of parity symbols
    nroots: c_int,
    /// @fcr: First consecutive root, index form
    fcr: c_int,
    /// @prim: Primitive element, index form
    prim: c_int,
    /// @iprim:	prim-th root of 1, index form
    iprim: c_int,
    /// @gfpoly: The primitive generator polynominal
    gfpoly: c_int,
    /// @gffunc: Function to generate the field, if non-canonical representation
    gffunc: Option<unsafe extern "C" fn(c_int) -> c_int>,
    /// @users:	Users of this structure
    users: c_int,
    /// @list: List entry for the rs control list
    list: ListHead,
}

extern "C" {
    fn init_rs(symsize: c_int, gfpoly: c_int, fcr: c_int, prim: c_int, nroots: c_int) -> *mut C_RsControl;
    fn encode_rs8(rs: *mut C_RsControl, data: *mut u8, len: c_int, par: *mut u16, invmsk: u16) -> c_int;
    fn free_rs(rs_control: *mut C_RsControl);
}

pub fn init(parity: &[u16]) -> *mut C_RsControl {
    unsafe {
        return init_rs(8, 0x11d, 0, 1, parity.len() as c_int);
    }
}

pub fn encode(rs: *mut C_RsControl, data: &mut [u8], parity: &mut [u16]) -> i32 {
    unsafe {
        let ok = encode_rs8(rs, data.as_mut_ptr(), data.len() as c_int, parity.as_mut_ptr(), 0x0);
        return ok as i32;
    }
}

pub fn free(rs: *mut C_RsControl) {
    unsafe {
        free_rs(rs);
    }
}
