#pragma once
#include <Arduino.h>
#include <SPI.h>

/*
  Minimal MCP3561/2/4R Arduino driver
  - SPI mode 0 (works with mode 1 as well)
  - Default 32-bit data (sign-extended + 24b code)
  - Supports start/standby/shutdown/reset fast commands
  - MUX, OSR, gain, clock, VREF, CRC-on-comms (optional)

  Datasheet refs:
    - Command types & fast commands (Table 6-2)      DS20006391C p.68
    - ADCDATA register (0x00)                        p.90
    - CONFIG0 (0x01): VREF, clock, ADC_MODE         p.91
    - CONFIG1 (0x02): PRE, OSR                       p.92
    - CONFIG2 (0x03): BOOST, GAIN, AZ_*              p.93
    - CONFIG3 (0x04): CONV_MODE, DATA_FORMAT, CRC   p.94
    - IRQ (0x05): DR_STATUS, POR, CRC flags          p.95
    - SPI mode/timing (Mode 0/1, up to 20MHz)        p.12-14
*/

class MCP356x {
 public:
  enum : uint8_t {
    // Register addresses (6-bit ADDR field)
    REG_ADCDATA = 0x00,  // 24/32b (read)
    REG_CONFIG0 = 0x01,
    REG_CONFIG1 = 0x02,
    REG_CONFIG2 = 0x03,
    REG_CONFIG3 = 0x04,
    REG_IRQ     = 0x05,
    REG_MUX     = 0x06,
    // ... (others exist but not required for basic operation)
  };

  // COMMAND type (CMD[1:0])
  enum : uint8_t {
    CMD_STATIC_READ       = 0b01,
    CMD_INCREMENTAL_WRITE = 0b10,
    CMD_INCREMENTAL_READ  = 0b11,
    CMD_FAST              = 0b00
  };

  // Fast command upper nibble in CMD[5:2] (CMD[1:0]==00)
  enum : uint8_t {
    FAST_START    = 0b1010,  // overwrite ADC_MODE=11
    FAST_STANDBY  = 0b1011,  // ADC_MODE=10
    FAST_ADCSHDN  = 0b1100,  // ADC_MODE=00
    FAST_FULLSHDN = 0b1101,  // CONFIG0=0x00
    FAST_RESET    = 0b1110   // full reset of map
  };

  // CONFIG0 helpers
  enum ClockSel : uint8_t {
    CLK_EXT       = 0b00,  // external
    CLK_EXT_      = 0b01,  // external (alias)
    CLK_INT_NOOUT = 0b10,  // internal RC, no MCLK out
    CLK_INT_OUT   = 0b11   // internal RC, MCLK out on MCLK pin
  };

  // CONFIG1: OSR
  enum OSR : uint8_t {
    OSR_32 = 0x0,
    OSR_64,
    OSR_128,
    OSR_256,
    OSR_512,
    OSR_1024,
    OSR_2048,
    OSR_4096,
    OSR_8192 = 0x8,
    OSR_16384,
    OSR_20480,
    OSR_24576,
    OSR_40960,
    OSR_49152,
    OSR_81920,
    OSR_98304
  };

  // CONFIG2: Gain
  enum Gain : uint8_t {
    GAIN_DIV3 = 0,
    GAIN_1    = 1,
    GAIN_2    = 2,
    GAIN_4    = 3,
    GAIN_8    = 4,
    GAIN_16   = 5,
    GAIN_32   = 6,
    GAIN_64   = 7
  };

  // CONFIG3: Data format & conversion mode
  enum DataFormat : uint8_t {
    DF_24_LEFT_32       = 0b01,  // 24-bit left-justified in 32 bits
    DF_25_RIGHT_32      = 0b10,  // 25-bit right-justified (8-bit sign ext + 24)
    DF_25_RIGHT_32_CHID = 0b11   // + channel ID
  };
  enum ConvMode : uint8_t {
    ONE_SHOT_SHDN = 0b00,  // one-shot then ADC shutdown
    ONE_SHOT_STBY = 0b10,  // one-shot then standby
    CONTINUOUS    = 0b11   // continuous conversion
  };

  struct Reading {
    int32_t code;  // sign-extended 24-bit code
    uint8_t chid;  // 0..15 when DF_*_CHID mode, else 0xFF
  };

  MCP356x(uint8_t cs, int8_t irq = -1) : _cs(cs), _irq(irq) {
  }

  // DEBUG SHIM: safe wrapper to test link
  bool _readReg_public(uint8_t addr6, uint8_t* data, size_t len) {
    return readReg(addr6, data, len);
  }

  void begin(SPIClass& spi = SPI, uint32_t spiHz = 4000000,
             uint8_t spiMode = SPI_MODE0) {
    _spi = &spi;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    if (_irq >= 0)
      pinMode(_irq, INPUT_PULLUP);
    _spiSettings = SPISettings(spiHz, MSBFIRST, spiMode);
  }

  // --- Basic control ---
  void hwReset() {
    fastCommand(FAST_RESET);
    delayMicroseconds(20);
  }
  void startConversion() {
    fastCommand(FAST_START);
  }
  void standby() {
    fastCommand(FAST_STANDBY);
  }
  void adcShutdown() {
    fastCommand(FAST_ADCSHDN);
  }
  void fullShutdown() {
    fastCommand(FAST_FULLSHDN);
  }

  // --- Configuration helpers ---
  // CONFIG0: Vref internal (2.4V) or external, clock, ADC mode
  bool setConfig0(bool vrefInternal, ClockSel clkSel, uint8_t cs_sel = 0b00,
                  uint8_t adc_mode = 0b10) {
    uint8_t v = 0;
    v |= (vrefInternal ? 1 : 0) << 7;
    v |= (clkSel & 0x03) << 4;
    v |= (cs_sel & 0x03) << 2;
    v |= (adc_mode & 0x03);
    return writeReg(REG_CONFIG0, &v, 1);
  }

  // CONFIG1: PRE (prescaler= MCLK/(1,2,4,8)), OSR
  bool setConfig1(uint8_t pre /*0..3*/, OSR osr) {
    uint8_t v = ((pre & 0x03) << 6) | ((uint8_t(osr) & 0x0F) << 2);
    return writeReg(REG_CONFIG1, &v, 1);
  }

  // CONFIG2: BOOST, GAIN, AZ_MUX, AZ_REF
  bool setConfig2(uint8_t boost /*0..3*/, Gain gain, bool az_mux = false,
                  bool az_ref = true) {
    uint8_t v = ((boost & 0x03) << 6) | ((uint8_t(gain) & 0x07) << 3) |
                ((az_mux ? 1 : 0) << 2) | ((az_ref ? 1 : 0) << 1) | 0x01;
    return writeReg(REG_CONFIG2, &v, 1);
  }

  // CONFIG3: mode, data format, CRC on comms (optional)
  bool setConfig3(ConvMode mode, DataFormat fmt = DF_25_RIGHT_32,
                  bool crc_on = false, bool crc_32 = false,
                  bool en_offcal = false, bool en_gaincal = false) {
    uint8_t v = (uint8_t(mode) & 0x03) << 6;
    v |= (uint8_t(fmt) & 0x03) << 4;
    v |= (crc_32 ? 1 : 0) << 3;
    v |= (crc_on ? 1 : 0) << 2;
    v |= (en_offcal ? 1 : 0) << 1;
    v |= (en_gaincal ? 1 : 0) << 0;
    return writeReg(REG_CONFIG3, &v, 1);
  }

  // IRQ: set IRQ pin behavior / read flags
  bool setIRQMode(bool mdat = false, bool inactiveHigh = false,
                  bool enableFastCmd = true, bool enStartInt = true) {
    uint8_t v = 0;
    v |= (mdat ? 1 : 0) << 3;           // IRQ_MODE[1]
    v |= (inactiveHigh ? 1 : 0) << 2;   // IRQ_MODE[0]
    v |= (enableFastCmd ? 1 : 0) << 1;  // EN_FASTCMD
    v |= (enStartInt ? 1 : 0) << 0;     // EN_STP
    return writeReg(REG_IRQ, &v, 1);
  }

  // Select input MUX (see datasheet MUX register definition).
  // 'pos' and 'neg' are 4-bit channel codes: 0..7 for CH0..CH7, 8=TEMP, 9=AVDD,
  // 10=REFIN+, 11=REFIN-, 12=AGND
  bool setMux(uint8_t pos, uint8_t neg) {
    uint8_t v = ((pos & 0x0F) << 4) | (neg & 0x0F);
    return writeReg(REG_MUX, &v, 1);
  }

  // Poll IRQ register for DRDY (DR_STATUS==0 => new data available)
  bool dataReady() {
    uint8_t irq;
    if (!readReg(REG_IRQ, &irq, 1))
      return false;
    return ((irq & (1 << 6)) == 0);  // DR_STATUS bit 6: 0 means new data
  }

  // Quick check using interrupt pin (active low when used as IRQ)
  bool irqAsserted() const {
    return (_irq >= 0) ? (digitalRead(_irq) == LOW) : false;
  }

  // Read latest ADC result (32-bit format recommended)
  bool readData32(Reading& out, bool chidInFormat = false) {
    uint8_t buf[4];
    if (!readReg(REG_ADCDATA, buf, 4))
      return false;
    if (chidInFormat) {
      out.chid    = (buf[0] >> 4) & 0x0F;  // CHID[3:0]
      int32_t sgn = (int8_t) ((buf[0] & 0x0F)
                              << 4);  // upper nibble sign ext (part of 25-bit)
      int32_t x   = (int32_t(sgn) << 24) | (int32_t(buf[1]) << 16) |
                  (int32_t(buf[2]) << 8) | buf[3];
      out.code = (x & 0x01FFFFFF);  // keep 25b
      // Sign-extend 25-bit:
      if (out.code & 0x01000000)
        out.code |= 0xFE000000;
    }
    else {
      // DF_25_RIGHT_32 default: buf[0] is 8-bit sign extension
      int32_t x = (int32_t(buf[0]) << 24) | (int32_t(buf[1]) << 16) |
                  (int32_t(buf[2]) << 8) | buf[3];
      out.code = x;  // already sign-extended to 32b
      out.chid = 0xFF;
    }
    return true;
  }

  // Read 24-bit raw (when using DATA_FORMAT=01 left-justified in 32 bits, use
  // readData32 instead)
  bool readData24(int32_t& code24) {
    uint8_t b[3];
    if (!readReg(REG_ADCDATA, b, 3))
      return false;
    int32_t x = (int32_t(b[0]) << 16) | (int32_t(b[1]) << 8) | b[2];
    if (x & 0x800000)
      x |= 0xFF000000;  // sign-extend
    code24 = x;
    return true;
  }

  // Convert code to volts given Vref and gain (FSR = ±Vref/Gain)
  // Code is 24-bit two's complement scaled to ±(2^23 - 1)
  static float codeToVolts(int32_t code, float vref, float gain) {
    const float denom = 8388607.0f;  // 2^23 - 1
    return (float) code / denom * (vref / gain);
  }

 private:
  // Build and send command byte: upper 6 bits = ADDR or fast code; lower 2 bits
  // = type
  inline uint8_t makeCmd(uint8_t upper6, uint8_t type2) {
    return uint8_t((upper6 & 0x3F) << 2) | (type2 & 0x03);
  }

  bool fastCommand(uint8_t fastCodeUpper4) {
    uint8_t upper6 = (fastCodeUpper4 << 2);  // CMD[5:2] = fastCode, CMD[1:0]=00
    uint8_t cmd    = makeCmd(upper6, CMD_FAST);
    select();
    _spi->transfer(cmd);
    deselect();
    return true;
  }

  bool writeReg(uint8_t addr6, const uint8_t* data, size_t len) {
    select();
    _spi->transfer(makeCmd(addr6, CMD_INCREMENTAL_WRITE));
    for (size_t i = 0; i < len; i++)
      _spi->transfer(data[i]);
    deselect();
    return true;
  }

  bool readReg(uint8_t addr6, uint8_t* data, size_t len) {
    select();
    _spi->transfer(makeCmd(addr6, CMD_STATIC_READ));
    for (size_t i = 0; i < len; i++)
      data[i] = _spi->transfer(0x00);
    deselect();
    return true;
  }

  inline void select() {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_cs, LOW);
  }
  inline void deselect() {
    digitalWrite(_cs, HIGH);
    _spi->endTransaction();
  }

  SPIClass*   _spi{nullptr};
  SPISettings _spiSettings{4000000, MSBFIRST, SPI_MODE0};
  uint8_t     _cs;
  int8_t      _irq;
};
