/*
 * libaps.cpp - Library for APSv2 control. 
 *
 */

#include "headings.h"
#include "libaps.h"
#include "APS2.h"
#include <sstream>
#include "asio.hpp"

map<string, APS2> APSs; //map to hold on to the APS instances
set<string> deviceSerials; // set of APSs that responded to an enumerate broadcast


// stub class to close the logger file handle when the driver goes out of scope
class CleanUp {
public:
	~CleanUp();
};

CleanUp::~CleanUp() {
	if (Output2FILE::Stream()) {
		fclose(Output2FILE::Stream());
	}
}
CleanUp cleanup_;

#ifdef __cplusplus
extern "C" {
#endif

int init() {
	//Create the logger
	FILE* pFile = fopen("libaps.log", "a");
	Output2FILE::Stream() = pFile;

	//Setup the network interface
	APSEthernet::get_instance().init();

	//Enumerate the serial numbers and MAC addresses of the devices attached
	enumerate_devices();

	return APS_OK;
}

int init_nolog() {
	//Setup the network interface
	APSEthernet::get_instance().init();

	//Enumerate the serial numbers and MAC addresses of the devices attached
	enumerate_devices();

	return APS_OK;
}

int enumerate_devices(){

	/*
	* Look for all APS devices in this APS Rack.
	*/

	set<string> oldSerials = deviceSerials;
	deviceSerials = APSEthernet::get_instance().enumerate();

	//See if any devices have been removed
	set<string> diffSerials;
	set_difference(oldSerials.begin(), oldSerials.end(), deviceSerials.begin(), deviceSerials.end(), std::inserter(diffSerials, diffSerials.begin()));
	for (auto serial : diffSerials) APSs.erase(serial);

	//Or if any devices have been added
	diffSerials.clear();
	set_difference(deviceSerials.begin(), deviceSerials.end(), oldSerials.begin(), oldSerials.end(), std::inserter(diffSerials, diffSerials.begin()));
	for (auto serial : diffSerials) APSs[serial] = APS2(serial);

	return APS_OK;
}

int get_numDevices(){
	return deviceSerials.size();
}

void get_deviceSerials(const char ** deviceSerialsOut){
	//Assumes sufficient memory has been allocated
	size_t ct = 0;
	for (auto serial : deviceSerials){
		deviceSerialsOut[ct] = serial.c_str();
		ct++;
	}
}

//Connect to a device specified by serial number string
//Assumes null-terminated deviceSerial
int connect_APS(const char * deviceSerial){
	return APSs[string(deviceSerial)].connect();
}

//Assumes a null-terminated deviceSerial
int disconnect_APS(const char * deviceSerial){
	return APSs[string(deviceSerial)].disconnect();
}

int reset(const char * deviceSerial, int resetMode){
	return APSs[string(deviceSerial)].reset(static_cast<APS_RESET_MODE_STAT>(resetMode));
}

//Initialize an APS unit
//Assumes null-terminated bitFile
int initAPS(const char * deviceSerial, int forceReload){
	try {
		return APSs[string(deviceSerial)].init(forceReload);
	} catch (std::exception& e) {
		string error = e.what();
		if (error.compare("Unable to open bitfile.") == 0) {
			return APS_FILE_ERROR;
		} else {
			return APS_UNKNOWN_ERROR;
		}
	}

}

int get_firmware_version(const char * deviceSerial) {
	return APSs[string(deviceSerial)].get_firmware_version();
}

double get_uptime(const char * deviceSerial){
	return APSs[string(deviceSerial)].get_uptime();
}

int set_sampleRate(const char * deviceSerial, int freq){
	return APSs[string(deviceSerial)].set_sampleRate(freq);
}

int get_sampleRate(const char * deviceSerial){
	return APSs[string(deviceSerial)].get_sampleRate();
}

//Load the waveform library as floats
int set_waveform_float(const char * deviceSerial, int channelNum, float* data, int numPts){
	return APSs[string(deviceSerial)].set_waveform( channelNum, vector<float>(data, data+numPts));
}

//Load the waveform library as int16
int set_waveform_int(const char * deviceSerial, int channelNum, int16_t* data, int numPts){
	return APSs[string(deviceSerial)].set_waveform(channelNum, vector<int16_t>(data, data+numPts));
}

int set_markers(const char * deviceSerial, int channelNum, uint8_t* data, int numPts) {
	return APSs[string(deviceSerial)].set_markers(channelNum, vector<uint8_t>(data, data+numPts));
}

int write_sequence(const char * deviceSerial, uint64_t* data, uint32_t numWords) {
	vector<uint64_t> dataVec(data, data+numWords);
	return APSs[string(deviceSerial)].write_sequence(dataVec);
}

int load_sequence_file(const char * deviceSerial, const char * seqFile){
	try {
		return APSs[string(deviceSerial)].load_sequence_file(string(seqFile));
	} catch (...) {
		return APS_UNKNOWN_ERROR;
	}
	// should not reach this point
	return APS_UNKNOWN_ERROR;
}

int clear_channel_data(const char * deviceSerial) {
	return APSs[string(deviceSerial)].clear_channel_data();
}

int run(const char * deviceSerial) {
	return APSs[string(deviceSerial)].run();
}

int stop(const char * deviceSerial) {
	return APSs[string(deviceSerial)].stop();
}

int get_running(const char * deviceSerial){
	return APSs[string(deviceSerial)].running;
}

//Expects a null-terminated character array
int set_log(const char * fileNameArr) {

	//Close the current file
	if(Output2FILE::Stream()) fclose(Output2FILE::Stream());
	
	string fileName(fileNameArr);
	if (fileName.compare("stdout") == 0){
		Output2FILE::Stream() = stdout;
		return APS_OK;
	}
	else if (fileName.compare("stderr") == 0){
		Output2FILE::Stream() = stdout;
		return APS_OK;
	}
	else{

		FILE* pFile = fopen(fileName.c_str(), "a");
		if (!pFile) {
			return APS_FILE_ERROR;
		}
		Output2FILE::Stream() = pFile;
		return APS_OK;
	}
}

int set_logging_level(int logLevel){
	FILELog::ReportingLevel() = TLogLevel(logLevel);
	return APS_OK;
}

int set_trigger_source(const char * deviceSerial, int triggerSource) {
	return APSs[string(deviceSerial)].set_trigger_source(TRIGGERSOURCE(triggerSource));
}

int get_trigger_source(const char * deviceSerial) {
	return int(APSs[string(deviceSerial)].get_trigger_source());
}

int set_trigger_interval(const char * deviceSerial, double interval){
	return APSs[string(deviceSerial)].set_trigger_interval(interval);
}

double get_trigger_interval(const char * deviceSerial){
	return APSs[string(deviceSerial)].get_trigger_interval();
}

int set_channel_offset(const char * deviceSerial, int channelNum, float offset){
	return APSs[string(deviceSerial)].set_channel_offset(channelNum, offset);
}
int set_channel_scale(const char * deviceSerial, int channelNum, float scale){
	return APSs[string(deviceSerial)].set_channel_scale(channelNum, scale);
}
int set_channel_enabled(const char * deviceSerial, int channelNum, int enable){
	return APSs[string(deviceSerial)].set_channel_enabled(channelNum, enable);
}

float get_channel_offset(const char * deviceSerial, int channelNum){
	return APSs[string(deviceSerial)].get_channel_offset(channelNum);
}
float get_channel_scale(const char * deviceSerial, int channelNum){
	return APSs[string(deviceSerial)].get_channel_scale(channelNum);
}
int get_channel_enabled(const char * deviceSerial, int channelNum){
	return APSs[string(deviceSerial)].get_channel_enabled(channelNum);
}

int set_run_mode(const char * deviceSerial, int mode) {
	return APSs[string(deviceSerial)].set_run_mode(RUN_MODE(mode));
}

//int save_state_files() {
//	return APSRack_.save_state_files();
//}
//
//int read_state_files() {
//	return APSRack_.read_state_files();
//}
//
//int save_bulk_state_file() {
//	string fileName = "";
//	return APSRack_.save_bulk_state_file(fileName);
//}
//int read_bulk_state_file() {
//	string fileName = "";
//	return APSRack_.read_bulk_state_file(fileName);
//}

int write_memory(const char * deviceSerial, uint32_t addr, uint32_t* data, uint32_t numWords) {
	vector<uint32_t> dataVec(data, data+numWords);
	return APSs[string(deviceSerial)].write_memory(addr, dataVec);
}

int read_memory(const char * deviceSerial, uint32_t addr, uint32_t* data, uint32_t numWords) {
	auto readData = APSs[string(deviceSerial)].read_memory(addr, numWords);
	std::copy(readData.begin(), readData.end(), data);
	return 0;
}

int read_register(const char * deviceSerial, uint32_t addr){
	uint32_t buffer[1];
	read_memory(deviceSerial, addr, buffer, 1);
	return buffer[0];
}

int program_FPGA(const char * deviceSerial, const char * bitFile) {
	return APSs[string(deviceSerial)].program_FPGA(string(bitFile));
}

int write_flash(const char * deviceSerial, uint32_t addr, uint32_t* data, uint32_t numWords) {
	vector<uint32_t> writeData(data, data+numWords);
	return APSs[string(deviceSerial)].write_flash(addr, writeData);
}
int read_flash(const char * deviceSerial, uint32_t addr, uint32_t numWords, uint32_t* data) {
	auto readData = APSs[string(deviceSerial)].read_flash(addr, numWords);
	std::copy(readData.begin(), readData.end(), data);
	return 0;
}
uint64_t get_mac_addr(const char * deviceSerial) {
	return APSs[string(deviceSerial)].get_mac_addr();
}
int set_mac_addr(const char * deviceSerial, uint64_t mac) {
	return APSs[string(deviceSerial)].set_mac_addr(mac);
}
const char * get_ip_addr(const char * deviceSerial) {
	uint32_t ip_addr = APSs[string(deviceSerial)].get_ip_addr();
	return asio::ip::address_v4(ip_addr).to_string().c_str();
}
int set_ip_addr(const char * deviceSerial, const char * ip_addr_str) {
	uint32_t ip_addr = asio::ip::address_v4::from_string(ip_addr_str).to_ulong();
	return APSs[string(deviceSerial)].set_ip_addr(ip_addr);
}
int write_SPI_setup(const char * deviceSerial) {
	return APSs[string(deviceSerial)].write_SPI_setup();
}

#ifdef __cplusplus
}
#endif

