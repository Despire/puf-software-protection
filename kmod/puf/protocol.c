// The process open the puf device
// 1. The puf devices registers the PID and read the Enrollment data within the .open() function.
// 2. The puf starts  immediately decaying with the first timeout and will continue until the last timeout.
// 3. After a timeouts finishes it will wait for it to be read before initiating the next timeout.
// 4. After all timeouts the PID will reset and the PUF will again wait for the next .open() function.
// 5. In steps between 2 and 4 the .open() function must reject all other PIDs.
//
// Specifies the PUF is in unused state.
// It completed all the decay requests from a program
// and waits for another program.
#define PUF_UNUSED   0x0
// Specifies the PUF is in a decay state.
// After the the requested decay timeout times outs
// the PUF will be back in IDLE state.
#define PUF_DECAYING 0x1
// Specifies the PUF was registerd with a certain PID, but
// is waiting for the enrollment data before starting.
#define PUF_WAITING_FOR_ENROLLMENT 0x2
// Specifies the PUF was successfully generated and is waiting
// to be read the the process.
#define PUF_WAITING_FOR_READ       0x3

#define ENROLLMENT_TIMEOUT_BYTES 2
#define ENROLLMENT_POINTER_BYTES 4

uint16_t consume_timeout_be(uint8_t *ptr, uint8_t *enrollment_data) {
    uint16_t timeout = 0x0;
    uint8_t first_byte, second_byte;

    first_byte = enrollment_data[*ptr];
    second_byte = enrollment_data[(*ptr) + 1];
    *ptr += ENROLLMENT_TIMEOUT_BYTES;

    timeout = ((uint16_t) second_byte) | first_byte;
    return timeout;
}

uint32_t consume_block_ptr_be(uint8_t *ptr, uint8_t *enrollment_data) {
    uint16_t block_ptr = 0x0;
    uint8_t first_byte, second_byte, third_byte, fourth_byte;

    first_byte  = enrollment_data[*ptr];
    second_byte = enrollment_data[(*ptr) + 1];
    third_byte  = enrollment_data[(*ptr) + 2];
    fourth_byte = enrollment_data[(*ptr) + 3];
    *ptr += ENROLLMENT_POINTER_BYTES;

    block_ptr = ((uint16_t) fourth_byte << 24) | (third_byte << 16) | (second_byte << 8) | first_byte;

    return block_ptr;
}
