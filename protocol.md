
## CanBoot Protocol

### Frame

Each command and response is framed as follows:

```
<2 byte header> <1 byte command> <1 byte payload word length> <payload> <2 byte trailer> <2 byte crc>
```

- The header is <0x01><0x88>
- The trailer is <0x99><0x03>
- The payload length must be specified in 4 byte words.  A value of 1
  corresponds to a payload 4 bytes in length.
- The payload is optional depending on the command. If present it must be a
  multiple of 4 bytes in length.
- Any arguments within the payload should be sent in big endian byte order.
- The CRC is performed on the entire frame (header through trailer) using
  the standard CRC16-CCITT algorithm.
- The CRC and all integer arguments are sent in big endian byte order.

### Commands

The bootloader accepts the following commands:

#### Connect: `0x11`

Initiates communication with the bootloader.  This command has no payload:

```
<0x01><0x88><0x11><0x00><0x99><0x03><CRC>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 4 byte payload
in the following format:

```
<0x11><0x00><2 byte block_size>
```

The `block_size` is a 16 bit unsigned integer in big endian byte order.  It
represents the size of a block (in bytes) expected in the `send block` and
`request block` commands.  Typically this should be 64 bytes.

#### Send Block: `0x12`

Sends a block of data to be written.  This command takes a `block index`
argument followed by the block of data:

```
<0x01><0x88><0x12><1 byte payload word length><4 byte block_index><block_data><0x99><0x03><CRC>
```

The `payload word length` will include one word for the block index argument
plus `block_size // 4` for the data.

The `block_index` refers to a single block within the binary of `block_size`
bytes, starting at 0.  It should be sent as 32-bit integer in big endian format.

The `block_data` is the data contained with in the block.  If the final block
is less than `block_size` in length it should be padded with `0xFF` to fill
the remainder.

Responds with [acknowledged](#acknowledged-0xa0) containing a 4 byte payload
in the following format:

```
<0x12><0x00><2 byte block_index>
```

The returned `block_index` is a 16-bit unsigned integer in big endian byte
order. It must match the `block_index` sent in the request.

#### EOF: `0x13`

Indicates that the end of file has been reached and the bootloader should
write any remaining in the buffer to flash.  This command has no payload:

```
<0x01><0x88><0x13><0x00><0x99><0x03><CRC>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 4 byte payload
in the following format:

```
<0x13><0x00><2 byte page_count>
```

The `page_count` is a 16-bit unsigned integer in big endian byte order.  It
represents the total number of pages written to flash.

#### Request Block: `0x14`

Requests of block of data in flash, used for verification.  This command takes
a `block index` argument indicating the block to read:

```
<0x01><0x88><0x14><0x01><4 byte block_index><0x99><0x03><CRC>
```

The `block_index` is a 32-bit unsigned integer in big endian byte order.

Responds with [acknowledged](#acknowledged-0xa0) containing a payload
in the following format:

```
<0x14><0x00><2 byte block_index><block_data>
```

The returned `block_index` is a 16-bit unsigned integer in big endian byte
order. It must match the `block_index` sent in the request.

#### Complete: `0x15`

Indicates that process is complete.  The bootloader will reset and attempt
to jump to the application after responding to this command:

```
<0x01><0x88><0x15><0x00><0x99><0x03><CRC>
```

Responds with [acknowledged](#acknowledged-0xa0) containing a 4 byte payload
in the following format:

```
<0x15><0x00><0x00><0x00>
```

### Responses

All responses contain a payload of at least 4 bytes, where the first
byte contains the command being responded to.  It is possible for
a response to include another argument within this first word.

#### Acknowledged: `0xa0`

This response indicate successful execution of a command:

```
<0x01><0x88><0xa0><payload word length><payload><0x99><0x03><CRC>
```

The payload depends on the command.

#### Invalid Command: `0xf0`

Indicates the the bootloader received an invalid command:

```
<0x01><0x88><0xf0><0x01><4 byte payload><0x99><0x03><CRC>
```

The payload is in the following format:

```
<1 byte orig_command><0x00><0x00><0x00>
```

The `orig_command` is the invalid command received.

#### CRC Mismatch: `0xf1`

Indicates that CRC verification failed on the requested command:

```
<0x01><0x88><0xf1><0x01><4 byte payload><0x99><0x03><CRC>
```

The payload is in the following format:

```
<1 byte orig_command><0x00><2 byte calculated_crc>
```

The `command` is the command received from the original request.
The `calculated_crc` is a 16-bit unsigned integer represented
the CRC calculated on the frame.

#### Invalid Block: `0xf2`

Indicates that the payload contained in a [send block](#send-block-0x12)
command is not of the expected lenght:

```
<0x01><0x88><0xf2><0x01><4 byte payload><0x99><0x03><CRC>
```

The payload is in the following format:

```
<1 byte orig_command><0x00><0x00><0x00>
```

The `orig_command` is the command received which generated

#### Invalid Trailer: `0xf3`

Indicates that the trailer of the frame is not `<0x88><0x03>`:

```
<0x01><0x88><0xf3><0x01><4 byte payload><0x99><0x03><CRC>
```

The payload is in the following format:

```
<1 byte orig_command><0x00><0x00><0x00>
```

The `orig_command` is the command received from the original request.

#### Invalid Length: `0xf4`

Indicates that the payload length of a request is too large
to fit within the buffer:

```
<0x01><0x88><0xf4><0x01><4 byte payload><0x99><0x03><CRC>
```

The payload is in the following format:

```
<1 byte orig_command><0x00><0x00><0x00>
```

The `orig_command` is the command received from the original request.
