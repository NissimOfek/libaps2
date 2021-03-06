#include "DummyAPS.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include "EthernetControl.h"
#include "constants.h"
#include "logger.h"

#ifdef _WIN32
#include "concol.h"
#endif 

using std::cout;
using std::endl;
using std::ofstream;



DummyAPS::DummyAPS( string dev ) : pll_cycles_(00), pll_bypass_(0x80) {


	memset(&statusRegs_, 0, sizeof(struct APS_Status_Registers));

	statusRegs_.hostFirmwareVersion = 0x000A0001;
	statusRegs_.userFirmwareVersion = 0x00000001;
	statusRegs_.configurationSource = BASELINE_IMAGE;

    // setup some default registers
    user_registers_[FPGA_ADDR_VERSION] = 0x0;
    user_registers_[FPGA_ADDR_PLL_STATUS] = (1 << PLL_02_LOCK_BIT) | (1 << PLL_13_LOCK_BIT) | (1 << REFERENCE_PLL_LOCK_BIT);

    outboundPacketPtr_ = &outboundPacket_[0];

    for (int cnt = 0; cnt < MAX_APS_CHANNELS; cnt++) {
        dacs.push_back({0,0,0,0});
    }
	
    EthernetControl::debugAPSEcho(dev,  this);
}

unsigned char * DummyAPS::packetCallback(const void * voidDataPtr, size_t & length) {

    uint32_t * data = reinterpret_cast<uint32_t*>(const_cast<void*>(voidDataPtr));

	APSEthernetHeader * eh = (APSEthernetHeader *) data;
    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;

    setcolor(red, black);
	cout << "Recv ";
    setcolor(white,black);
    cout << "SeqNum: " << eh->seqNum;
    //cout << "Src: " << EthernetControl::print_ethernetAddress(eh->src);
    //out << " Dest: " << EthernetControl::print_ethernetAddress(eh->dest);
    cout << " Len: " << length;
    cout << " Command: " << APS2::printAPSCommand(eh->command) << endl;

    // update sequece number information
    
    if (eh->seqNum != 0) {
        if (eh->seqNum == seqnum_) statusRegs_.sequenceDupCount++;
        if (eh->seqNum > (seqnum_ + 1)) statusRegs_.sequenceSkipCount++;
        seqnum_ = eh->seqNum;
    }

    statusRegs_.receivePacketCount++;

    if (eh->command.cmd == APS_COMMAND_RESET) {
    	length = reset();
    } 

    if (eh->command.cmd == APS_COMMAND_STATUS) {
        length = status(data, length);
    }


    if ((eh->command.cmd == APS_COMMAND_FPGACONFIG_ACK) || 
        (eh->command.cmd == APS_COMMAND_FPGACONFIG_NACK)
       ) {
        length = recv_fpga_file(data, length);
    }

    if (eh->command.cmd == APS_COMMAND_FPGACONFIG_CTRL) {
        length = select_fpga_program();
    }

 
    if (eh->command.cmd == APS_COMMAND_CHIPCONFIGIO) {
        length = chip_config(data, length);
    }

    if ((eh->command.cmd == APS_COMMAND_USERIO_ACK) || 
        (eh->command.cmd == APS_COMMAND_USERIO_NACK)
       ) {
        length = user_io(data, length);
    }

    if (length > 0) {
        // packet will be sent so update count
        statusRegs_.sendPacketCount++;
    }

    dh->seqNum = eh->seqNum;

    unsigned char *ptr = reinterpret_cast<unsigned char *>(outboundPacketPtr_);

    return ptr;

}

size_t DummyAPS::reset() {

    setcolor(purple,black);
    cout << "reset()" << endl;
    setcolor(white,black);

    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;

    // reset status register values
    
    statusRegs_.sendPacketCount = 0;
    statusRegs_.receivePacketCount = 0;
    statusRegs_.sequenceSkipCount = 0;
    statusRegs_.sequenceDupCount = 0;
    statusRegs_.uptime = 0;

    bootTime_ = std::chrono::steady_clock::now();

    // remove fpga bit file
    ofstream bitFile ("fpga.bit", ofstream::out | ofstream::binary);
    bitFile.close();

    // copy status registers
    memcpy((uint8_t *) getPayloadPtr(outboundPacketPtr_), &statusRegs_, sizeof(struct APS_Status_Registers));
    
    return sizeof(struct APSEthernetHeader) + sizeof(struct APS_Status_Registers);
}

size_t DummyAPS::status(uint32_t * frameData,  size_t & length) {

    setcolor(purple,black);
    cout << "status()" << endl;
    setcolor(white,black);

    APSEthernetHeader * eh = (APSEthernetHeader *) frameData;
    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;

    std::fill(outboundPacket_, outboundPacket_ + frameLenWords, 0);

    dh->command.packed = eh->command.packed;
    dh->command.ack = 1;
    
    statusRegs_.uptime = uptime();

    // copy status registers
    memcpy((uint8_t *) getPayloadPtr(outboundPacketPtr_), &statusRegs_, sizeof(struct APS_Status_Registers));
    
    return sizeof(struct APSEthernetHeader) + sizeof(struct APS_Status_Registers);
}

unsigned int DummyAPS::uptime() {
    std::chrono::time_point<std::chrono::steady_clock> t;
    t = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t - bootTime_).count();
}

size_t DummyAPS::recv_fpga_file(uint32_t * frameData,  size_t & length) {

    ofstream bitFile ("fpga.bit", ofstream::out | ofstream::binary |  ofstream::app);

    APSEthernetHeader * eh = (APSEthernetHeader *) frameData;
    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;
    std::fill(outboundPacket_,outboundPacket_ + frameLenWords, 0);

    dh->command.packed = 0;
    dh->command.cmd = APS_COMMAND_FPGACONFIG_ACK;
    dh->command.ack = 1;

    setcolor(purple,black);
    cout << "recv_fpga_file: ";
    setcolor(white,black);

    unsigned char *data;

    uint32_t addr;

    size_t payloadLen;

    data = (unsigned char *) frameData;
    data += sizeof(APSEthernetHeader);

    payloadLen = length - sizeof(APSEthernetHeader);

    if (payloadLen < sizeof(uint32_t)) cout << "Error payload does not contain addr" << endl;

    addr = eh->addr;

    bitFile.seekp(addr * sizeof(uint32_t));

    // convert from bytes to words
    payloadLen /= sizeof(uint32_t); 

    // test to make sure length matches
    if (payloadLen != eh->command.cnt) {
        dh->command.mode_stat = 0x01;
        cout << "Error payload length " << payloadLen << " does not match cnt " << eh->command.cnt << endl;
    }  else {
        cout << "addr = " << std::hex << addr << " len = " << std::dec << payloadLen << endl;
        bitFile.write((const char *) data, payloadLen*sizeof(uint32_t));
    }

    bitFile.close();

    // ack frame if required
    if (eh->command.cmd == APS_COMMAND_FPGACONFIG_ACK) {
        return sizeof(struct APSEthernetHeader);
    } 

    return 0;
}

size_t DummyAPS::select_fpga_program() {
    // mimic a reprogram
    
    setcolor(purple,black);
    cout << "select_fpga_program" << endl;
    setcolor(white,black);

    // sleep random amount of time and then send host interface registers

    std::chrono::milliseconds dura( rand() % 1000 );
    std::this_thread::sleep_for( dura );

    // dummy up expected FPGA version
    user_registers_[FPGA_ADDR_VERSION] = FIRMWARE_VERSION;

    // copy status registers
    memcpy((uint8_t *)getPayloadPtr(outboundPacketPtr_), &statusRegs_, sizeof(struct APS_Status_Registers));
    
    return sizeof(struct APSEthernetHeader) + sizeof(struct APS_Status_Registers);
}

size_t DummyAPS::user_io(uint32_t * frameData,  size_t & length) {
    // mimic a reprogram
    

    APSEthernetHeader * eh = (APSEthernetHeader *) frameData;
    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;
    std::fill(outboundPacket_,outboundPacket_ + frameLenWords, 0);

    setcolor(purple,black);
    cout << "user_io ";
    setcolor(white,black);
    cout << ((eh->command.r_w) ? "read " : "write ") << std::hex << eh->addr << std::dec << " ";

    dh->command.packed = 0;
    dh->command.cmd = APS_COMMAND_USERIO_ACK;
    dh->command.ack = 1;

    uint32_t * inRegValue  = getPayloadPtr(frameData);
    uint32_t * outRegValue = getPayloadPtr(outboundPacket_);

    if (eh->command.r_w == 0) { // write
        cout << "val = " << *inRegValue << endl;
        user_registers_[eh->addr] = *inRegValue;
    } else {
        *outRegValue = user_registers_[eh->addr];
        cout << "val = " << *outRegValue << endl;
    }

    /*
    cout << "Register State" << endl;
    for (auto reg: user_registers_) {
        cout << std::hex << "Register: " << reg.first << " = " << std::dec << reg.second << endl;
    }
    */
    
    // ack frame if required
    if (eh->command.cmd == APS_COMMAND_USERIO_ACK) {
        return sizeof(struct APSEthernetHeader) + sizeof(uint32_t);
    } 

    return 0;
}

size_t DummyAPS::chip_config(uint32_t * frameData,  size_t & length) {

    APSEthernetHeader * eh = (APSEthernetHeader *) frameData;
    APSEthernetHeader * dh = (APSEthernetHeader *) outboundPacket_;
    std::fill(outboundPacket_,outboundPacket_ + frameLenWords, 0);

    setcolor(purple,black);
    cout << "chip_config ";
    setcolor(white,black);

    dh->command.packed = eh->command.packed;
    dh->command.ack = 1;

    APSChipConfigCommand_t chipCmd;


    uint32_t * inValue =  getPayloadPtr(frameData);
    uint32_t * outValue = getPayloadPtr(outboundPacket_);

    chipCmd.packed = inValue[0];

    // byte swap from big endian
    
    chipCmd.packed = ntohl(chipCmd.packed);
    
    cout << "chip command = " << APS2::printAPSChipCommand(chipCmd) << endl;




    switch(chipCmd.target) {
        case CHIPCONFIG_IO_TARGET_PAUSE:         
            break;
        case CHIPCONFIG_IO_TARGET_DAC_0_MULTI: 
            break;
        case CHIPCONFIG_IO_TARGET_DAC_1_MULTI: 
            break;

        case CHIPCONFIG_IO_TARGET_PLL_MULTI: 
           
            break;
        case CHIPCONFIG_IO_TARGET_DAC_0_SINGLE: 
        case CHIPCONFIG_IO_TARGET_DAC_1_SINGLE: 
            {
            DACCommand_t dacCmd;
            dacCmd.packed = chipCmd.instr;
            int dac = (chipCmd.target == CHIPCONFIG_IO_TARGET_DAC_0_SINGLE) ? 0 : 1;
            uint8_t data = inValue[1] & 0xFF;

            uint8_t cmd = dacCmd.addr & 0x0F;
            
            auto invalidDacCMD = [] () {  
                setcolor(red,black);
                cout << "Error: ";
                setcolor(white, black);
                cout << "Invalid DAC cmd" << endl; 
            };

            if (dacCmd.r_w) { // if read 
                switch (cmd) {
                    case 0x1: *outValue = dacs[dac].interrupt; break;
                    case 0x6: *outValue = dacs[dac].controller; break;
                    case 0x5: *outValue = dacs[dac].sd; break;
                    case 0x4: *outValue = dacs[dac].msdMhd; break;
                    default: invalidDacCMD(); break;
                }
            } else {
                switch (cmd) {
                    case 0x1: dacs[dac].interrupt = data; break;
                    case 0x6: dacs[dac].controller = data; break;
                    case 0x5: dacs[dac].sd = data; break;
                    case 0x4: dacs[dac].msdMhd = data; break;
                    default: invalidDacCMD(); break;
                }
            }

            cout << "DAC[" << dac << "] Addr " << dacCmd.addr << " r/w " << dacCmd.r_w << " Data " << data << endl ;
            }
            break;
        case CHIPCONFIG_IO_TARGET_PLL_SINGLE:  
            {
                PLLCommand_t pllcmd;
                pllcmd.packed = chipCmd.instr;
                uint8_t data = inValue[1] & 0xFF;
                if (pllcmd.r_w && pllcmd.addr == FPGA1_PLL_CYCLES_ADDR) {
                    *outValue = pll_cycles_;
                } else if (pllcmd.r_w && pllcmd.addr == FPGA1_PLL_BYPASS_ADDR) {
                    *outValue = pll_bypass_;
                } else if (!pllcmd.r_w && pllcmd.addr == FPGA1_PLL_CYCLES_ADDR) {
                    pll_cycles_ = data;
                } else if (!pllcmd.r_w && pllcmd.addr == FPGA1_PLL_BYPASS_ADDR) {
                    pll_bypass_ = data;
                }
            }
            break;
        case CHIPCONFIG_IO_TARGET_VCXO: 
            break;
        case CHIPCONFIG_IO_TARGET_EOL: 
            break;
        default:
            dh->command.mode_stat = CHIPCONFIG_INVALID_TARGET;
            FILE_LOG(logERROR) << "Invalid Chip Config Target";
    }
 
    return sizeof(struct APSEthernetHeader) + sizeof(uint32_t);
}


