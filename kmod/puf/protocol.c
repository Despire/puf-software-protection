uint16_t consume_16bits_be(uint8_t *ptr, uint8_t *enrollment_data) {
    uint16_t timeout = 0x0;
    uint8_t first_byte, second_byte;

    first_byte = enrollment_data[*ptr];
    second_byte = enrollment_data[(*ptr) + 1];
    *ptr += 2;

    timeout = (((uint16_t) first_byte) << 8) | second_byte;
    return timeout;
}

uint32_t consume_32bits_be(uint8_t *ptr, uint8_t *enrollment_data) {
    uint32_t block_ptr = 
	(enrollment_data[*ptr] << 24) |
	(enrollment_data[(*ptr) + 1] << 16) |
	(enrollment_data[(*ptr) + 2] << 8)  |
	 enrollment_data[(*ptr) + 3];

    *ptr += 4;
    return block_ptr;
}

uint8_t consume_8bits_be(uint8_t *ptr, uint8_t *enrollment_data) {
    uint8_t parity = 0x0;

    parity = enrollment_data[*ptr];
    *ptr += 1;

    return parity;
}
