
## CanBoot Protocol

### Frame

Each command and response is framed as follows:

```
<2 byte header> <1 byte command> <1 byte payload word length> <payload> <2 byte crc> <2 byte trailer>
|             | |                                                     |              |              |
-<0x01><0x88>-- ----------------- CRC Checked Data --------------------              --<0x99><0x03>--
```

- The header is <0x01><0x88>
- The trailer is <0x99><0x03>
- The payload length must be specified in 4 byte words.  A value of 1
  corresponds to a payload 4 bytes in length.
- The payload is optional depending on the command. If present it must be a
  multiple of 4 bytes in length.
- The CRC is performed on the command byte, length byte, and payload using
  the standard CRC16-CCITT algorithm.
- The CRC and all integer arguments within the payload are sent in little-endian
  byte order.

### Commands

The bootloader accepts the following commands:

#### Connect: `0x11`

Initiates communication with the bootloader.  This command has no payload:

```
<0x01><0x88><0x11><0x00><CRC><0x99><0x03>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 16 byte payload
in the following format:

```
<4 byte orig_command><4 byte protocol_version><4 byte start_address><4 byte block_size><n byte mcu_type_string>
```

- `orig_command` - must be `0x11`
- `protocol_version` - The current version of the protocol.  This is an integer
   value, where each of the 3 least significant bytes represent a part of the
   version string.  For example, a value of `0x00010002` represents `1.0.2`.
- `start_address` - The application start address in flash memory.  All addresses
  provided in the `send_block` and `request block` commands must start at this
  address.
- `block_size` - the size of a block (in bytes) expected in the `send block` and
  `request block` commands.  Typically this should be 64 bytes.
- `mcu_type_string` - The type of micro-controller (eg, "stm32f103xe").


#### Send Block: `0x12`

Sends a block of data to be written.

```
<0x01><0x88><0x12><1 byte payload word length><4 byte block_address><block_data><CRC><0x99><0x03>
```
The `payload word length` will include one word for the `block_address` argument
plus `block_size / 4` for the data.

The `block_address` refers to a address in flash memory where the block write
should begin.  The first `block_address` must be the `start_address` received
in the [connect](#connect-0x11) command.

The `block_data` is the data contained within the block.  If the final block
is less than `block_size` in length it should be padded with `0xFF` to fill
the remainder.

Responds with [acknowledged](#acknowledged-0xa0) containing an 8 byte payload
in the following format:

```
<4 byte orig_command><4 byte block_address>
```

- `orig_command`: Must be `0x12`
- `block_address`: Must match the `block_address` sent in the command

#### EOF: `0x13`

Indicates that the end of file has been reached and the bootloader should
write any remaining data in the buffer to flash.  This command has no payload:

```
<0x01><0x88><0x13><0x00><CRC><0x99><0x03>
```

Responds with [acknowledged](#acknowledged-0xa0) containing an 8 byte payload
in the following format:

```
<4 byte orig_command><4 byte page_count>
```

- `orig_command`: Must be `0x13`
- `page_count`: The total number of pages written to flash.

#### Request Block: `0x14`

Requests of block of data in flash, used for verification.

```
<0x01><0x88><0x14><0x01><4 byte block_address><CRC><0x99><0x03>
```

The `block_address` refers to a address in flash memory where the block read
should begin.  The first `block_address` must be the `start_address` received
in the [connect](#connect-0x11) command.

Responds with [acknowledged](#acknowledged-0xa0) containing a payload
in the following format:

```
<4 byte orig_command><4 byte block_address><block_data>
```

- `orig_command`: Must be `0x14`
- `block_address`: Must match the `block_address` sent in the command
- `block_data`: The requested block of data


#### Complete: `0x15`

Indicates that process is complete.  The bootloader will reset and attempt
to jump to the application after responding to this command:

```
<0x01><0x88><0x15><0x00><CRC><0x99><0x03>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 4 byte payload
in the following format:

```
<4 byte orig_command>
```

#### Get CANbus id: `0x16`

Return the CANbus UUID (for verification of correct communication
channel).

```
<0x01><0x88><0x16><0x00><CRC><0x99><0x03>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 12 byte
payload in the following format:

```
<4 byte orig_command><6 byte UUID><0x00><0x00>
```

### Responses

#### Acknowledged: `0xa0`

This response indicates successful execution of a command:

```
<0x01><0x88><0xa0><payload word length><payload><CRC><0x99><0x03>
```

The payload depends on the command.

#### NACK: `0xf1`

Indicates that the bootloader failed to receive a well formed command.
The sender should retry.

```
<0x01><0x88><0xf1><0x00><0x68><0x95><0x99><0x03>
```

#### Command Error: `0xf2`

Indicates that the bootloader encountered an error when processing
a command.

```
<0x01><0x88><0xf2><0x00><0x00><0xbf><0x99><0x03>
```
