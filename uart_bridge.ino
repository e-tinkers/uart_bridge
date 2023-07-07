#include <Wire.h>

/*
 * This is a UART Bridge (between UART and I2C/GPIO) based on SC18IM704 UART protocol.
 * It emulate a SC18IM704 chip as a UART bridge and pass the UART commands recevied to
 * I2C or GPIO peripherials.
 * 
 * reference: https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf
 *            https://www.nxp.com/docs/en/user-manual/UM11664.pdf
 *            
 * author:    Henry Cheung
 * date:      7 June, 2023
 * 
 * TO-DO: Adding support for SPI communication
 */

// ----------------- Configuration Parameters ------------
#define LOG           Serial
#define bridge        Serial2 

#define BUFFER_SIZE   64
#define I2C_READ_BIT  0x01

// ----------------- Simple Logging System ---------------
#if defined(LOG)
#define log_print(...) LOG.print(__VA_ARGS__)
#define log_println(...) LOG.println(__VA_ARGS__)
#define log_printf(...) LOG.printf(__VA_ARGS__)
#else
#define log_print(...)((void) 0)
#define log_println(...)((void) 0)
#define log_printf(...)((void) 0)
#endif

// ----------------- MACROS based on SC18IM704 -----------
// UART Commands
#define CMD_START         'S' // 0x53
#define CMD_STOP          'P' // 0x50
#define CMD_REG_READ      'R' // 0x52
#define CMD_REG_WRITE     'W' // 0x57
#define CMD_GPIO_READ     'I' // 0x49
#define CMD_GPIO_WRITE    'O' // 0x4f
#define CMD_POWER_DOWN    'Z' // 0x5A
#define CMD_READ_ID       'V' // 0x56

// SC18IM704 Registers
#define REG_BAUD_RATE_0    0  // default 0xF0 i.e. Baud rate = 7372800 /(16 + 0x02f0) = 9600
#define REG_BAUD_RATE_1    1  // default 0x02
#define REG_PORT_CONF1     2  // default B01010101 (all as input)
#define REG_PORT_CONF2     3  // default B01010101 (all as input)
#define REG_IO_STATE       4
#define REG_RESERVED       5
#define REG_I2C_BUS_ADDR   6  // default 0x26
#define REG_I2C_CLK_L      7  // default 0x13 i.e. 99kHz
#define REG_I2C_CLK_H      8  // default 0x00
#define REG_I2C_TIMEOUT    9  // default 0x66
#define REG_I2C_STATE      10 // default I2C_OK, See I2C Status

// GPIO Configuration
#define IO_INPUT_LOW      0
#define IO_INPUT_HIGH     1
#define IO_PUSH_PULL      2
#define IO_OPEN_DRAIN     3

// I2C Clock Configuration
#define I2C_CLK_375KHZ    0x05  // 0x05 mimimum value
#define I2C_CLCK_99KHZ    0x13  // 19

// I2C Status
#define I2C_OK            0xF0
#define I2C_NACK_ON_ADDR  0xF1
#define I2C_NACK_ON_DATA  0xF2
#define I2C_TIME_OUT      0xF8

// ----------------- Application variables -----------

uint8_t rx_buf[BUFFER_SIZE]{0};
uint8_t i2c_received[BUFFER_SIZE]{0};

const uint8_t gpio_pins[] = {2, 4, 5, 12, 13, 14, 18, 19};
const char * device_id = "U-Bridge v1.0.0\0";

uint8_t i2c_state{0};
uint8_t ioStates = 0;
int _cnt{0};

// ---------------------------------------------------

void dump_data(const char* func, int len) {
  log_printf("%s [", func);
  for (int i=0; i< len; i++) {
    log_printf("%02X ", rx_buf[i]);
  }
  log_print("] ");
}


int wait_for_cmd() {
  
  int cnt = 0;
  memset(rx_buf, 0, BUFFER_SIZE);

  do {
    if(bridge.available()) {
      uint8_t c = bridge.read();
      switch (c) {
        case '\r':
          break;
        case '\n':
          rx_buf[cnt] = '\0';
          return cnt;
        default:
          rx_buf[cnt++] = c;
          if (cnt > BUFFER_SIZE) return -1;
      };
    }
  } while (true);

  return cnt;
  
}

/* 
 *  I2C write 
 *  return: i2c_state - the status from Wire.endTransmission which is sligtly 
 *  different from SC18IM704 status
 */
uint8_t i2c_write(uint8_t *data) {
  dump_data("I2C Write", _cnt);

  // buffer [0] = 'S', [1] = addr, [2] = len, [3] = data ... [4 + len] = 'P' (optional)
  uint8_t len = data[2];
  // minimum command with 'P' is 5 bytes, without 'P' would be only 4 bytes
  bool send_stop = ( (_cnt == (4 + len)) && (data[_cnt - 1] == 'P') );
  
  Wire.beginTransmission(data[1] >> 1);
  (len == 1) ? Wire.write(data[3]) : Wire.write(&data[3], len);
  i2c_state = Wire.endTransmission(send_stop);

  log_printf("len=%d cmd=%2x status=%2x\n", len, data[3], i2c_state);
  
  return i2c_state;
}


/*
 * I2C Read
 * read number of bytes from i2c and send the data back via UART
 */
void i2c_read(uint8_t addr, uint8_t bytes) {
  dump_data("I2C Read", _cnt);

  Wire.requestFrom(addr >> 1, (int) bytes);  // casting to keep compiler happy on warning of ambiguity

  while (Wire.available()==0) {};
  
  int i = 0;
  uint8_t rbuf[10]{0};
  while (Wire.available()) {
    rbuf[i++] = Wire.read();
  }

  int l = i;
  log_print(" <<< ");
  for (int i=0; i < l; i++) {
    log_printf("%02x ", rbuf[i]);
    bridge.write(rbuf[i]);
  }
  bridge.write('\0');
  log_println();
}


/*
 * CMD_REG_READ ('R') 
 * For now, it only support reding REG_IO_STATE and REG_I2C_STATE for practical reasons.
 * TO-DO: implmentation for return state for other registers.
 * 
 * REG_I2C_STATE return i2c error status in in the form of 0xFx, the statuses are similar 
 * to what is return from end.Transmission() which will be stored in _i2c_status.
 * Wire.endTransmission()               SC18IM704         Value
 *   0 - success                        I2C_OK            0xF0
 *   1 - data overflow transmit buffer  not supported
 *   2 - NACK on address                I2C_NACK_ON_ADDR  0xF1
 *   3 - NACK on data                   I2C_NACK_ON_DATA  0xF2
 *   4 - other error                    not supported
 *   5 - timeout                        I2C_TIME_OUT      0xF8
 */
void reg_read(uint8_t reg) {
  dump_data("Read Register", _cnt);

  if (reg == REG_IO_STATE) {
    log_printf(" <<< %02X\n", ioStates);
    bridge.write(ioStates);    
  }
  
  if (reg == REG_I2C_STATE) {
    uint8_t status = 0xF0;
    switch (i2c_state) {
      case 0:
        status = I2C_OK;
        break;
      case 2: 
        status = I2C_NACK_ON_ADDR;
        break;
      case 3:
        status = I2C_NACK_ON_DATA;
        break;
      case 5:
        status = I2C_TIME_OUT;
        break;
      default:
        status = i2c_state;
        break;
    }
    if (status == 1 || status == 4) 
      log_printf(" <<< unsupported status %X\n", status);
    else {
      log_printf(" <<< %X\n", status);
      bridge.write(status);
    }
  }

}


/*
 * Write to Registers
 * there are 3 use cases:
 * 1 - gpio_config
 *   'W' + REG_PORT_CONFIG1 + pin3-0 + REG_PORT_CONFIG2 + pin7-4 + 'P'
 *   all gpio_pins are configure as INPUT by default, therefore there is no need to
 *   explicitly config a gpio_pin as input unless the gpio_pin has been config 
 * 2 - uart_baud_rate_setting
 *   'W' + REG_BAUD_RATE_0 +  setting_l + REG_BAUD_RATE_1 + setting_h  + 'P'
 *    baud_rate = 7372800 /(16 + REG_BAUD_RATE_VALUES)
 *    REG_BAUD_RATE_SETTING = (7372800 / baud_rate) - 16;
 * 3 - set i2c clock
 *   'W' + REG_I2C_CLK_L + I2C_CLCK_VALUE + REG_I2C_CLK_H + 0 + 'P'
 */
void reg_write(uint8_t *buffer) {

  dump_data("Write Register", _cnt);
  
  uint16_t reg = buffer[3] << 8 | buffer[1];
  uint16_t val = buffer[4] << 8 | buffer[2];

  switch (reg) {
    case (REG_PORT_CONF2 << 8 | REG_PORT_CONF1): // config gpio
      log_printf(" config gpio port mode\n");      
      break;
    case (REG_BAUD_RATE_1 << 8 | REG_BAUD_RATE_0):   // config uart baud rate
      {
        uint32_t baud_rate[] = { 9600L,  14400L, 19200L, 28800L, 38400L, 57600L, 76800L, 115200L, 230400L, 460800L };
        uint16_t br_config[] = { 0x02f0, 0x01f0, 0x0170, 0x00f0, 0x00b0, 0x0070, 0x0050, 0x0030,  0x0010,  0x0000 };
        for (int i=0; i < sizeof(baud_rate)/sizeof(baud_rate[0]); i++) {
          if (val == br_config[i]) {
            log_printf(" config uart baud rate %d\n", baud_rate[i]);
            bridge.flush();
            bridge.begin(baud_rate[i]);
          }
        }
      }
      break;
    case (REG_I2C_CLK_H << 8 | REG_I2C_CLK_L):       // config i2c clock
      log_printf(" config i2c clock %luHz\n", (val==0x0005) ? 400000L : (val==0x0013)? 100000L : 0L);
      if (val == 0x0005)
        Wire.setClock(400000L);
      if (val == 0x0013)
        Wire.setClock(100000L);
      break;
    default:
      log_println("Unknown register settings");
      break;
  }
}


/* 
 * GPIO Read ('I' + 'P')
 * send states of all GPIO pins via UART 
 */
void gpio_read() {
  dump_data("Digital Read", _cnt);
  ioStates = 0;
  for (int i=0; i<8; i++) {
    uint8_t p = digitalRead(gpio_pins[i]);
    ioStates |= (p << i);
    log_printf(" pin%d=%d ", gpio_pins[i], p);
  }
  log_printf(" <<< %02X\n", ioStates);
  bridge.write(ioStates);
}


/* 
 * GPIO Write ('O' + Data + 'P')
 * set all gpio_pins based on the value of the state, each bit of the state represents
 *the value of one gpio pin to be set
 */
void gpio_write(uint8_t state) {
  dump_data("Digital Write", _cnt);
  for (int i=0; i<8; i++) {
    log_printf(" pin%d->%d ", gpio_pins[i], (state >> i) & 0x01);
    digitalWrite(gpio_pins[i], (state >> i) & 0x01);
  }
  log_println();
}


/*
 * Power-down ('Z' + 0x5a + 0xa5 + 'P')
 * Sending any character via UART should wake-up the chip but the char will be ignored
 */
void power_down() {
  // To-Do: to be implemented
  dump_data("Power Down", _cnt);
  log_println();
  return;
}


/* 
 * Read ID ('V' + 'P')
 * send 16-char (NULL terminator included) string defined in device_id via UART
 */
const char* read_id() {
  dump_data("Device iD", _cnt);
  log_printf(" <<< %s\n", device_id);
  bridge.write(device_id);
  return device_id;
}

// ---------------------------------------------------

void setup() {

  Serial.begin(115200);
  bridge.begin(115200);
  delay(200);

  for (int i=0; i<8; i++) {
    pinMode(gpio_pins[i], OUTPUT);
  }

  Wire.begin();
  Wire.setClock(400000L);

  bridge.write("OK");  // as per SC18IM704 spec
  
}


void loop() {

  _cnt = wait_for_cmd();
  
  switch (rx_buf[0]) {
    case CMD_START:
      if (rx_buf[1] & I2C_READ_BIT) {
        i2c_read(rx_buf[1], rx_buf[2]);
      }
      else {
       i2c_write(rx_buf);
      }
      break;
    case CMD_REG_READ: 
      reg_read(rx_buf[1]);
      break;
    case CMD_REG_WRITE: 
      reg_write(rx_buf);
      break;
    case CMD_GPIO_READ: 
      gpio_read();
      break;
    case CMD_GPIO_WRITE: 
      gpio_write(rx_buf[1]); 
      break;
    case CMD_POWER_DOWN: 
      power_down();
      break;
    case CMD_READ_ID:
      read_id();
      break;
    default:
      log_printf("Error: Unsupported UART command ");
      for (int i=0; i<_cnt; i++) {
        log_printf("%2X ", rx_buf[i]);
      }
      log_printf("\n");
      break;    
  }

  yield();
  
}
