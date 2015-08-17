#include <iostream>
#include <fstream>
#include <cstdint>
#include <array>
#include <vector>
#include <map>

static const uint8_t MaxPorts{4};

class raw_header_data
{
private:
    uint8_t Data[256];
public:
    const uint8_t* GetByteAddress(uint8_t Byte)
    {
        // NOTE(tyler): 8-bit integer will automatically clamp to 0-255 means no segfault worries.
        return Data+Byte;
    }
};

struct controller_state
{
    bool Start:1, A:1, B:1, X:1, Y:1, Z:1;
    bool DPadUp:1, DPadDown:1, DPadLeft:1, DPadRight:1;
    bool L:1, R:1;
    bool disc:1;
    bool reset:1;
    bool reserved:2;

    uint8_t LPressure{0};
    uint8_t RPressure{0};
    uint8_t ControlStickX{128};
    uint8_t ControlStickY{128};
    uint8_t CStickX{128};
    uint8_t CStickY{128};

    controller_state()
    {
        // Zero-intialize the two bytes of bitfields.
        memset(this, 0, 2);
    }
};

enum class port : uint8_t
{
    One = 0, // Port numbers aren't 0-based, but pointer arithmetic is.
    Two,
    Three,
    Four,
};

struct input_state
{
    controller_state ControllerState;
    port Port;
    uint32_t Frame;

    input_state(controller_state& ControllerState, port Port, uint32_t Frame);
    
    bool SetPort(uint8_t Port);
};

input_state::input_state(controller_state& ControllerState, port Port, uint32_t Frame)
        :
        ControllerState(ControllerState), Port(Port), Frame(Frame)
{
    
}

const uint8_t GetActivePortCount(uint8_t PortBitField)
{
    uint8_t ActivePortCount = 0;
    
    for(uint8_t port = 0; port < MaxPorts; port++)
    {
        ActivePortCount += (PortBitField >> port) & 1;
    }
    
    return ActivePortCount;
}

const uint8_t GetControllerOffset(uint8_t PortBitField, port Port)
{
    uint8_t ControllerOffset = 0;
    
    for(int port = 0; port < MaxPorts; port++)
    {
        if(((PortBitField >> port) & 1) == 1)
        {
            if(port == static_cast<int>(Port)) return ControllerOffset;
        }
        
        ControllerOffset++;
    }

    std::cout << "ERROR: Tried to get controller offset for empty port." << std::endl;
    return 0;
}

void
LoadHeaderFromDTM(std::string FileName, raw_header_data& Destination)
{
    std::ifstream ConfigFile("config.dtm", std::ios::in|std::ios::binary|std::ios::ate);
    if(ConfigFile.is_open())
    {
        std::streampos Size = ConfigFile.tellg();
        uint8_t* Memory = new uint8_t[Size];
        ConfigFile.seekg(0, std::ios::beg);
        ConfigFile.read((char*)Memory, Size);
        ConfigFile.close();

        memcpy(&Destination, Memory, 256);
    }
    else
    {
        std::cout << "ERROR: Failed to load header from config.dtm" << std::endl;
    }

}

int main()
{ 
    raw_header_data Header;
    LoadHeaderFromDTM("config.dtm", Header);

    uint8_t PortBitField = *Header.GetByteAddress(0xB);

    controller_state Shine;
    Shine.B = true;
    Shine.ControlStickY = 0;

    controller_state Down;
    Down.DPadDown = true;

    controller_state Jump;
    Jump.X = true;

    controller_state WavelandDown;
    WavelandDown.L = true;
    WavelandDown.ControlStickY = 0;
    
    controller_state WavelandRight;
    WavelandRight.L = true;
    WavelandRight.ControlStickX = 255;
    WavelandRight.ControlStickY = 0;

    controller_state WavelandLeft;
    WavelandLeft.L = true;
    WavelandLeft.ControlStickX = 0;
    WavelandLeft.ControlStickY = 0;
    
    std::vector<input_state> Inputs;

    int WSF = 16;

    for(port p = port::One; p != port::Four; ++p)
    {
        for(uint32_t i = 0; i < WSF*10*2; i += WSF*2)
        {
            Inputs.push_back({Shine, port::One, i});
            Inputs.push_back({Jump, port::One, i+3});
            Inputs.push_back({WavelandRight, port::One, i+6});
        
            Inputs.push_back({Shine, port::One, WSF+i});
            Inputs.push_back({Jump, port::One, WSF+i+3});
            Inputs.push_back({WavelandLeft, port::One, WSF+i+6});
        
        }
    }

    uint32_t LastFrame{};

    // NOTE(tyler): Find out how many frames our memory block needs to be.
    for(const input_state& inputState : Inputs)
    {
        if(inputState.Frame > LastFrame) LastFrame = inputState.Frame;
        // TODO(tyler): For some reason, the next line adds 2 padding frames instead of 1.
        LastFrame++; // Dolphin seems to need at least 1 padding frame at the end or it won't execute actions on the last frame.
    }

    uint32_t FrameCount = LastFrame + 1;

    controller_state* InputData = new controller_state[GetActivePortCount(PortBitField) * FrameCount]{};

    for(const input_state& inputState : Inputs)
    {
        memcpy(InputData + inputState.Frame * GetActivePortCount(PortBitField) + GetControllerOffset(PortBitField, inputState.Port),
               &inputState.ControllerState,
               sizeof(controller_state));
    }

    std::ofstream OutFile("NewFile.dtm", std::ios::out|std::ios::binary|std::ios::trunc);

    OutFile.write((char*)(Header.GetByteAddress(0)), 256);
    // TODO(tyler): This is potentially unsafe if PortBitField or FrameCount somehow gets changed.
    OutFile.write((char*)(InputData), GetActivePortCount(PortBitField) * FrameCount * sizeof(controller_state));

    OutFile.close();

    delete InputData;
    
    return 0;
}
