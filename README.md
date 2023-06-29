## ESP UART Bridge

ESP UART Bridge is an ESP32-based bridge which emulates the SC18IM704 protocol, so instead of using an SC18IM704 
chip as a UART to I2C Contoller (Master) and GPIO bridge, you can run this sketch on an ESP32 to have a UART 
brdige that functions similar to a SC18IM704 chip.

The test was done on an ESP32 but it could easily port to other MCU that has two UART ports, and with at least 8 
GPIO and one I2C interface.

## Differences from SC18IM704
Although the ESP UART Bridge emulates the SC18IM704 UART protocol, it does has some minor different behaviour from SC18IM704. 

### GPIO Pin Assignments
SC18IM704 allows a client to address GPIO pins with GPIO 0 to 7. On the ESP UART Bridge the GPIO numbers are mapped to the ESP GPIO pin number 2, 4, 5, 12, 13, 14, 18 and 19. In the case any of those pins is not available, you can change the settings of the follow line.

```
const uint8_t gpio_pins[] = {2, 4, 5, 12, 13, 14, 18, 19};
```

### I2C Status
The value of ESP UART Bridge returns I2C status based on the values return from `Wire.endTransmission()` which is different from the value return from SC18IM704 I2C status request.
```
 * Wire.endTransmission()               SC18IM704         Value
 *   0 - success                        I2C_OK            0xF0
 *   1 - data overflow transmit buffer  not supported
 *   2 - NACK on address                I2C_NACK_ON_ADDR  0xF1
 *   3 - NACK on data                   I2C_NACK_ON_DATA  0xF2
 *   4 - other error                    not supported
 *   5 - timeout                        I2C_TIME_OUT      0xF8
```
In the case where the status code is not available in SC18IM704, the status from the `Wire` Arduino library will be return (e.g. status 1 and status 4).

### Write to Register
Unlike the SC18IM704 where one could write to any one of the 11 registers, ESP UART only allows for writing to 6 registers for practical reasons at current implementation. Those are the two registers for baud rate configuration, two registers for GPIO port configurations and registers for configuring I2C clock speed. Writing to any other registers has no effect.

### Power Down function
The power down function is currently not supported on ESP UART Bridge.

### Return string of Read ID function
When sending the command for read ID, the SC18IM704 will return a 16-character NULL-terminated string as "SC18IM704 1.0.1", the string returned by ESP UART Bridge is also 16-character with NULL terminator but as "U-Bridge v1.0.0". 

### Supported UART Baud Rate
As a design decision, the ESP UART Baidge only support one of the 10 baud rates, 9600, 14400, 19200, 28800, 38400, 57600, 76800, 115200, 230400, 460800. Any baud rate other than those would be rejected and has no effect.

## References

* [SC18IM704 datasheet](https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf)
* [SC18IM704-EVB Evaluation Board User Manual](https://www.nxp.com/docs/en/user-manual/UM11664.pdf)

