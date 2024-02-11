use libc::{c_int, uint8_t, uint16_t};
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
    alpha_to: *mut uint16_t,
    /// @index_of: Antilog lookup table
    index_of: *mut uint16_t,
    /// @genpoly: Generator polynomial
    genpoly: *mut uint16_t,
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
    fn encode_rs8(rs: *mut C_RsControl, data: *mut uint8_t, len: c_int, par: *mut uint16_t, invmsk: uint16_t) -> c_int;
}