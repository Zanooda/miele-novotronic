#pragma once

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <sstream>

namespace miele_novotronic {

class MC14489Pin : public esphome::esp8266::ESP8266GPIOPin {
public:
    MC14489Pin(const MC14489Pin&) = default;
    MC14489Pin(uint8_t pin, esphome::gpio::Flags flags, bool inverted = false) {
        set_pin(pin);
        set_flags(flags);
        set_inverted(inverted);
    }

    using esphome::esp8266::ESP8266GPIOPin::attach_interrupt;
};

template <typename T, size_t N>
class RingBuffer {
public:
    T values[N] = {};

    void push(T value) {
        values[_ptr] = value;
        _ptr = (_ptr + 1) % N;
        _size = std::min(_size + 1, N);
    }

    size_t size() const {
        return _size;
    }

private:
    size_t _ptr = 0;
    size_t _size = 0;
};

template <typename T, size_t Votes = 3>
class ConsensusBuffer {
public:
    operator T() const {
        return _consensusValue;
    }

    ConsensusBuffer& operator=(T nextValue) {
        _buffer.push(nextValue);
        updateConsensus();
        return *this;
    }

private:
    RingBuffer<T, Votes> _buffer;
    volatile T _consensusValue = T();

    void updateConsensus() {
        if (_buffer.size() < Votes) {
            return;
        }

        T value = _buffer.values[0];
        for (size_t i = 1; i < Votes; ++i) {
            if (value != _buffer.values[i]) {
                return;
            }
        }

        _consensusValue = value;
    }
};

class MC14489 {
public:
    MC14489(uint8_t csPin, MC14489Pin* data) : _data(*data), _cs(csPin, esphome::gpio::FLAG_INPUT, true) {
    }

    MC14489(const MC14489&) = delete;
    MC14489(MC14489&&) = delete;

    void setup() {
        _cs.setup();
        _cs.attach_interrupt(&handleChipSelect, this, esphome::gpio::INTERRUPT_ANY_EDGE);
    }

    static void ICACHE_RAM_ATTR HOT handleChipSelect(void* self) {
        reinterpret_cast<MC14489*>(self)->select();
    }

    void ICACHE_RAM_ATTR HOT select() {
        _selected = _cs.digital_read();
        if (_selected) {
            _buffer = 0;
            _bits = 0;
        } else {
            switch (_bits) {
                case 8:
                    _ctrlReg = uint8_t(_buffer);
                    ctrlUpdates++;
                    break;
                case 24:
                    _displayReg = _buffer;
                    displayUpdates++;
                    break;
                default:;
                    // glitch
            }
        }
    }

    void ICACHE_RAM_ATTR HOT tick() {
        if (!_selected) {
            return;
        }
        _buffer = (_buffer << 1) + _data.digital_read();
        _bits++;
    }

    uint32_t getDisplayReg() const {
        return _displayReg;
    }

    uint8_t getCtrlReg() const {
        return _ctrlReg;
    }

    static char decodeBank(uint8_t bankNibble, bool specialDecode) {
        if (!specialDecode) {
            if (bankNibble < 10) {
                return '0' + bankNibble;
            } else {
                return 'A' + bankNibble - 10;
            }
        } else {
            switch (bankNibble) {
                case 0x0: return ' ';
                case 0x1: return 'c';
                case 0x2: return 'H';
                case 0x3: return 'h';
                case 0x4: return 'J';
                case 0x5: return 'L';
                case 0x6: return 'n';
                case 0x7: return 'o';
                case 0x8: return 'P';
                case 0x9: return 'r';
                case 0xA: return 'U';
                case 0xB: return 'u';
                case 0xC: return 'y';
                case 0xD: return '-';
                case 0xE: return '=';
                case 0xF: return 'o';
            }
        }
        __builtin_unreachable();
    }

    volatile uint32_t displayUpdates = 0, ctrlUpdates = 0;

private:
    MC14489Pin _data;
    MC14489Pin _cs;

    bool _selected = false;

    uint32_t _buffer = 0;
    uint8_t _bits = 0;

    ConsensusBuffer<uint32_t> _displayReg;
    ConsensusBuffer<uint8_t> _ctrlReg;
};

class MotorolaLedDriverSniffer : public esphome::Component, public esphome::text_sensor::TextSensor {

public:
    MotorolaLedDriverSniffer(esphome::text_sensor::TextSensor* timeOutput, esphome::text_sensor::TextSensor* stateOutput)
        : _data(14, esphome::gpio::FLAG_INPUT) // D5
        , _clk(12, esphome::gpio::FLAG_INPUT) // D6
        , _left(4, &_data) // D2
        , _right(5, &_data) // D1
        , _timeOutput(timeOutput)
        , _stateOutput(stateOutput)
    {}

    MotorolaLedDriverSniffer(const MotorolaLedDriverSniffer&) = delete;
    MotorolaLedDriverSniffer(MotorolaLedDriverSniffer&&) = delete;

    float get_setup_priority() const override {
        return esphome::setup_priority::AFTER_CONNECTION;
    }

    static void ICACHE_RAM_ATTR handleClk(void* self) {
        reinterpret_cast<MotorolaLedDriverSniffer*>(self)->handleClkImpl();
    }

    void ICACHE_RAM_ATTR handleClkImpl() {
        _left.tick();
        _right.tick();
    }

    void setup() override {
        _data.setup();
        _clk.setup();
        _left.setup();
        _right.setup();

        _clk.attach_interrupt(&handleClk, this, esphome::gpio::INTERRUPT_RISING_EDGE);
    }

    using DisplayArray = std::array<char, 3>;

    bool isDisplayOff() const {
        return (_left.getCtrlReg() & 1) == 0;
    }

    DisplayArray getDisplay() const {
        auto ldispreg = _left.getDisplayReg();
        auto lctrlreg = _left.getCtrlReg();

        constexpr uint8_t specialDecode1 = 0x42;
        constexpr uint8_t specialDecode2 = 0x44;
        constexpr uint8_t specialDecode3 = 0x48;

        return {
            MC14489::decodeBank(ldispreg & 0xf, (lctrlreg & specialDecode1) == specialDecode1),
            MC14489::decodeBank((ldispreg >> 4) & 0xf, (lctrlreg & specialDecode2) == specialDecode2),
            MC14489::decodeBank((ldispreg >> 8) & 0xf, (lctrlreg & specialDecode3) == specialDecode3),
        };
    }

    std::string formatTime() const {
        std::ostringstream str;

        if (isDisplayOff()) {
            return " ";
        }

        auto disp = getDisplay();
        if (disp[0] == '-') {
            return "---";
        }
        if (disp[2] == ' ') {
            return " ";
        }

        bool hasHours = disp[0] != ' ';
        if (hasHours) {
            str << disp[0] << "h ";
        }

        if (disp[1] != ' ') {
            str << disp[1];
        }
        if (disp[2] != ' ') {
            str << disp[2];
        }

        if (hasHours) {
            str << "m";
        } else {
            str << " min";
        }

        return str.str();
    }

    enum State {
        MIELE_NORMAL,
        MIELE_DOOR_OPEN,
        MIELE_FAULT,
    };

    State decodeState() const {
        auto lctrlreg = _left.getCtrlReg();

        if (isDisplayOff()) {
            return MIELE_DOOR_OPEN;
        }
        if (getDisplay()[0] == '-') {
            return MIELE_FAULT;
        }
        return MIELE_NORMAL;
    }

    std::string formatState() const {
        auto rdispreg = _right.getDisplayReg();
        auto rctrlreg = _right.getCtrlReg();

        std::vector<std::string> states;

        auto progress = ((rdispreg & 0xf000) >> 8) | (rdispreg & 0x7);

        auto state = decodeState();
        switch (state) {
            case MIELE_DOOR_OPEN:
                states.emplace_back("Door open");
                break;

            case MIELE_FAULT:
                states.emplace_back("Fault");
                break;

            default:
                switch (progress) {
                    case 0x10: states.emplace_back("Pre-washing"); break;
                    case 0x20: states.emplace_back("Washing"); break;
                    case 0x40: states.emplace_back("Rinsing"); break;
                    case 0x80: states.emplace_back("Paused Rinse"); break;
                    case 0x01: states.emplace_back("Pumping"); break;
                    case 0x02: states.emplace_back("Spinning"); break;
                    case 0x04: {
                        if (getDisplay() == DisplayArray{' ', ' ', '0'}) {
                            states.emplace_back("Finished");
                        } else {
                            states.emplace_back("Idle");
                        }
                        break;
                    }
                    case 0: states.emplace_back("Ready"); break;
                }
        }

        auto centrifugeSetting = (rdispreg & 0x7e0000) >> 16;
        switch (centrifugeSetting) {
            case 0x02: states.emplace_back("Ø 1600"); break;
            case 0x04: states.emplace_back("Ø 1400"); break;
            case 0x08: states.emplace_back("Ø 1200"); break;
            case 0x50: states.emplace_back("Ø 900"); break;
            case 0x40: states.emplace_back("Ø 600"); break;
            case 0x30: states.emplace_back("Ø 400"); break;
            case 0x20: states.emplace_back("Ø Rinse-pause"); break;
            case 0x10: states.emplace_back("Ø No"); break;
        }

        if (rdispreg & 0x100) {
            states.emplace_back("Pre-wash");
        }
        if (rdispreg & 0x40) {
            states.emplace_back("Short");
        }
        if (rdispreg & 0x80) {
            states.emplace_back("Wasser Plus");
        }
        if (rdispreg & 0x010000) {
            states.emplace_back("Summer");
        }

        std::ostringstream str;
        for (auto& state : states) {
            if (str.tellp() > 0) {
                str << ", ";
            }
            str << state;
        }

        return str.str();
    }

    static void publishNew(esphome::text_sensor::TextSensor* sensor, std::string newState) {
        if (sensor->state != newState) {
            sensor->publish_state(std::move(newState));
        }
    }

    void loop() override {
        publishNew(_timeOutput, formatTime());
        publishNew(_stateOutput, formatState());
    }

    MC14489Pin _data, _clk;
    MC14489 _left, _right;

    esphome::text_sensor::TextSensor* _timeOutput;
    esphome::text_sensor::TextSensor* _stateOutput;
};

}  // namespace miele_novotronic
